#include "UI.h"
#include "API.h"
#include "Config.h"
#include "Logger.h"
#include <WiFi.h>

static TFT_eSPI *tft = nullptr;

static const unsigned long TOUCH_DEBOUNCE_MS = 250;
static const int TASKBAR_H = 25;
static const int TABBAR_Y = 212;
static const int TABBAR_H = 28;
static const int CONTENT_Y = 31;
static const int CONTENT_H = TABBAR_Y - CONTENT_Y;

static const uint16_t C_BG = TFT_BLACK;
static const uint16_t C_PANEL = 0x18E3;
static const uint16_t C_PANEL_2 = 0x2945;
static const uint16_t C_STROKE = 0x4A49;
static const uint16_t C_TEXT = TFT_WHITE;
static const uint16_t C_MUTED = 0xA514;
static const uint16_t C_CYAN = 0x04BF;
static const uint16_t C_GREEN = 0x07E0;
static const uint16_t C_AMBER = 0xFEA0;
static const uint16_t C_RED = 0xF986;
static const uint16_t C_BLUE = 0x1B9F;

enum ScreenTab {
    TAB_NOW,
    TAB_FORECAST,
    TAB_LOG,
    TAB_SYSTEM
};

static ScreenTab currentTab = TAB_NOW;
static bool chromeDirty = true;
static bool contentDirty = true;
static bool forecastDirty = true;
static unsigned long lastTouchMs = 0;

static void drawPaddedText(int x, int y, int font, uint16_t color, const char *text, int width, uint16_t bg = C_BG) {
    tft->setTextFont(font);
    tft->setTextColor(color, bg);
    tft->setTextPadding(width);
    tft->drawString(text, x, y);
    tft->setTextPadding(0);
}

static void drawRightText(int x, int y, int font, uint16_t color, const char *text, int width, uint16_t bg = C_BG) {
    tft->setTextFont(font);
    tft->setTextColor(color, bg);
    tft->setTextDatum(TR_DATUM);
    tft->setTextPadding(width);
    tft->drawString(text, x + width, y);
    tft->setTextPadding(0);
    tft->setTextDatum(TL_DATUM);
}

static void clearContent() {
    tft->fillRect(0, CONTENT_Y, tft->width(), CONTENT_H, C_BG);
}

static void drawCard(int x, int y, int w, int h, uint16_t accent = C_STROKE) {
    tft->fillRoundRect(x, y, w, h, 6, C_PANEL);
    tft->drawRoundRect(x, y, w, h, 6, C_STROKE);
    tft->fillRoundRect(x, y, 4, h, 3, accent);
}

static void drawChip(int x, int y, const char *label, bool ok, uint16_t bg = C_PANEL_2) {
    uint16_t color = ok ? C_GREEN : C_RED;
    tft->fillRoundRect(x, y, 34, 15, 5, bg);
    tft->drawRoundRect(x, y, 34, 15, 5, color);
    drawPaddedText(x + 5, y + 2, 1, color, label, 24, bg);
}

static void drawMetricCard(int x, int y, int w, int h, const char *label, const char *value, uint16_t accent) {
    if (contentDirty) {
        drawCard(x, y, w, h, accent);
        drawPaddedText(x + 10, y + 8, 1, C_MUTED, label, w - 18, C_PANEL);
    }
    drawPaddedText(x + 10, y + 24, 2, C_TEXT, value, w - 18, C_PANEL);
}

static void drawTaskbar(const UiStatus &status) {
    const uint16_t bg = 0x1082;
    if (chromeDirty) {
        tft->fillRect(0, 0, tft->width(), TASKBAR_H, bg);
        tft->fillRect(0, TASKBAR_H - 2, tft->width(), 2, C_BLUE);
    }

    char line[80];
    snprintf(line, sizeof(line), "%s  %us", WiFi.localIP().toString().c_str(), DATA_LOG_INTERVAL_SEC);

    drawPaddedText(7, 5, 2, C_TEXT, "PCHUB", 54, bg);
    drawPaddedText(67, 5, 2, C_CYAN, line, 118, bg);
    drawChip(202, 5, "SD", status.sdReady, bg);
    drawChip(241, 5, "API", apiReady(), bg);
    drawChip(280, 5, "LOG", loggerReady(), bg);
}

static void drawTabButton(int index, const char *label, bool active) {
    int tabW = tft->width() / 4;
    int x = index * tabW;
    uint16_t bg = active ? C_BLUE : 0x1082;
    uint16_t fg = active ? C_TEXT : C_MUTED;

    tft->fillRect(x, TABBAR_Y, tabW, TABBAR_H, 0x1082);
    tft->fillRoundRect(x + 3, TABBAR_Y + 4, tabW - 6, TABBAR_H - 7, 6, bg);
    if (active) {
        tft->fillRect(x + 10, TABBAR_Y + TABBAR_H - 4, tabW - 20, 2, C_AMBER);
    }
    drawPaddedText(x + 9, TABBAR_Y + 9, 2, fg, label, tabW - 18, bg);
}

