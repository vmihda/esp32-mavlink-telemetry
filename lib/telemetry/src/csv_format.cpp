#include "csv_format.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "telemetry_decoder.h"

namespace {

// Appends formatted text to a fixed buffer; remembers overflow.
class Writer {
public:
    Writer(char* buf, size_t len) : buf_(buf), len_(len) {
        if (len_) buf_[0] = 0;
    }

    void printf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vappend(fmt, args);
        va_end(args);
    }

    void comma() { printf(","); }

    // Value followed by a separator; empty when `known` is false
    void field(bool known, const char* fmt, ...) {
        if (known) {
            va_list args;
            va_start(args, fmt);
            vappend(fmt, args);
            va_end(args);
        }
        comma();
    }

    void degreesE7(bool known, int32_t valueE7) {
        if (known) {
            const char* sign = valueE7 < 0 ? "-" : "";
            const long absValue = labs(static_cast<long>(valueE7));
            printf("%s%ld.%07ld", sign, absValue / 10000000L, absValue % 10000000L);
        }
        comma();
    }

    int result() const { return overflow_ ? -1 : static_cast<int>(pos_); }

private:
    void vappend(const char* fmt, va_list args) {
        if (overflow_) return;
        const int n = vsnprintf(buf_ + pos_, len_ - pos_, fmt, args);
        if (n < 0 || static_cast<size_t>(n) >= len_ - pos_) {
            overflow_ = true;
            return;
        }
        pos_ += static_cast<size_t>(n);
    }

    char* buf_;
    size_t len_;
    size_t pos_ = 0;
    bool overflow_ = false;
};

}  // namespace

const char* csvHeader() {
    return "uptime_ms,utc,boot,link,armed,mode,"
           "voltage_v,current_a,battery_pct,consumed_mah,"
           "lat,lon,alt_msl_m,alt_rel_m,"
           "gps_fix,satellites,hdop,"
           "groundspeed_ms,airspeed_ms,climb_ms,heading_deg,throttle_pct,"
           "roll_deg,pitch_deg,yaw_deg";
}

int formatCsvRow(char* buf, size_t len, const TelemetryState& s, const CsvRowContext& ctx) {
    Writer w(buf, len);
    char utc[32];
    formatUtc(utc, sizeof utc, s, ctx.uptimeMs);

    w.field(true, "%lu", static_cast<unsigned long>(ctx.uptimeMs));
    w.field(true, "%s", utc);
    w.field(true, "%lu", static_cast<unsigned long>(ctx.bootCount));
    w.field(true, "%d", ctx.linkUp ? 1 : 0);
    w.field(s.hasHeartbeat, "%d", s.armed ? 1 : 0);
    w.field(s.hasHeartbeat, "%s", flightModeName(s.vehicleType, s.customMode));

    w.field(s.hasBattery, "%.2f", s.voltageV);
    w.field(s.hasBattery && s.hasCurrent, "%.2f", s.currentA);
    w.field(s.hasBattery && s.batteryPct >= 0, "%d", s.batteryPct);
    w.field(s.consumedMah >= 0, "%ld", static_cast<long>(s.consumedMah));

    w.degreesE7(s.hasPosition && s.hasLatLon, s.latE7);
    w.degreesE7(s.hasPosition && s.hasLatLon, s.lonE7);
    w.field(s.hasPosition, "%.2f", s.altMslM);
    w.field(s.hasPosition, "%.2f", s.altRelM);

    w.field(s.hasGps, "%u", s.gpsFix);
    w.field(s.hasGps && s.satellites != UINT8_MAX, "%u", s.satellites);
    w.field(s.hasGps && s.hdop >= 0, "%.2f", s.hdop);

    w.field(s.hasVfr, "%.2f", s.groundspeed);
    w.field(s.hasVfr, "%.2f", s.airspeed);
    w.field(s.hasVfr, "%.2f", s.climb);
    w.field(s.hasVfr, "%d", s.headingDeg);
    w.field(s.hasVfr, "%u", s.throttlePct);

    w.field(s.hasAttitude, "%.1f", s.rollDeg);
    w.field(s.hasAttitude, "%.1f", s.pitchDeg);
    // Last column: no trailing separator
    if (s.hasAttitude) w.printf("%.1f", s.yawDeg);
    return w.result();
}

bool formatUtc(char* buf, size_t len, const TelemetryState& s, uint32_t nowMs) {
    if (len) buf[0] = 0;
    if (s.unixTimeUs == 0) return false;

    // Signed: the time may have been received slightly after `nowMs` was taken
    const int32_t sinceRxMs = static_cast<int32_t>(nowMs - s.unixTimeRxMs);
    const uint64_t nowUnixMs = static_cast<uint64_t>(
        static_cast<int64_t>(s.unixTimeUs / 1000ULL) + sinceRxMs);
    const time_t seconds = static_cast<time_t>(nowUnixMs / 1000ULL);
    struct tm tm;
    gmtime_r(&seconds, &tm);
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03uZ", tm.tm_year + 1900, tm.tm_mon + 1,
             tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
             static_cast<unsigned>(nowUnixMs % 1000ULL));
    return true;
}

void makeLogFileName(char* buf, size_t len, uint32_t bootCount, uint32_t part) {
    snprintf(buf, len, "/log_%05lu_%02lu.csv", static_cast<unsigned long>(bootCount),
             static_cast<unsigned long>(part));
}
