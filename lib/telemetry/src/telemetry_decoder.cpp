#include "telemetry_decoder.h"

#include <string.h>

namespace {

constexpr float RAD_TO_DEG_F = 57.29577951f;

struct ModeEntry {
    uint32_t number;
    const char* name;
};

const ModeEntry COPTER_MODES[] = {
    {0, "STABILIZE"},  {1, "ACRO"},       {2, "ALT_HOLD"},    {3, "AUTO"},
    {4, "GUIDED"},     {5, "LOITER"},     {6, "RTL"},         {7, "CIRCLE"},
    {9, "LAND"},       {11, "DRIFT"},     {13, "SPORT"},      {14, "FLIP"},
    {15, "AUTOTUNE"},  {16, "POSHOLD"},   {17, "BRAKE"},      {18, "THROW"},
    {19, "AVOID_ADSB"}, {20, "GUIDED_NOGPS"}, {21, "SMART_RTL"}, {22, "FLOWHOLD"},
    {23, "FOLLOW"},    {24, "ZIGZAG"},    {25, "SYSTEMID"},   {26, "AUTOROTATE"},
    {27, "AUTO_RTL"},  {28, "TURTLE"},
};

const ModeEntry PLANE_MODES[] = {
    {0, "MANUAL"},     {1, "CIRCLE"},     {2, "STABILIZE"},   {3, "TRAINING"},
    {4, "ACRO"},       {5, "FBWA"},       {6, "FBWB"},        {7, "CRUISE"},
    {8, "AUTOTUNE"},   {10, "AUTO"},      {11, "RTL"},        {12, "LOITER"},
    {13, "TAKEOFF"},   {14, "AVOID_ADSB"}, {15, "GUIDED"},    {17, "QSTABILIZE"},
    {18, "QHOVER"},    {19, "QLOITER"},   {20, "QLAND"},      {21, "QRTL"},
    {22, "QAUTOTUNE"}, {23, "QACRO"},     {24, "THERMAL"},    {25, "LOITER_ALT_QLAND"},
    {26, "AUTOLAND"},
};

template <size_t N>
const char* lookup(const ModeEntry (&table)[N], uint32_t mode) {
    for (const ModeEntry& e : table) {
        if (e.number == mode) return e.name;
    }
    return "UNKNOWN";
}

bool isCopterType(uint8_t type) {
    switch (type) {
        case MAV_TYPE_QUADROTOR:
        case MAV_TYPE_HEXAROTOR:
        case MAV_TYPE_OCTOROTOR:
        case MAV_TYPE_TRICOPTER:
        case MAV_TYPE_COAXIAL:
        case MAV_TYPE_HELICOPTER:
        case MAV_TYPE_DODECAROTOR:
        case MAV_TYPE_DECAROTOR:
            return true;
        default:
            return false;
    }
}

bool isPlaneType(uint8_t type) {
    switch (type) {
        case MAV_TYPE_FIXED_WING:
        case MAV_TYPE_VTOL_TAILSITTER_DUOROTOR:
        case MAV_TYPE_VTOL_TAILSITTER_QUADROTOR:
        case MAV_TYPE_VTOL_TILTROTOR:
        case MAV_TYPE_VTOL_FIXEDROTOR:
        case MAV_TYPE_VTOL_TAILSITTER:
        case MAV_TYPE_VTOL_TILTWING:
            return true;
        default:
            return false;
    }
}

void applyHeartbeat(TelemetryState& s, const mavlink_message_t& msg, uint32_t nowMs) {
    mavlink_heartbeat_t hb;
    mavlink_msg_heartbeat_decode(&msg, &hb);
    s.hasHeartbeat = true;
    s.sysId = msg.sysid;
    s.compId = msg.compid;
    s.vehicleType = hb.type;
    s.autopilot = hb.autopilot;
    s.systemStatus = hb.system_status;
    s.armed = (hb.base_mode & MAV_MODE_FLAG_SAFETY_ARMED) != 0;
    s.customMode = hb.custom_mode;
    s.lastHeartbeatMs = nowMs;
}

void applySysStatus(TelemetryState& s, const mavlink_message_t& msg) {
    mavlink_sys_status_t st;
    mavlink_msg_sys_status_decode(&msg, &st);
    if (st.voltage_battery == UINT16_MAX) return;
    s.hasBattery = true;
    s.voltageV = st.voltage_battery / 1000.0f;
    s.hasCurrent = st.current_battery >= 0;
    s.currentA = s.hasCurrent ? st.current_battery / 100.0f : 0;
    s.batteryPct = st.battery_remaining;
}

void applyBatteryStatus(TelemetryState& s, const mavlink_message_t& msg) {
    mavlink_battery_status_t bs;
    mavlink_msg_battery_status_decode(&msg, &bs);
    if (bs.id != 0) return;  // primary battery only
    s.consumedMah = bs.current_consumed;
}

void applyGlobalPosition(TelemetryState& s, const mavlink_message_t& msg) {
    mavlink_global_position_int_t gp;
    mavlink_msg_global_position_int_decode(&msg, &gp);
    s.hasPosition = true;
    s.hasLatLon = gp.lat != 0 || gp.lon != 0;
    s.latE7 = gp.lat;
    s.lonE7 = gp.lon;
    s.altMslM = gp.alt / 1000.0f;
    s.altRelM = gp.relative_alt / 1000.0f;
}

void applyGpsRaw(TelemetryState& s, const mavlink_message_t& msg) {
    mavlink_gps_raw_int_t g;
    mavlink_msg_gps_raw_int_decode(&msg, &g);
    s.hasGps = true;
    s.gpsFix = g.fix_type;
    s.satellites = g.satellites_visible;
    s.hdop = g.eph == UINT16_MAX ? -1.0f : g.eph / 100.0f;
}

void applyVfrHud(TelemetryState& s, const mavlink_message_t& msg) {
    mavlink_vfr_hud_t v;
    mavlink_msg_vfr_hud_decode(&msg, &v);
    s.hasVfr = true;
    s.groundspeed = v.groundspeed;
    s.airspeed = v.airspeed;
    s.climb = v.climb;
    s.headingDeg = v.heading;
    s.throttlePct = v.throttle;
}

void applyAttitude(TelemetryState& s, const mavlink_message_t& msg) {
    mavlink_attitude_t a;
    mavlink_msg_attitude_decode(&msg, &a);
    s.hasAttitude = true;
    s.rollDeg = a.roll * RAD_TO_DEG_F;
    s.pitchDeg = a.pitch * RAD_TO_DEG_F;
    s.yawDeg = a.yaw * RAD_TO_DEG_F;
}

void applySystemTime(TelemetryState& s, const mavlink_message_t& msg, uint32_t nowMs) {
    mavlink_system_time_t t;
    mavlink_msg_system_time_decode(&msg, &t);
    if (t.time_unix_usec == 0) return;  // no GPS time yet
    s.unixTimeUs = t.time_unix_usec;
    s.unixTimeRxMs = nowMs;
}

void applyStatusText(TelemetryState& s, const mavlink_message_t& msg) {
    mavlink_statustext_t st;
    mavlink_msg_statustext_decode(&msg, &st);
    // The text field is not null-terminated when it uses all 50 chars
    memcpy(s.statusText, st.text, sizeof st.text);
    s.statusText[sizeof st.text] = 0;
    s.statusSeverity = st.severity;
    s.statusTextSeq++;
}

}  // namespace

