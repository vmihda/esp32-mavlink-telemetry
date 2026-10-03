#include <string.h>
#include <unity.h>

#include "telemetry_decoder.h"

static const uint8_t FC_SYS = 1;
static const uint8_t FC_COMP = MAV_COMP_ID_AUTOPILOT1;

static mavlink_message_t heartbeat(uint8_t sys, uint8_t comp, uint8_t type, uint8_t autopilot,
                                   uint8_t baseMode, uint32_t customMode) {
    mavlink_heartbeat_t hb{};
    hb.type = type;
    hb.autopilot = autopilot;
    hb.base_mode = baseMode;
    hb.custom_mode = customMode;
    hb.system_status = MAV_STATE_STANDBY;
    mavlink_message_t msg;
    mavlink_msg_heartbeat_encode(sys, comp, &msg, &hb);
    return msg;
}

static mavlink_message_t copterHeartbeat(bool armed, uint32_t mode) {
    uint8_t base = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED | (armed ? MAV_MODE_FLAG_SAFETY_ARMED : 0);
    return heartbeat(FC_SYS, FC_COMP, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_ARDUPILOTMEGA, base, mode);
}

static TelemetryState lockedState() {
    TelemetryState s;
    applyMavlinkMessage(s, copterHeartbeat(false, 0), 100);
    return s;
}

void setUp() {}
void tearDown() {}

void test_autopilot_heartbeat_sets_identity_mode_and_armed() {
    TelemetryState s;
    TEST_ASSERT_TRUE(applyMavlinkMessage(s, copterHeartbeat(true, 5), 1234));

    TEST_ASSERT_TRUE(s.hasHeartbeat);
    TEST_ASSERT_EQUAL_UINT8(FC_SYS, s.sysId);
    TEST_ASSERT_EQUAL_UINT8(FC_COMP, s.compId);
    TEST_ASSERT_EQUAL_UINT8(MAV_TYPE_QUADROTOR, s.vehicleType);
    TEST_ASSERT_TRUE(s.armed);
    TEST_ASSERT_EQUAL_UINT32(5, s.customMode);
    TEST_ASSERT_EQUAL_UINT32(1234, s.lastHeartbeatMs);
    TEST_ASSERT_EQUAL_STRING("LOITER", flightModeName(s.vehicleType, s.customMode));
}

void test_gcs_heartbeat_is_ignored() {
    TelemetryState s;
    mavlink_message_t gcs = heartbeat(255, 190, MAV_TYPE_GCS, MAV_AUTOPILOT_INVALID, 0, 0);

    TEST_ASSERT_FALSE(isAutopilotHeartbeat(gcs));
    TEST_ASSERT_FALSE(applyMavlinkMessage(s, gcs, 10));
    TEST_ASSERT_FALSE(s.hasHeartbeat);
}

void test_messages_before_autopilot_heartbeat_are_ignored() {
    TelemetryState s;
    mavlink_sys_status_t st{};
    st.voltage_battery = 12000;
    mavlink_message_t msg;
    mavlink_msg_sys_status_encode(FC_SYS, FC_COMP, &msg, &st);

    TEST_ASSERT_FALSE(applyMavlinkMessage(s, msg, 10));
    TEST_ASSERT_FALSE(s.hasBattery);
}

void test_messages_from_other_system_are_ignored_after_lock() {
    TelemetryState s = lockedState();
    mavlink_message_t other = copterHeartbeat(true, 6);
    mavlink_heartbeat_t hb{};
    hb.type = MAV_TYPE_QUADROTOR;
    hb.autopilot = MAV_AUTOPILOT_ARDUPILOTMEGA;
    hb.base_mode = MAV_MODE_FLAG_SAFETY_ARMED;
    hb.custom_mode = 6;
    mavlink_msg_heartbeat_encode(2, FC_COMP, &other, &hb);

    TEST_ASSERT_FALSE(applyMavlinkMessage(s, other, 200));
    TEST_ASSERT_FALSE(s.armed);
    TEST_ASSERT_EQUAL_UINT32(0, s.customMode);
}

void test_released_lock_accepts_new_autopilot_system() {
    TelemetryState s = lockedState();
    releaseAutopilotLock(s);
    TEST_ASSERT_FALSE(s.hasHeartbeat);

    mavlink_message_t hb = heartbeat(2, FC_COMP, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_ARDUPILOTMEGA,
                                     MAV_MODE_FLAG_SAFETY_ARMED, 6);
    TEST_ASSERT_TRUE(applyMavlinkMessage(s, hb, 300));
    TEST_ASSERT_EQUAL_UINT8(2, s.sysId);
    TEST_ASSERT_TRUE(s.armed);
}

