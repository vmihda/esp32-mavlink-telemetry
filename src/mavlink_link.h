#pragma once

#include <Arduino.h>

// Starts the MAVLink task: reads the FC UART, decodes telemetry into gStore,
// sends companion heartbeats, requests message streams and tracks link health.
TaskHandle_t startMavlinkTask(HardwareSerial& uart);
