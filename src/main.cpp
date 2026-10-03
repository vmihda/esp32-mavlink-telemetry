// ESP32 companion computer for an ArduPilot flight controller.
//
// MAVLink task (UART2) -> TelemetryStore -> CSV logger task (LittleFS)
//                                        -> web UI (Wi-Fi, loop())
//                                        -> serial console status line

#include <Arduino.h>

#include "config.h"
#include "mavlink_link.h"
#include "storage.h"
#include "telemetry_decoder.h"
#include "telemetry_store.h"
#include "web_ui.h"

namespace {

TaskHandle_t gMavlinkTask = nullptr;
TaskHandle_t gLoggerTask = nullptr;

void printConsoleStatus() {
    const SharedStatus st = gStore.snapshot();
    const TelemetryState& t = st.telemetry;
    Serial.printf("[status] link=%s lost=%lu baud=%lu%s pkts=%lu err=%lu rows=%lu",
                  linkStateName(st.link), static_cast<unsigned long>(st.lostCount),
                  static_cast<unsigned long>(st.baud), st.pinsSwapped ? " (swapped)" : "",
                  static_cast<unsigned long>(st.rxPackets),
                  static_cast<unsigned long>(st.rxParseErrors),
                  static_cast<unsigned long>(loggedRows()));
    if (t.hasHeartbeat) {
        Serial.printf(" | %s %s", flightModeName(t.vehicleType, t.customMode),
                      t.armed ? "ARMED" : "disarmed");
    }
    if (t.hasBattery) Serial.printf(" | %.2fV %d%%", t.voltageV, t.batteryPct);
    if (t.hasPosition) Serial.printf(" | alt %.1fm", t.altRelM);
    if (t.hasGps) Serial.printf(" | gps fix=%u sats=%u", t.gpsFix, t.satellites);
    if (t.hasAttitude) Serial.printf(" | r%.0f p%.0f y%.0f", t.rollDeg, t.pitchDeg, t.yawDeg);
    // Minimum free stack ever seen, bytes (ESP-IDF stacks are byte-sized)
    Serial.printf(" | stack mav=%u log=%u loop=%u heap=%u",
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(gMavlinkTask)),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(gLoggerTask)),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                  static_cast<unsigned>(ESP.getMinFreeHeap()));
    Serial.println();
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[boot] ESP32 MAVLink telemetry companion");

    storageBegin();
    gLoggerTask = startLoggerTask();
    gMavlinkTask = startMavlinkTask(Serial2);
    webBegin();
}

void loop() {
    webLoop();

    static uint32_t lastStatusMs = 0;
    if (millis() - lastStatusMs >= cfg::CONSOLE_STATUS_PERIOD_MS) {
        lastStatusMs = millis();
        printConsoleStatus();
    }
    delay(2);
}