void test_sys_status_battery() {
    TelemetryState s = lockedState();
    mavlink_sys_status_t st{};
    st.voltage_battery = 12600;  // mV
    st.current_battery = 150;    // cA
    st.battery_remaining = 87;
    mavlink_message_t msg;
    mavlink_msg_sys_status_encode(FC_SYS, FC_COMP, &msg, &st);

    TEST_ASSERT_TRUE(applyMavlinkMessage(s, msg, 200));
    TEST_ASSERT_TRUE(s.hasBattery);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.6f, s.voltageV);
    TEST_ASSERT_TRUE(s.hasCurrent);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, s.currentA);
    TEST_ASSERT_EQUAL_INT8(87, s.batteryPct);
}

void test_sys_status_unknown_current_and_remaining() {
    TelemetryState s = lockedState();
    mavlink_sys_status_t st{};
    st.voltage_battery = 4100;
    st.current_battery = -1;
    st.battery_remaining = -1;
    mavlink_message_t msg;
    mavlink_msg_sys_status_encode(FC_SYS, FC_COMP, &msg, &st);

    applyMavlinkMessage(s, msg, 200);
    TEST_ASSERT_FALSE(s.hasCurrent);
    TEST_ASSERT_EQUAL_INT8(-1, s.batteryPct);
}

void test_battery_status_consumed_mah() {
    TelemetryState s = lockedState();
    mavlink_battery_status_t bs{};
    bs.id = 0;
    bs.current_consumed = 345;
    mavlink_message_t msg;
    mavlink_msg_battery_status_encode(FC_SYS, FC_COMP, &msg, &bs);

    applyMavlinkMessage(s, msg, 200);
    TEST_ASSERT_EQUAL_INT32(345, s.consumedMah);
}

void test_global_position_int() {
    TelemetryState s = lockedState();
    mavlink_global_position_int_t gp{};
    gp.lat = 504501234;
    gp.lon = 305234567;
    gp.alt = 123456;          // mm MSL
    gp.relative_alt = 10500;  // mm
    mavlink_message_t msg;
    mavlink_msg_global_position_int_encode(FC_SYS, FC_COMP, &msg, &gp);

    applyMavlinkMessage(s, msg, 200);
    TEST_ASSERT_TRUE(s.hasPosition);
    TEST_ASSERT_TRUE(s.hasLatLon);
    TEST_ASSERT_EQUAL_INT32(504501234, s.latE7);
    TEST_ASSERT_EQUAL_INT32(305234567, s.lonE7);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 123.456f, s.altMslM);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.5f, s.altRelM);
}

void test_global_position_without_gps_has_no_lat_lon() {
    TelemetryState s = lockedState();
    mavlink_global_position_int_t gp{};
    gp.lat = 0;
    gp.lon = 0;
    gp.relative_alt = 2500;
    mavlink_message_t msg;
    mavlink_msg_global_position_int_encode(FC_SYS, FC_COMP, &msg, &gp);

    applyMavlinkMessage(s, msg, 200);
    TEST_ASSERT_TRUE(s.hasPosition);
    TEST_ASSERT_FALSE(s.hasLatLon);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, s.altRelM);
}

void test_gps_raw_int() {
    TelemetryState s = lockedState();
    mavlink_gps_raw_int_t g{};
    g.fix_type = GPS_FIX_TYPE_3D_FIX;
    g.satellites_visible = 12;
    g.eph = 85;
    mavlink_message_t msg;
    mavlink_msg_gps_raw_int_encode(FC_SYS, FC_COMP, &msg, &g);

    applyMavlinkMessage(s, msg, 200);
    TEST_ASSERT_TRUE(s.hasGps);
    TEST_ASSERT_EQUAL_UINT8(3, s.gpsFix);
    TEST_ASSERT_EQUAL_UINT8(12, s.satellites);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.85f, s.hdop);
}

void test_gps_raw_int_unknown_hdop() {
    TelemetryState s = lockedState();
    mavlink_gps_raw_int_t g{};
    g.fix_type = GPS_FIX_TYPE_NO_GPS;
    g.eph = UINT16_MAX;
    mavlink_message_t msg;
    mavlink_msg_gps_raw_int_encode(FC_SYS, FC_COMP, &msg, &g);

    applyMavlinkMessage(s, msg, 200);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, s.hdop);
}

