#include "web_ui.h"

#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>

#include "config.h"
#include "csv_format.h"
#include "storage.h"
#include "telemetry_decoder.h"
#include "telemetry_store.h"
#include "web_page.h"

namespace {

WebServer server(80);

// Appends `"key":value,` with null for unknown values.
void jsonNum(String& out, const char* key, bool known, double value, int decimals) {
    out += '"';
    out += key;
    out += "\":";
    if (known && isfinite(value)) {
        out += String(value, decimals);
    } else {
        out += "null";
    }
    out += ',';
}

void jsonStr(String& out, const char* key, const char* value) {
    out += '"';
    out += key;
    out += "\":\"";
    for (const char* p = value; *p; ++p) {
        const char c = *p;
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<uint8_t>(c) >= 0x20) {
            out += c;
        }
    }
    out += "\",";
}

void closeObject(String& out) {
    if (out.endsWith(",")) out.remove(out.length() - 1);
    out += '}';
}

void handleStatus() {
    const SharedStatus st = gStore.snapshot();
    const TelemetryState& t = st.telemetry;
    const uint32_t now = millis();

    String j;
    j.reserve(1024);
    j += '{';
    jsonStr(j, "link", linkStateName(st.link));
    jsonNum(j, "lost_count", true, st.lostCount, 0);
    jsonNum(j, "uptime_ms", true, now, 0);
    jsonNum(j, "boot", true, bootCount(), 0);
    jsonNum(j, "baud", true, st.baud, 0);
    j += st.pinsSwapped ? "\"swapped\":true," : "\"swapped\":false,";
    jsonNum(j, "rx_bytes", true, st.rxBytes, 0);
    jsonNum(j, "rx_packets", true, st.rxPackets, 0);
    jsonNum(j, "rx_errors", true, st.rxParseErrors, 0);
    jsonNum(j, "rows", true, loggedRows(), 0);
    jsonNum(j, "fs_used", true, LittleFS.usedBytes(), 0);
    jsonNum(j, "fs_total", true, LittleFS.totalBytes(), 0);
    // Empty until the first row is logged (no FC link yet)
    const String active = activeLogFile();
    jsonStr(j, "active_file", active.isEmpty() ? "" : active.c_str() + 1);

    char utc[32];
    formatUtc(utc, sizeof utc, t, now);

    j += "\"t\":{";
    const bool hb = t.hasHeartbeat;
    if (hb) {
        jsonStr(j, "mode", flightModeName(t.vehicleType, t.customMode));
        j += t.armed ? "\"armed\":true," : "\"armed\":false,";
    } else {
        j += "\"mode\":null,\"armed\":null,";
    }
    jsonNum(j, "voltage", t.hasBattery, t.voltageV, 2);
    jsonNum(j, "current", t.hasBattery && t.hasCurrent, t.currentA, 2);
    jsonNum(j, "battery_pct", t.hasBattery && t.batteryPct >= 0, t.batteryPct, 0);
    jsonNum(j, "consumed_mah", t.consumedMah >= 0, t.consumedMah, 0);
    jsonNum(j, "lat", t.hasPosition && t.hasLatLon, t.latE7 / 1e7, 7);
    jsonNum(j, "lon", t.hasPosition && t.hasLatLon, t.lonE7 / 1e7, 7);
    jsonNum(j, "alt_msl", t.hasPosition, t.altMslM, 2);
    jsonNum(j, "alt_rel", t.hasPosition, t.altRelM, 2);
    jsonNum(j, "gps_fix", t.hasGps, t.gpsFix, 0);
    jsonNum(j, "sats", t.hasGps && t.satellites != UINT8_MAX, t.satellites, 0);
    jsonNum(j, "hdop", t.hasGps && t.hdop >= 0, t.hdop, 2);
    jsonNum(j, "groundspeed", t.hasVfr, t.groundspeed, 2);
    jsonNum(j, "airspeed", t.hasVfr, t.airspeed, 2);
    jsonNum(j, "climb", t.hasVfr, t.climb, 2);
    jsonNum(j, "heading", t.hasVfr, t.headingDeg, 0);
    jsonNum(j, "throttle", t.hasVfr, t.throttlePct, 0);
    jsonNum(j, "roll", t.hasAttitude, t.rollDeg, 1);
    jsonNum(j, "pitch", t.hasAttitude, t.pitchDeg, 1);
    jsonNum(j, "yaw", t.hasAttitude, t.yawDeg, 1);
    jsonStr(j, "utc", utc);
    jsonStr(j, "status_text", t.statusText);
    closeObject(j);
    closeObject(j);

    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", j);
}

