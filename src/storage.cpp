#include "storage.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <stdarg.h>

#include <algorithm>
#include <vector>

#include "config.h"
#include "csv_format.h"
#include "telemetry_store.h"

TelemetryStore gStore;

namespace {

QueueHandle_t gEventQueue = nullptr;
SemaphoreHandle_t gFileMutex = nullptr;
uint32_t gBootCount = 0;
bool gFsReady = false;
char gActiveFile[32] = {0};
volatile uint32_t gLoggedRows = 0;
volatile uint32_t gDroppedEvents = 0;  // queue full; reported by the logger

const char* resetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXTERNAL";
        case ESP_RST_SW: return "SOFTWARE";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        default: return "UNKNOWN";
    }
}

bool isLogFile(const String& name) {
    return name.startsWith("/log_") && name.endsWith(".csv");
}

std::vector<String> listLogFiles() {
    std::vector<String> names;
    File root = LittleFS.open("/");
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        String name = f.name();
        if (!name.startsWith("/")) name = "/" + name;
        if (isLogFile(name)) names.push_back(name);
    }
    std::sort(names.begin(), names.end());  // zero-padded: oldest first
    return names;
}

class CsvLogger {
public:
    void loop() {
        const uint32_t now = millis();
        drainEvents();

        if (now - lastRowMs_ >= cfg::LOG_PERIOD_MS) {
            lastRowMs_ = now;
            writeRow(now);
        }
        if (now - lastFlushMs_ >= cfg::LOG_FLUSH_PERIOD_MS) {
            lastFlushMs_ = now;
            flush();
        }
    }

private:
    void openNewFile() {
        ensureFreeSpace();
        char name[32];
        makeLogFileName(name, sizeof name, gBootCount, part_);

        xSemaphoreTake(gFileMutex, portMAX_DELAY);
        if (file_) file_.close();
        file_ = LittleFS.open(name, FILE_APPEND);
        if (file_ && file_.size() == 0) {
            file_.println(csvHeader());
            file_.flush();
        }
        strlcpy(gActiveFile, name, sizeof gActiveFile);
        xSemaphoreGive(gFileMutex);

        if (!file_) {
            Serial.printf("[log] cannot open %s\n", name);
            return;
        }
        Serial.printf("[log] writing %s\n", name);
    }

    void writeRow(uint32_t now) {
        if (!gFsReady) return;
        const SharedStatus st = gStore.snapshot();
        // Rows are written only while the FC is talking to us; gaps in
        // uptime_ms show link outages, the reason is in events.csv.
        if (st.link != LinkState::Connected) return;

        char row[384];
        const CsvRowContext ctx{now, gBootCount, true};
        if (formatCsvRow(row, sizeof row, st.telemetry, ctx) < 0) return;

        if (!file_) {
            openNewFile();
            if (!file_) return;
        }
        file_.println(row);
        gLoggedRows = gLoggedRows + 1;

        if (file_.size() >= cfg::LOG_MAX_FILE_BYTES) {
            part_++;
            openNewFile();
        }
    }

    void flush() {
        if (!gFsReady) return;
        xSemaphoreTake(gFileMutex, portMAX_DELAY);
        if (file_) file_.flush();
        xSemaphoreGive(gFileMutex);
        ensureFreeSpace();
    }

    // Deletes the oldest telemetry logs (never the active one) when the FS fills up.
    // Bounded: a failed remove must not spin until the task watchdog fires.
    void ensureFreeSpace() {
        const size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
        if (freeBytes >= cfg::FS_MIN_FREE_BYTES) return;
        // The active file is skipped, not treated as the end of the list: after an
        // NVS reset its boot number restarts and it may sort before older logs.
        for (const String& name : listLogFiles()) {
            if (name == gActiveFile) continue;
            Serial.printf("[log] low space, deleting %s\n", name.c_str());
            if (!LittleFS.remove(name)) {
                Serial.printf("[log] cannot delete %s\n", name.c_str());
                return;
            }
            if (LittleFS.totalBytes() - LittleFS.usedBytes() >= cfg::FS_MIN_FREE_BYTES) return;
        }
    }

    void drainEvents() {
        LogEvent ev;
        while (xQueueReceive(gEventQueue, &ev, 0) == pdTRUE) {
            writeEvent(ev);
        }
        const uint32_t dropped = gDroppedEvents;
        if (dropped != reportedDrops_) {
            LogEvent lost{};
            lost.uptimeMs = millis();
            strlcpy(lost.kind, "EVENTS_DROPPED", sizeof lost.kind);
            snprintf(lost.detail, sizeof lost.detail, "%lu events lost (queue full)",
                     static_cast<unsigned long>(dropped - reportedDrops_));
            reportedDrops_ = dropped;
            writeEvent(lost);
        }
    }