void test_vfr_hud_and_attitude() {
    TelemetryState s = lockedState();
    mavlink_vfr_hud_t v{};
    v.groundspeed = 4.5f;
    v.airspeed = 5.0f;
    v.climb = -0.5f;
    v.heading = 270;
    v.throttle = 42;
    mavlink_message_t msg;
    mavlink_msg_vfr_hud_encode(FC_SYS, FC_COMP, &msg, &v);
    applyMavlinkMessage(s, msg, 200);

    mavlink_attitude_t a{};
    a.roll = 0.1745329f;  // 10 deg
    a.pitch = -0.0872665f;
    a.yaw = 3.1415926f;
    mavlink_msg_attitude_encode(FC_SYS, FC_COMP, &msg, &a);
    applyMavlinkMessage(s, msg, 210);

    TEST_ASSERT_TRUE(s.hasVfr);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.5f, s.groundspeed);
    TEST_ASSERT_EQUAL_INT16(270, s.headingDeg);
    TEST_ASSERT_EQUAL_UINT16(42, s.throttlePct);
    TEST_ASSERT_TRUE(s.hasAttitude);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, s.rollDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.0f, s.pitchDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f, s.yawDeg);
}

void test_system_time() {
    TelemetryState s = lockedState();
    mavlink_system_time_t t{};
    t.time_unix_usec = 1790000000000000ULL;
    mavlink_message_t msg;
    mavlink_msg_system_time_encode(FC_SYS, FC_COMP, &msg, &t);

    applyMavlinkMessage(s, msg, 5000);
    TEST_ASSERT_TRUE(s.unixTimeUs == 1790000000000000ULL);
    TEST_ASSERT_EQUAL_UINT32(5000, s.unixTimeRxMs);
}

void test_statustext_is_copied_and_counted() {
    TelemetryState s = lockedState();
    mavlink_statustext_t st{};
    st.severity = MAV_SEVERITY_WARNING;
    // 50 chars, no terminator: must still be terminated in the state
    memcpy(st.text, "PreArm: Battery below minimum arming voltage 12345", 50);
    mavlink_message_t msg;
    mavlink_msg_statustext_encode(FC_SYS, FC_COMP, &msg, &st);

    applyMavlinkMessage(s, msg, 200);
    TEST_ASSERT_EQUAL_UINT32(1, s.statusTextSeq);
    TEST_ASSERT_EQUAL_UINT8(MAV_SEVERITY_WARNING, s.statusSeverity);
    TEST_ASSERT_EQUAL_STRING("PreArm: Battery below minimum arming voltage 12345", s.statusText);
}

void test_flight_mode_names() {
    TEST_ASSERT_EQUAL_STRING("STABILIZE", flightModeName(MAV_TYPE_QUADROTOR, 0));
    TEST_ASSERT_EQUAL_STRING("ALT_HOLD", flightModeName(MAV_TYPE_HEXAROTOR, 2));
    TEST_ASSERT_EQUAL_STRING("RTL", flightModeName(MAV_TYPE_QUADROTOR, 6));
    TEST_ASSERT_EQUAL_STRING("SMART_RTL", flightModeName(MAV_TYPE_QUADROTOR, 21));
    TEST_ASSERT_EQUAL_STRING("FBWA", flightModeName(MAV_TYPE_FIXED_WING, 5));
    TEST_ASSERT_EQUAL_STRING("AUTO", flightModeName(MAV_TYPE_FIXED_WING, 10));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", flightModeName(MAV_TYPE_QUADROTOR, 999));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", flightModeName(MAV_TYPE_QUADROTOR, 8));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_autopilot_heartbeat_sets_identity_mode_and_armed);
    RUN_TEST(test_gcs_heartbeat_is_ignored);
    RUN_TEST(test_messages_before_autopilot_heartbeat_are_ignored);
    RUN_TEST(test_messages_from_other_system_are_ignored_after_lock);
    RUN_TEST(test_released_lock_accepts_new_autopilot_system);
    RUN_TEST(test_sys_status_battery);
    RUN_TEST(test_sys_status_unknown_current_and_remaining);
    RUN_TEST(test_battery_status_consumed_mah);
    RUN_TEST(test_global_position_int);
    RUN_TEST(test_global_position_without_gps_has_no_lat_lon);
    RUN_TEST(test_gps_raw_int);
    RUN_TEST(test_gps_raw_int_unknown_hdop);
    RUN_TEST(test_vfr_hud_and_attitude);
    RUN_TEST(test_system_time);
    RUN_TEST(test_statustext_is_copied_and_counted);
    RUN_TEST(test_flight_mode_names);
    return UNITY_END();
}
