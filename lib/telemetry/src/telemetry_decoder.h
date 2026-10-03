#pragma once

#include <ardupilotmega/mavlink.h>

#include "telemetry_state.h"

// Applies one MAVLink message to the telemetry state.
// The state locks onto the first autopilot HEARTBEAT; messages from other
// systems (GCS, other companions) are ignored afterwards.
// Returns true if the message was accepted from the autopilot.
bool applyMavlinkMessage(TelemetryState& state, const mavlink_message_t& msg, uint32_t nowMs);

// Forgets the locked autopilot so the next autopilot HEARTBEAT (possibly from
// another system ID after an FC swap or SYSID change) is accepted.
// Telemetry values are kept; group flags stay as they were.
void releaseAutopilotLock(TelemetryState& state);

// Returns true for a HEARTBEAT that originates from a flight controller
// (not from a GCS or another onboard computer).
bool isAutopilotHeartbeat(const mavlink_message_t& msg);

// ArduPilot flight mode name for the given MAV_TYPE and custom_mode.
// Returns "UNKNOWN" for unsupported vehicle types or mode numbers.
const char* flightModeName(uint8_t vehicleType, uint32_t customMode);
