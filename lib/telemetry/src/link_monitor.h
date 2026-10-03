#pragma once

#include <stdint.h>

enum class LinkState : uint8_t { Waiting, Connected, Lost };

enum class LinkEvent : uint8_t { None, Connected, Lost, Restored };

// Tracks the MAVLink connection health based on autopilot heartbeats.
// All time arithmetic is wraparound-safe for a 32-bit millisecond counter.
class LinkMonitor {
public:
    explicit LinkMonitor(uint32_t timeoutMs) : timeoutMs_(timeoutMs) {}

    // Call on every autopilot HEARTBEAT.
    LinkEvent onHeartbeat(uint32_t nowMs);

    // Call periodically; reports Lost once when heartbeats stop.
    LinkEvent update(uint32_t nowMs);

    LinkState state() const { return state_; }
    uint32_t lostCount() const { return lostCount_; }
    uint32_t lastHeartbeatMs() const { return lastHeartbeatMs_; }

private:
    uint32_t timeoutMs_;
    LinkState state_ = LinkState::Waiting;
    uint32_t lastHeartbeatMs_ = 0;
    uint32_t lostCount_ = 0;
};

const char* linkStateName(LinkState state);
