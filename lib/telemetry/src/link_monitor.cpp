#include "link_monitor.h"

LinkEvent LinkMonitor::onHeartbeat(uint32_t nowMs) {
    lastHeartbeatMs_ = nowMs;
    const LinkState prev = state_;
    state_ = LinkState::Connected;
    if (prev == LinkState::Waiting) return LinkEvent::Connected;
    if (prev == LinkState::Lost) return LinkEvent::Restored;
    return LinkEvent::None;
}

LinkEvent LinkMonitor::update(uint32_t nowMs) {
    if (state_ != LinkState::Connected) return LinkEvent::None;
    if (nowMs - lastHeartbeatMs_ <= timeoutMs_) return LinkEvent::None;
    state_ = LinkState::Lost;
    lostCount_++;
    return LinkEvent::Lost;
}

const char* linkStateName(LinkState state) {
    switch (state) {
        case LinkState::Waiting: return "WAITING";
        case LinkState::Connected: return "CONNECTED";
        case LinkState::Lost: return "LOST";
    }
    return "?";
}
