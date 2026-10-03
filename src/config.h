#pragma once

#include <stdint.h>

// Optional home Wi-Fi credentials (gitignored). Define WIFI_STA_SSID and
// WIFI_STA_PASSWORD there to join an existing network in addition to the AP.
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif

namespace cfg {

// UART2 to the flight controller (FC SERIAL6 / UART6)
constexpr int MAV_RX_PIN = 16;  // <- FC T6
constexpr int MAV_TX_PIN = 17;  // -> FC R6
// Auto-detection: until the first heartbeat, every baud rate is tried with
// straight and swapped RX/TX pins. The working combination is saved in NVS.
constexpr uint32_t MAV_BAUDS[] = {115200, 57600, 921600, 460800, 230400};
constexpr uint32_t MAV_PROBE_MS = 4000;  // no valid packet for this long -> next combination

// Our identity on the MAVLink bus: same system as the FC, onboard computer component
constexpr uint8_t COMPANION_SYS_ID = 1;
constexpr uint8_t COMPANION_COMP_ID = 191;  // MAV_COMP_ID_ONBOARD_COMPUTER

constexpr uint32_t HEARTBEAT_PERIOD_MS = 1000;
constexpr uint32_t LINK_TIMEOUT_MS = 3000;
// Re-request message intervals when a requested message stops arriving
constexpr uint32_t STREAM_STALE_MS = 4000;
constexpr uint32_t STREAM_REQUEST_RETRY_MS = 5000;

// CSV logging
constexpr uint32_t LOG_PERIOD_MS = 500;              // 2 rows per second
constexpr uint32_t LOG_FLUSH_PERIOD_MS = 1000;
constexpr uint32_t LOG_MAX_FILE_BYTES = 512 * 1024;  // start a new part after this
constexpr uint32_t FS_MIN_FREE_BYTES = 96 * 1024;    // delete oldest logs below this
constexpr uint32_t EVENTS_MAX_FILE_BYTES = 64 * 1024;
constexpr const char* EVENTS_FILE = "/events.csv";
constexpr const char* EVENTS_OLD_FILE = "/events_old.csv";

// Wi-Fi access point (always on)
constexpr const char* AP_SSID = "ESP32-Telemetry";
constexpr const char* AP_PASSWORD = "telemetry123";
constexpr const char* MDNS_HOST = "telemetry";

// Serial console status line
constexpr uint32_t CONSOLE_STATUS_PERIOD_MS = 2000;

}  // namespace cfg