void handleFiles() {
    const String active = activeLogFile();
    String j = "{\"files\":[";
    File root = LittleFS.open("/");
    bool first = true;
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        String path = f.name();
        if (!path.startsWith("/")) path = "/" + path;
        if (!isServableFile(path)) continue;
        if (!first) j += ',';
        first = false;
        j += "{\"name\":\"" + path.substring(1) + "\",\"size\":" + String(f.size()) +
             ",\"active\":" + (path == active ? "true" : "false") + "}";
    }
    j += "]}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", j);
}

// Resolves the ?file= argument to an absolute path; replies with an error and
// returns an empty string when the name is invalid or missing.
String requestedFile() {
    String path = "/" + server.arg("file");
    if (!isServableFile(path)) {
        server.send(400, "text/plain", "invalid file name");
        return String();
    }
    if (!LittleFS.exists(path)) {
        server.send(404, "text/plain", "file not found");
        return String();
    }
    return path;
}

void sendFile(bool asAttachment) {
    const String path = requestedFile();
    if (path.isEmpty()) return;
    File f = LittleFS.open(path, FILE_READ);
    if (!f) {
        server.send(500, "text/plain", "cannot open file");
        return;
    }
    if (asAttachment) {
        server.sendHeader("Content-Disposition", "attachment; filename=\"" + path.substring(1) + "\"");
    }
    server.sendHeader("Cache-Control", "no-store");
    server.streamFile(f, asAttachment ? "text/csv" : "text/plain; charset=utf-8");
    f.close();
}

void handleDelete() {
    const String path = requestedFile();
    if (path.isEmpty()) return;
    if (path == activeLogFile()) {
        server.send(409, "text/plain", "file is being written");
        return;
    }
    if (!LittleFS.remove(path)) {
        server.send(500, "text/plain", "delete failed");
        return;
    }
    postEvent("FILE_DELETED", "%s", path.c_str());
    server.send(200, "text/plain", "deleted");
}

void startWifi() {
    WiFi.persistent(false);
#ifdef WIFI_STA_SSID
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);
    Serial.printf("[wifi] joining \"%s\" in background\n", WIFI_STA_SSID);
#else
    WiFi.mode(WIFI_AP);
#endif
    WiFi.softAP(cfg::AP_SSID, cfg::AP_PASSWORD);
    Serial.printf("[wifi] AP \"%s\" (password \"%s\") -> http://%s/\n", cfg::AP_SSID,
                  cfg::AP_PASSWORD, WiFi.softAPIP().toString().c_str());

    if (MDNS.begin(cfg::MDNS_HOST)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[wifi] mDNS http://%s.local/\n", cfg::MDNS_HOST);
    }
}

}  // namespace

void webBegin() {
    startWifi();

    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", WEB_PAGE); });
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/files", HTTP_GET, handleFiles);
    server.on("/download", HTTP_GET, [] { sendFile(true); });
    server.on("/view", HTTP_GET, [] { sendFile(false); });
    server.on("/delete", HTTP_POST, handleDelete);
    server.onNotFound([] { server.send(404, "text/plain", "not found"); });
    server.begin();
}

void webLoop() {
    server.handleClient();

#ifdef WIFI_STA_SSID
    static bool reported = false;
    if (!reported && WiFi.status() == WL_CONNECTED) {
        reported = true;
        Serial.printf("[wifi] joined \"%s\" -> http://%s/\n", WIFI_STA_SSID,
                      WiFi.localIP().toString().c_str());
    } else if (reported && WiFi.status() != WL_CONNECTED) {
        reported = false;
    }
#endif
}
