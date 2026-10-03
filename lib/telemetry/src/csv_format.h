#pragma once

#include <stddef.h>
#include <stdint.h>

#include "telemetry_state.h"

struct CsvRowContext {
    uint32_t uptimeMs;
    uint32_t bootCount;
    bool linkUp;
};

// CSV header line (without trailing newline).
const char* csvHeader();

// Formats one CSV row (without trailing newline). Unknown values are left empty.
// Returns the number of characters written, or -1 if the buffer is too small.
int formatCsvRow(char* buf, size_t len, const TelemetryState& state, const CsvRowContext& ctx);

// Formats the current UTC time as "YYYY-MM-DDTHH:MM:SS.mmmZ", extrapolated from
// the last SYSTEM_TIME by the elapsed local time. Writes an empty string and
// returns false when the time is unknown.
bool formatUtc(char* buf, size_t len, const TelemetryState& state, uint32_t nowMs);

// Log file name for the given boot session and part: "/log_00007_01.csv".
void makeLogFileName(char* buf, size_t len, uint32_t bootCount, uint32_t part);
