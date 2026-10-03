#include "mavlink_link.h"

#include <Preferences.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "link_monitor.h"
#include "telemetry_decoder.h"
#include "telemetry_store.h"

namespace {

struct StreamRequest {
    uint16_t msgId;
    uint32_t intervalUs;
};

// Message rates requested from ArduPilot via MAV_CMD_SET_MESSAGE_INTERVAL.
// These are not persisted by the FC, so they are re-sent after every reconnect.
const StreamRequest STREAMS[] = {
    {MAVLINK_MSG_ID_SYS_STATUS, 500000},
    {MAVLINK_MSG_ID_BATTERY_STATUS, 1000000},
    {MAVLINK_MSG_ID_GLOBAL_POSITION_INT, 200000},
    {MAVLINK_MSG_ID_GPS_RAW_INT, 500000},
    {MAVLINK_MSG_ID_VFR_HUD, 200000},
    {MAVLINK_MSG_ID_ATTITUDE, 100000},
    {MAVLINK_MSG_ID_SYSTEM_TIME, 1000000},
};

constexpr size_t BAUD_COUNT = sizeof(cfg::MAV_BAUDS) / sizeof(cfg::MAV_BAUDS[0]);
constexpr size_t PROFILE_COUNT = BAUD_COUNT * 2;  // straight + swapped pins per baud

uint32_t profileBaud(size_t profile) { return cfg::MAV_BAUDS[profile / 2]; }
bool profileSwapped(size_t profile) { return (profile % 2) != 0; }

class MavlinkLink {
public:
    explicit MavlinkLink(HardwareSerial& uart) : uart_(uart), monitor_(cfg::LINK_TIMEOUT_MS) {}

