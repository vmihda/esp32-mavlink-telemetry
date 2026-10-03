#pragma once

#include <Arduino.h>

#include "link_monitor.h"
#include "telemetry_state.h"

// Everything the MAVLink task publishes for the logger, web UI and console.
struct SharedStatus {
    TelemetryState telemetry;
    LinkState link = LinkState::Waiting;
    uint32_t lostCount = 0;
    uint32_t baud = 0;
    bool pinsSwapped = false;
    uint32_t rxBytes = 0;
    uint32_t rxPackets = 0;
    uint32_t rxParseErrors = 0;
    uint32_t streamRequests = 0;
};

// Thread-safe holder of SharedStatus shared between FreeRTOS tasks.
class TelemetryStore {
public:
    void begin() {
        mutex_ = xSemaphoreCreateMutex();
        configASSERT(mutex_);
    }

    template <typename Fn>
    void update(Fn&& fn) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        fn(status_);
        xSemaphoreGive(mutex_);
    }

    SharedStatus snapshot() {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        SharedStatus copy = status_;
        xSemaphoreGive(mutex_);
        return copy;
    }

private:
    SemaphoreHandle_t mutex_ = nullptr;
    SharedStatus status_;
};

// Event for the events log (link changes, FC status texts, boot info).
struct LogEvent {
    uint32_t uptimeMs;
    char kind[16];
    char detail[80];
};

extern TelemetryStore gStore;

// Queues an event for the logger task; never blocks the caller.
void postEvent(const char* kind, const char* detailFmt, ...);