    void writeEvent(const LogEvent& ev) {
        const SharedStatus st = gStore.snapshot();
        char utc[32];
        formatUtc(utc, sizeof utc, st.telemetry, ev.uptimeMs);

        // Keep CSV valid: the detail is quoted, embedded quotes doubled
        String detail = ev.detail;
        detail.replace("\"", "\"\"");

        Serial.printf("[event] %lu %s %s\n", static_cast<unsigned long>(ev.uptimeMs), ev.kind,
                      ev.detail);

        if (!gFsReady) return;
        File f = LittleFS.open(cfg::EVENTS_FILE, FILE_APPEND);
        if (!f) return;
        if (f.size() == 0) f.println("uptime_ms,utc,boot,event,detail");
        f.printf("%lu,%s,%lu,%s,\"%s\"\n", static_cast<unsigned long>(ev.uptimeMs), utc,
                 static_cast<unsigned long>(gBootCount), ev.kind, detail.c_str());
        const size_t size = f.size();
        f.close();

        if (size > cfg::EVENTS_MAX_FILE_BYTES) {
            LittleFS.remove(cfg::EVENTS_OLD_FILE);
            LittleFS.rename(cfg::EVENTS_FILE, cfg::EVENTS_OLD_FILE);
        }
    }

    File file_;
    uint32_t part_ = 1;
    uint32_t lastRowMs_ = 0;
    uint32_t lastFlushMs_ = 0;
    uint32_t reportedDrops_ = 0;
};

void loggerTask(void*) {
    static CsvLogger logger;
    // A hung flash operation reboots the ESP; the reason is logged as TASK_WDT
    if (esp_task_wdt_add(nullptr) != ESP_OK) Serial.println("[log] task WDT subscribe failed");
    for (;;) {
        esp_task_wdt_reset();
        logger.loop();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

}  // namespace

void postEvent(const char* kind, const char* detailFmt, ...) {
    if (!gEventQueue) return;
    LogEvent ev{};
    ev.uptimeMs = millis();
    strlcpy(ev.kind, kind, sizeof ev.kind);
    va_list args;
    va_start(args, detailFmt);
    vsnprintf(ev.detail, sizeof ev.detail, detailFmt, args);
    va_end(args);
    if (xQueueSend(gEventQueue, &ev, 0) != pdTRUE) gDroppedEvents = gDroppedEvents + 1;
}

bool storageBegin() {
    gStore.begin();
    gFileMutex = xSemaphoreCreateMutex();
    gEventQueue = xQueueCreate(32, sizeof(LogEvent));
    configASSERT(gFileMutex && gEventQueue);

    Preferences prefs;
    prefs.begin("telemetry", false);
    gBootCount = prefs.getUInt("boot", 0) + 1;
    prefs.putUInt("boot", gBootCount);
    prefs.end();

    // formatOnFail: the first boot after flashing gets an empty filesystem
    if (!LittleFS.begin(true)) {
        Serial.println("[fs] LittleFS mount failed, logging disabled");
        return false;
    }
    gFsReady = true;
    Serial.printf("[fs] LittleFS %u / %u bytes used\n", static_cast<unsigned>(LittleFS.usedBytes()),
                  static_cast<unsigned>(LittleFS.totalBytes()));

    postEvent("BOOT", "boot #%lu, reset reason %s", static_cast<unsigned long>(gBootCount),
              resetReasonName(esp_reset_reason()));
    return true;
}

uint32_t bootCount() { return gBootCount; }

TaskHandle_t startLoggerTask() {
    // Core 1, below the MAVLink task: flash writes may stall for tens of ms
    TaskHandle_t handle = nullptr;
    xTaskCreatePinnedToCore(loggerTask, "logger", 8192, nullptr, 2, &handle, 1);
    configASSERT(handle);
    return handle;
}

String activeLogFile() {
    xSemaphoreTake(gFileMutex, portMAX_DELAY);
    String name = gActiveFile;
    xSemaphoreGive(gFileMutex);
    return name;
}

uint32_t loggedRows() { return gLoggedRows; }

bool isServableFile(const String& path) {
    if (path.indexOf("..") >= 0 || path.indexOf('/', 1) >= 0) return false;
    return isLogFile(path) || path == cfg::EVENTS_FILE || path == cfg::EVENTS_OLD_FILE;
}