    void run() {
        openUart(loadProfile());
        if (esp_task_wdt_add(nullptr) != ESP_OK) Serial.println("[mav] task WDT subscribe failed");
        for (;;) {
            esp_task_wdt_reset();
            const uint32_t now = millis();
            readUart(now);
            sendHeartbeatIfDue(now);
            handleLinkEvent(monitor_.update(now), now);
            retryStreamsIfStale(now);
            recoverSilentUart(now);
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

private:
    void openUart(size_t profile) {
        profile_ = profile;
        const uint32_t baud = profileBaud(profile);
        const bool swapped = profileSwapped(profile);
        uart_.end();
        uart_.setRxBufferSize(4096);
        // Without a TX ring buffer writes block on the 128-byte FIFO, stalling
        // this high-priority task for ~16 ms on every stream request burst
        uart_.setTxBufferSize(1024);
        uart_.begin(baud, SERIAL_8N1, swapped ? cfg::MAV_TX_PIN : cfg::MAV_RX_PIN,
                    swapped ? cfg::MAV_RX_PIN : cfg::MAV_TX_PIN);
        baud_ = baud;
        lastValidPacketMs_ = millis();
        gStore.update([&](SharedStatus& s) {
            s.baud = baud;
            s.pinsSwapped = swapped;
        });
        Serial.printf("[mav] UART2 %lu baud, RX=GPIO%d TX=GPIO%d\n", static_cast<unsigned long>(baud),
                      swapped ? cfg::MAV_TX_PIN : cfg::MAV_RX_PIN,
                      swapped ? cfg::MAV_RX_PIN : cfg::MAV_TX_PIN);
    }

    static size_t loadProfile() {
        Preferences prefs;
        prefs.begin("mavlink", false);  // read-write: creates the namespace on first boot
        const size_t profile = prefs.getUInt("profile", 0);
        prefs.end();
        return profile < PROFILE_COUNT ? profile : 0;
    }

    void saveProfile() {
        Preferences prefs;
        prefs.begin("mavlink", false);
        if (prefs.getUInt("profile", PROFILE_COUNT) != profile_) prefs.putUInt("profile", profile_);
        prefs.end();
    }

    void readUart(uint32_t now) {
        uint32_t bytes = 0;
        uint32_t packets = 0;
        mavlink_message_t msg;
        mavlink_status_t status;

        while (uart_.available() > 0) {
            const uint8_t c = static_cast<uint8_t>(uart_.read());
            bytes++;
            lastByteMs_ = now;
            if (!mavlink_parse_char(MAVLINK_COMM_0, c, &msg, &status)) continue;
            packets++;
            lastValidPacketMs_ = now;
            handleMessage(msg, now);
        }

        if (bytes == 0) return;
        const uint32_t errors = mavlink_get_channel_status(MAVLINK_COMM_0)->packet_rx_drop_count;
        gStore.update([&](SharedStatus& s) {
            s.rxBytes += bytes;
            s.rxPackets += packets;
            s.rxParseErrors = errors;
        });
    }

    void handleMessage(const mavlink_message_t& msg, uint32_t now) {
        bool accepted = false;
        bool heartbeat = false;
        uint32_t textSeq = 0;
        TelemetryState copy;

        gStore.update([&](SharedStatus& s) {
            const uint32_t prevSeq = s.telemetry.statusTextSeq;
            accepted = applyMavlinkMessage(s.telemetry, msg, now);
            heartbeat = accepted && msg.msgid == MAVLINK_MSG_ID_HEARTBEAT;
            if (s.telemetry.statusTextSeq != prevSeq) {
                textSeq = s.telemetry.statusTextSeq;
                copy = s.telemetry;
            }
            if (heartbeat) {
                targetSys_ = s.telemetry.sysId;
                targetComp_ = s.telemetry.compId;
            }
        });
        if (!accepted) return;

        if (msg.msgid == MAVLINK_MSG_ID_GLOBAL_POSITION_INT) lastPositionMs_ = now;
        if (msg.msgid == MAVLINK_MSG_ID_SYS_STATUS) lastSysStatusMs_ = now;
        if (textSeq) postEvent("FC_TEXT", "sev=%u %s", copy.statusSeverity, copy.statusText);
        if (heartbeat) handleLinkEvent(monitor_.onHeartbeat(now), now);
    }

    void handleLinkEvent(LinkEvent ev, uint32_t now) {
        if (ev == LinkEvent::None) return;

        const LinkState state = monitor_.state();
        const uint32_t lost = monitor_.lostCount();
        gStore.update([&](SharedStatus& s) {
            s.link = state;
            s.lostCount = lost;
        });

        switch (ev) {
            case LinkEvent::Connected:
                postEvent("LINK_UP", "sys=%u comp=%u baud=%lu pins=%s", targetSys_, targetComp_,
                          static_cast<unsigned long>(baud_),
                          profileSwapped(profile_) ? "swapped" : "straight");
                profileLocked_ = true;
                saveProfile();
                requestStreams(now);
                break;
            case LinkEvent::Restored:
                postEvent("LINK_RESTORED", "after %lu ms, lost #%lu",
                          static_cast<unsigned long>(now - lostAtMs_),
                          static_cast<unsigned long>(lost));
                requestStreams(now);
                break;
            case LinkEvent::Lost:
                lostAtMs_ = now;
                // The FC may come back with another system ID (SYSID_THISMAV change or
                // a different board): accept the next autopilot heartbeat whatever it is
                gStore.update([](SharedStatus& s) { releaseAutopilotLock(s.telemetry); });
                postEvent("LINK_LOST", "no heartbeat for %lu ms",
                          static_cast<unsigned long>(cfg::LINK_TIMEOUT_MS));
                break;
            case LinkEvent::None:
                break;
        }
    }

    void sendHeartbeatIfDue(uint32_t now) {
        if (now - lastHeartbeatTxMs_ < cfg::HEARTBEAT_PERIOD_MS) return;
        lastHeartbeatTxMs_ = now;
        mavlink_message_t msg;
        mavlink_msg_heartbeat_pack_chan(cfg::COMPANION_SYS_ID, cfg::COMPANION_COMP_ID,
                                        MAVLINK_COMM_0, &msg, MAV_TYPE_ONBOARD_CONTROLLER,
                                        MAV_AUTOPILOT_INVALID, 0, 0, MAV_STATE_ACTIVE);
        send(msg);
    }

    void requestStreams(uint32_t now) {
        lastStreamRequestMs_ = now;
        for (const StreamRequest& r : STREAMS) {
            mavlink_message_t msg;
            mavlink_msg_command_long_pack_chan(
                cfg::COMPANION_SYS_ID, cfg::COMPANION_COMP_ID, MAVLINK_COMM_0, &msg, targetSys_,
                targetComp_, MAV_CMD_SET_MESSAGE_INTERVAL, 0, static_cast<float>(r.msgId),
                static_cast<float>(r.intervalUs), 0, 0, 0, 0, 0);
            send(msg);
        }
        gStore.update([](SharedStatus& s) { s.streamRequests++; });
    }

    // The FC drops message intervals on reboot; request them again if data stops
    // while heartbeats still arrive.
    void retryStreamsIfStale(uint32_t now) {
        if (monitor_.state() != LinkState::Connected) return;
        if (now - lastStreamRequestMs_ < cfg::STREAM_REQUEST_RETRY_MS) return;
        const bool stale = now - lastPositionMs_ > cfg::STREAM_STALE_MS ||
                           now - lastSysStatusMs_ > cfg::STREAM_STALE_MS;
        if (stale) requestStreams(now);
    }

    // No valid packet for MAV_PROBE_MS: switch to the next baud/pin combination.
    // Once a combination has worked, complete silence means the FC is off or
    // unplugged, so it is kept; only garbage (wrong baud) makes us search again.
    void recoverSilentUart(uint32_t now) {
        if (now - lastValidPacketMs_ < cfg::MAV_PROBE_MS) return;
        const bool silent = now - lastByteMs_ > 1000;
        if (profileLocked_ && silent) {
            lastValidPacketMs_ = now;  // re-arm the check
            return;
        }
        profileLocked_ = false;
        const size_t next = (profile_ + 1) % PROFILE_COUNT;
        postEvent("UART_PROBE", "%s for %lu ms, trying %lu baud %s pins",
                  silent ? "silence" : "garbage",
                  static_cast<unsigned long>(now - lastValidPacketMs_),
                  static_cast<unsigned long>(profileBaud(next)),
                  profileSwapped(next) ? "swapped" : "straight");
        openUart(next);
    }

    void send(const mavlink_message_t& msg) {
        uint8_t buf[MAVLINK_MAX_PACKET_LEN];
        const uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
        uart_.write(buf, len);
    }

    HardwareSerial& uart_;
    LinkMonitor monitor_;
    uint32_t baud_ = 0;
    size_t profile_ = 0;
    bool profileLocked_ = false;
    uint8_t targetSys_ = 1;
    uint8_t targetComp_ = MAV_COMP_ID_AUTOPILOT1;
    uint32_t lastValidPacketMs_ = 0;
    uint32_t lastByteMs_ = 0;
    uint32_t lastHeartbeatTxMs_ = 0;
    uint32_t lastStreamRequestMs_ = 0;
    uint32_t lastPositionMs_ = 0;
    uint32_t lastSysStatusMs_ = 0;
    uint32_t lostAtMs_ = 0;
};

void mavlinkTask(void* arg) {
    static MavlinkLink link(*static_cast<HardwareSerial*>(arg));
    link.run();
}

}  // namespace

TaskHandle_t startMavlinkTask(HardwareSerial& uart) {
    // Core 1, above the Arduino loop (web server) so UART is drained promptly
    TaskHandle_t handle = nullptr;
    xTaskCreatePinnedToCore(mavlinkTask, "mavlink", 8192, &uart, 3, &handle, 1);
    configASSERT(handle);
    return handle;
}