bool isAutopilotHeartbeat(const mavlink_message_t& msg) {
    if (msg.msgid != MAVLINK_MSG_ID_HEARTBEAT) return false;
    const uint8_t autopilot = mavlink_msg_heartbeat_get_autopilot(&msg);
    const uint8_t type = mavlink_msg_heartbeat_get_type(&msg);
    return autopilot != MAV_AUTOPILOT_INVALID && type != MAV_TYPE_GCS &&
           type != MAV_TYPE_ONBOARD_CONTROLLER;
}

void releaseAutopilotLock(TelemetryState& s) { s.hasHeartbeat = false; }

bool applyMavlinkMessage(TelemetryState& s, const mavlink_message_t& msg, uint32_t nowMs) {
    if (!s.hasHeartbeat) {
        // Lock onto the first autopilot heartbeat; ignore everything before it
        if (!isAutopilotHeartbeat(msg)) return false;
    } else if (msg.sysid != s.sysId) {
        return false;
    }

    switch (msg.msgid) {
        case MAVLINK_MSG_ID_HEARTBEAT:
            // Other components of the same system also send heartbeats
            if (!isAutopilotHeartbeat(msg)) return false;
            applyHeartbeat(s, msg, nowMs);
            break;
        case MAVLINK_MSG_ID_SYS_STATUS: applySysStatus(s, msg); break;
        case MAVLINK_MSG_ID_BATTERY_STATUS: applyBatteryStatus(s, msg); break;
        case MAVLINK_MSG_ID_GLOBAL_POSITION_INT: applyGlobalPosition(s, msg); break;
        case MAVLINK_MSG_ID_GPS_RAW_INT: applyGpsRaw(s, msg); break;
        case MAVLINK_MSG_ID_VFR_HUD: applyVfrHud(s, msg); break;
        case MAVLINK_MSG_ID_ATTITUDE: applyAttitude(s, msg); break;
        case MAVLINK_MSG_ID_SYSTEM_TIME: applySystemTime(s, msg, nowMs); break;
        case MAVLINK_MSG_ID_STATUSTEXT: applyStatusText(s, msg); break;
        default: break;
    }
    s.msgCount++;
    return true;
}

const char* flightModeName(uint8_t vehicleType, uint32_t customMode) {
    if (isCopterType(vehicleType)) return lookup(COPTER_MODES, customMode);
    if (isPlaneType(vehicleType)) return lookup(PLANE_MODES, customMode);
    return "UNKNOWN";
}
