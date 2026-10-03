#include <string.h>
#include <unity.h>

#include "csv_format.h"

static int countColumns(const char* line) {
    int n = 1;
    for (const char* p = line; *p; ++p) {
        if (*p == ',') ++n;
    }
    return n;
}

// Returns column `index` of a CSV line into `out`.
static void column(const char* line, int index, char* out, size_t outLen) {
    const char* p = line;
    for (int i = 0; i < index && p; ++i) {
        p = strchr(p, ',');
        if (p) ++p;
    }
    size_t n = 0;
    while (p && *p && *p != ',' && n + 1 < outLen) out[n++] = *p++;
    out[n] = 0;
}

static int headerIndex(const char* name) {
    const char* h = csvHeader();
    char col[32];
    for (int i = 0; i < countColumns(h); ++i) {
        column(h, i, col, sizeof col);
        if (strcmp(col, name) == 0) return i;
    }
    TEST_FAIL_MESSAGE(name);
    return -1;
}

static TelemetryState fullState() {
    TelemetryState s;
    s.hasHeartbeat = true;
    s.vehicleType = 2;  // MAV_TYPE_QUADROTOR
    s.armed = true;
    s.customMode = 5;
    s.hasBattery = true;
    s.voltageV = 12.6f;
    s.hasCurrent = true;
    s.currentA = 1.5f;
    s.batteryPct = 87;
    s.consumedMah = 345;
    s.hasPosition = true;
    s.hasLatLon = true;
    s.latE7 = 504501234;
    s.lonE7 = -305234567;
    s.altMslM = 123.456f;
    s.altRelM = 10.5f;
    s.hasGps = true;
    s.gpsFix = 3;
    s.satellites = 12;
    s.hdop = 0.85f;
    s.hasVfr = true;
    s.groundspeed = 4.5f;
    s.headingDeg = 270;
    s.throttlePct = 42;
    s.hasAttitude = true;
    s.rollDeg = 10.0f;
    return s;
}

static void assertColumn(const char* row, const char* name, const char* expected) {
    char col[48];
    column(row, headerIndex(name), col, sizeof col);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, col, name);
}

void setUp() {}
void tearDown() {}

void test_row_has_same_column_count_as_header() {
    char row[512];
    CsvRowContext ctx{1000, 7, true};
    TEST_ASSERT_GREATER_THAN(0, formatCsvRow(row, sizeof row, fullState(), ctx));
    TEST_ASSERT_EQUAL(countColumns(csvHeader()), countColumns(row));

    TelemetryState empty;
    TEST_ASSERT_GREATER_THAN(0, formatCsvRow(row, sizeof row, empty, ctx));
    TEST_ASSERT_EQUAL(countColumns(csvHeader()), countColumns(row));
}

void test_row_values() {
    char row[512];
    CsvRowContext ctx{123456, 7, true};
    formatCsvRow(row, sizeof row, fullState(), ctx);

    assertColumn(row, "uptime_ms", "123456");
    assertColumn(row, "boot", "7");
    assertColumn(row, "link", "1");
    assertColumn(row, "armed", "1");
    assertColumn(row, "mode", "LOITER");
    assertColumn(row, "voltage_v", "12.60");
    assertColumn(row, "current_a", "1.50");
    assertColumn(row, "battery_pct", "87");
    assertColumn(row, "consumed_mah", "345");
    assertColumn(row, "lat", "50.4501234");
    assertColumn(row, "lon", "-30.5234567");
    assertColumn(row, "alt_msl_m", "123.46");
    assertColumn(row, "alt_rel_m", "10.50");
    assertColumn(row, "gps_fix", "3");
    assertColumn(row, "satellites", "12");
    assertColumn(row, "hdop", "0.85");
    assertColumn(row, "groundspeed_ms", "4.50");
    assertColumn(row, "heading_deg", "270");
    assertColumn(row, "throttle_pct", "42");
    assertColumn(row, "roll_deg", "10.0");
}

void test_unknown_values_are_empty() {
    char row[512];
    CsvRowContext ctx{1, 1, false};
    TelemetryState s = fullState();
    s.hasCurrent = false;
    s.batteryPct = -1;
    s.consumedMah = -1;
    s.hdop = -1;
    s.hasPosition = false;
    formatCsvRow(row, sizeof row, s, ctx);

    assertColumn(row, "link", "0");
    assertColumn(row, "current_a", "");
    assertColumn(row, "battery_pct", "");
    assertColumn(row, "consumed_mah", "");
    assertColumn(row, "hdop", "");
    assertColumn(row, "lat", "");
    assertColumn(row, "alt_rel_m", "");
    assertColumn(row, "utc", "");
}

void test_lat_lon_empty_without_gps_but_altitude_kept() {
    char row[512];
    CsvRowContext ctx{1, 1, true};
    TelemetryState s = fullState();
    s.hasLatLon = false;
    formatCsvRow(row, sizeof row, s, ctx);

    assertColumn(row, "lat", "");
    assertColumn(row, "lon", "");
    assertColumn(row, "alt_rel_m", "10.50");
}

void test_small_lat_negative_sign_kept() {
    char row[512];
    CsvRowContext ctx{1, 1, true};
    TelemetryState s = fullState();
    s.latE7 = -5;  // -0.0000005
    formatCsvRow(row, sizeof row, s, ctx);
    assertColumn(row, "lat", "-0.0000005");
}

void test_buffer_too_small() {
    char row[16];
    CsvRowContext ctx{1, 1, true};
    TEST_ASSERT_EQUAL(-1, formatCsvRow(row, sizeof row, fullState(), ctx));
}

void test_utc_extrapolated_from_system_time() {
    TelemetryState s;
    s.unixTimeUs = 1790000000123000ULL;  // 2026-09-21T14:13:20.123Z
    s.unixTimeRxMs = 1000;
    char buf[32];
    TEST_ASSERT_TRUE(formatUtc(buf, sizeof buf, s, 3500));
    TEST_ASSERT_EQUAL_STRING("2026-09-21T14:13:22.623Z", buf);
}

void test_utc_when_time_received_after_now() {
    // SYSTEM_TIME may arrive between taking `now` and the state snapshot
    TelemetryState s;
    s.unixTimeUs = 1790000000123000ULL;  // 2026-09-21T14:13:20.123Z
    s.unixTimeRxMs = 5000;
    char buf[32];
    TEST_ASSERT_TRUE(formatUtc(buf, sizeof buf, s, 4900));
    TEST_ASSERT_EQUAL_STRING("2026-09-21T14:13:20.023Z", buf);
}

void test_utc_unknown() {
    TelemetryState s;
    char buf[32] = "garbage";
    TEST_ASSERT_FALSE(formatUtc(buf, sizeof buf, s, 3500));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_log_file_name() {
    char buf[32];
    makeLogFileName(buf, sizeof buf, 7, 1);
    TEST_ASSERT_EQUAL_STRING("/log_00007_01.csv", buf);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_row_has_same_column_count_as_header);
    RUN_TEST(test_row_values);
    RUN_TEST(test_unknown_values_are_empty);
    RUN_TEST(test_lat_lon_empty_without_gps_but_altitude_kept);
    RUN_TEST(test_small_lat_negative_sign_kept);
    RUN_TEST(test_buffer_too_small);
    RUN_TEST(test_utc_extrapolated_from_system_time);
    RUN_TEST(test_utc_when_time_received_after_now);
    RUN_TEST(test_utc_unknown);
    RUN_TEST(test_log_file_name);
    return UNITY_END();
}
