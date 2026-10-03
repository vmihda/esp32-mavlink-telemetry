#pragma once

#include <stdint.h>

// Snapshot of the vehicle telemetry decoded from MAVLink.
// Each group has a validity flag: fields of a group are meaningful only after
// the corresponding message has been received at least once.
struct TelemetryState {
    // Autopilot identity (locked on the first autopilot HEARTBEAT)
    bool hasHeartbeat = false;
    uint8_t sysId = 0;
    uint8_t compId = 0;
    uint8_t vehicleType = 0;   // MAV_TYPE
    uint8_t autopilot = 0;     // MAV_AUTOPILOT
    uint8_t systemStatus = 0;  // MAV_STATE
    bool armed = false;
    uint32_t customMode = 0;   // ArduPilot flight mode number
    uint32_t lastHeartbeatMs = 0;

    // SYS_STATUS / BATTERY_STATUS
    bool hasBattery = false;
    float voltageV = 0;
    bool hasCurrent = false;
    float currentA = 0;
    int8_t batteryPct = -1;    // -1 = unknown
    int32_t consumedMah = -1;  // -1 = unknown

    // GLOBAL_POSITION_INT
    bool hasPosition = false;
    bool hasLatLon = false;    // ArduPilot sends 0/0 until it has a position estimate
    int32_t latE7 = 0;
    int32_t lonE7 = 0;
    float altMslM = 0;
    float altRelM = 0;

    // GPS_RAW_INT
    bool hasGps = false;
    uint8_t gpsFix = 0;        // GPS_FIX_TYPE
    uint8_t satellites = 0;    // 255 = unknown
    float hdop = -1;           // -1 = unknown

    // VFR_HUD
    bool hasVfr = false;
    float groundspeed = 0;
    float airspeed = 0;
    float climb = 0;
    int16_t headingDeg = 0;
    uint16_t throttlePct = 0;

    // ATTITUDE
    bool hasAttitude = false;
    float rollDeg = 0;
    float pitchDeg = 0;
    float yawDeg = 0;

    // SYSTEM_TIME (UTC from GPS); unixTimeUs = 0 means unknown
    uint64_t unixTimeUs = 0;
    uint32_t unixTimeRxMs = 0;

    // Last STATUSTEXT from the autopilot
    uint32_t statusTextSeq = 0;
    uint8_t statusSeverity = 0;
    char statusText[51] = {0};

    // Total MAVLink messages accepted from the autopilot
    uint32_t msgCount = 0;
};
