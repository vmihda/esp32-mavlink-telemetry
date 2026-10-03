#pragma once

#include <Arduino.h>

// Mounts LittleFS (formats it on first use) and increments the persistent
// boot counter. Must be called before startLoggerTask().
bool storageBegin();

uint32_t bootCount();

// Starts the task that writes telemetry rows to CSV and events to events.csv.
TaskHandle_t startLoggerTask();

// Name of the CSV file currently being written ("/log_00007_01.csv").
String activeLogFile();

uint32_t loggedRows();

// True for names the web UI may serve or delete: "/log_*.csv" and event logs.
bool isServableFile(const String& path);