static void drawTabbar() {
    if (!chromeDirty) return;

    tft->fillRect(0, TABBAR_Y - 1, tft->width(), 1, C_STROKE);
    drawTabButton(0, "Now", currentTab == TAB_NOW);
    drawTabButton(1, "Fcst", currentTab == TAB_FORECAST);
    drawTabButton(2, "Log", currentTab == TAB_LOG);
    drawTabButton(3, "System", currentTab == TAB_SYSTEM);
}

static void drawNow(const SensorData &sensor, const WeatherData &weather) {
    char line[96];

    if (contentDirty) {
        drawCard(8, CONTENT_Y + 4, 304, 45, C_GREEN);
        drawPaddedText(20, CONTENT_Y + 12, 1, C_MUTED, "LOCAL TIME", 120, C_PANEL);
    }
    drawPaddedText(20, CONTENT_Y + 25, 4, C_GREEN, sensor.timeStr.c_str(), 278, C_PANEL);

    if (weather.ok) {
        snprintf(line, sizeof(line), "%.1fC  %s", weather.temperature, weatherDesc(weather.weather_code));
    } else {
        snprintf(line, sizeof(line), "no data");
    }
    drawMetricCard(8, CONTENT_Y + 56, 148, 48, "OUTSIDE", line, C_AMBER);

    if (weather.ok) {
        snprintf(line, sizeof(line), "%.1f km/h", weather.wind_speed);
    } else {
        snprintf(line, sizeof(line), "--");
    }
    drawMetricCard(164, CONTENT_Y + 56, 148, 48, "WIND", line, C_CYAN);

    if (sensor.bme_ok) {
        snprintf(line, sizeof(line), "%.1fC  %d%%", sensor.temperature, (int)sensor.humidity);
    } else {
        snprintf(line, sizeof(line), "no data");
    }
    drawMetricCard(8, CONTENT_Y + 112, 148, 48, "ROOM", line, C_CYAN);

    if (sensor.bme_ok) {
        snprintf(line, sizeof(line), "%.0fhPa  %.0fk", sensor.pressure, sensor.gas);
    } else {
        snprintf(line, sizeof(line), "--");
    }
    drawMetricCard(164, CONTENT_Y + 112, 148, 48, "AIR", line, C_GREEN);
}

static void drawForecast(const WeatherData &weather) {
    if (!contentDirty && !forecastDirty) return;

    char line[80];
    if (contentDirty || forecastDirty) {
        tft->fillRect(0, CONTENT_Y, tft->width(), CONTENT_H, C_BG);
        drawPaddedText(10, CONTENT_Y + 3, 2, C_TEXT, "7-day forecast", 300);
    }

    if (weather.ok) {
        for (int i = 0; i < 7; i++) {
            int y = CONTENT_Y + 24 + i * 22;
            const DayForecast &d = weather.forecast[i];
            if (contentDirty || forecastDirty) {
                uint16_t accent = i == 0 ? C_AMBER : C_STROKE;
                drawCard(8, y, 304, 18, accent);
            }
            drawPaddedText(18, y + 3, 1, i == 0 ? C_AMBER : C_CYAN, d.day, 36, C_PANEL);
            snprintf(line, sizeof(line), "%+.0f / %+.0f", d.temp_min, d.temp_max);
            drawPaddedText(62, y + 3, 1, C_TEXT, line, 72, C_PANEL);
            drawPaddedText(150, y + 3, 1, C_MUTED, weatherDesc(d.weather_code), 135, C_PANEL);
        }
    } else {
        drawCard(8, CONTENT_Y + 32, 304, 54, C_RED);
        drawPaddedText(20, CONTENT_Y + 52, 2, C_MUTED, "No forecast data", 260, C_PANEL);
    }

    forecastDirty = false;
}

static void drawLog(const UiStatus &status) {
    char line[96];
    unsigned long ageSec = status.lastLogWriteMs == 0 ? 0 : (millis() - status.lastLogWriteMs) / 1000UL;

    if (contentDirty) {
        drawPaddedText(10, CONTENT_Y + 3, 2, C_TEXT, "Data logging", 300);
        drawCard(8, CONTENT_Y + 28, 304, 58, loggerReady() ? C_GREEN : C_RED);
        drawCard(8, CONTENT_Y + 96, 304, 58, C_BLUE);
    }

    snprintf(line, sizeof(line), "%s", readingsLogPath());
    drawPaddedText(20, CONTENT_Y + 43, 2, C_CYAN, line, 275, C_PANEL);
    snprintf(line, sizeof(line), "Every %us  Last: %lus ago", DATA_LOG_INTERVAL_SEC, ageSec);
    drawPaddedText(20, CONTENT_Y + 62, 2, loggerReady() ? C_GREEN : C_RED, line, 275, C_PANEL);

    drawPaddedText(20, CONTENT_Y + 111, 2, C_AMBER, "/api/status", 130, C_PANEL);
    drawPaddedText(170, CONTENT_Y + 111, 2, C_AMBER, "/api/log", 120, C_PANEL);
    drawPaddedText(20, CONTENT_Y + 132, 1, C_MUTED, "CSV is streamed directly from SD card", 270, C_PANEL);
}

static void drawSystem(const SensorData &sensor, const UiStatus &status) {
    char line[96];

    if (contentDirty) {
        drawPaddedText(10, CONTENT_Y + 3, 2, C_TEXT, "System health", 300);
        drawCard(8, CONTENT_Y + 28, 148, 58, sensor.rtc_ok ? C_GREEN : C_RED);
        drawCard(164, CONTENT_Y + 28, 148, 58, sensor.bme_ok ? C_GREEN : C_RED);
        drawCard(8, CONTENT_Y + 96, 148, 58, status.sdReady ? C_GREEN : C_RED);
        drawCard(164, CONTENT_Y + 96, 148, 58, WiFi.status() == WL_CONNECTED ? C_CYAN : C_RED);
    }

    drawPaddedText(20, CONTENT_Y + 40, 1, C_MUTED, "RTC", 80, C_PANEL);
    drawPaddedText(20, CONTENT_Y + 57, 2, sensor.rtc_ok ? C_GREEN : C_RED, sensor.rtc_ok ? "OK" : "FAILED", 110, C_PANEL);
    drawPaddedText(176, CONTENT_Y + 40, 1, C_MUTED, "BME680", 80, C_PANEL);
    drawPaddedText(176, CONTENT_Y + 57, 2, sensor.bme_ok ? C_GREEN : C_RED, sensor.bme_ok ? "OK" : "FAILED", 110, C_PANEL);

    snprintf(line, sizeof(line), "%llu MB", status.sdSizeMb);
    drawPaddedText(20, CONTENT_Y + 108, 1, C_MUTED, "SD CARD", 80, C_PANEL);
    drawPaddedText(20, CONTENT_Y + 125, 2, status.sdReady ? C_GREEN : C_RED, status.sdReady ? line : "FAILED", 110, C_PANEL);

    drawPaddedText(176, CONTENT_Y + 108, 1, C_MUTED, "WIFI", 80, C_PANEL);
    drawPaddedText(176, CONTENT_Y + 125, 2, C_CYAN,
                   WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "OFFLINE",
                   110, C_PANEL);

    snprintf(line, sizeof(line), "Heap %lu   Weather %us", ESP.getFreeHeap(), WEATHER_UPDATE_INTERVAL_SEC);
    drawPaddedText(10, CONTENT_Y + 164, 1, C_MUTED, line, 300, C_BG);
}

void initUI(TFT_eSPI &display) {
    tft = &display;
    invalidateUI();
}

bool handleUI() {
    if (!tft) return false;
    if (millis() - lastTouchMs < TOUCH_DEBOUNCE_MS) return false;

    uint16_t x = 0;
    uint16_t y = 0;
    if (!tft->getTouch(&x, &y)) return false;
    if (y < TABBAR_Y || y >= TABBAR_Y + TABBAR_H) return false;

    int tab = x / (tft->width() / 4);
    if (tab < 0 || tab > 3 || tab == currentTab) return false;

    currentTab = static_cast<ScreenTab>(tab);
    lastTouchMs = millis();
    chromeDirty = true;
    contentDirty = true;
    return true;
}

void invalidateUI() {
    chromeDirty = true;
    contentDirty = true;
    forecastDirty = true;
}

void invalidateForecastUI() {
    forecastDirty = true;
    if (currentTab == TAB_FORECAST) {
        contentDirty = true;
    }
}

void drawUI(const SensorData &sensor, const WeatherData &weather, const UiStatus &status) {
    if (!tft) return;

    drawTaskbar(status);
    drawTabbar();

    if (contentDirty) {
        clearContent();
    }

    switch (currentTab) {
        case TAB_NOW:
            drawNow(sensor, weather);
            break;
        case TAB_FORECAST:
            drawForecast(weather);
            break;
        case TAB_LOG:
            drawLog(status);
            break;
        case TAB_SYSTEM:
            drawSystem(sensor, status);
            break;
    }

    chromeDirty = false;
    contentDirty = false;
}
