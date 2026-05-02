#include "UI.h"
#include "API.h"
#include "Config.h"
#include "Logger.h"
#include <WiFi.h>
#include <cmath>

static TFT_eSPI       *tft = nullptr;
static const PCData   *_pc  = nullptr;

static const unsigned long TOUCH_DEBOUNCE_MS = 250;
static const int TASKBAR_H = 28;
static const int TABBAR_Y  = 212;
static const int TABBAR_H  = 28;
static const int CONTENT_Y = 30;
static const int CONTENT_H = TABBAR_Y - CONTENT_Y;   // 182

static const uint16_t C_BG     = TFT_BLACK;
static const uint16_t C_HEADER = 0x1082;
static const uint16_t C_PANEL  = 0x18C3;
static const uint16_t C_STROKE = 0x3A6B;
static const uint16_t C_TEXT   = TFT_WHITE;
static const uint16_t C_MUTED  = 0xA514;
static const uint16_t C_CYAN   = 0x04FF;
static const uint16_t C_GREEN  = 0x07E0;
static const uint16_t C_AMBER  = 0xFD00;
static const uint16_t C_RED    = 0xF986;
static const uint16_t C_BLUE   = 0x1B9F;
static const uint16_t C_PILL   = 0x2B7F;

static const char* DOW_NAMES[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

enum ScreenTab { TAB_NOW, TAB_FORECAST, TAB_MONIT, TAB_SYSTEM };

static ScreenTab     currentTab    = TAB_NOW;
static bool          chromeDirty   = true;
static bool          contentDirty  = true;
static bool          forecastDirty = true;
static unsigned long lastTouchMs   = 0;

// ── primitives ───────────────────────────────────────────────────────────────

static void drawText(int x, int y, int font, uint16_t color,
                     const char *text, int pad, uint16_t bg = C_BG) {
    tft->setTextFont(font);
    tft->setTextColor(color, bg);
    tft->setTextPadding(pad);
    tft->drawString(text, x, y);
    tft->setTextPadding(0);
}

static void drawCard(int x, int y, int w, int h, uint16_t border = C_STROKE) {
    tft->fillRoundRect(x, y, w, h, 5, C_PANEL);
    tft->drawRoundRect(x, y, w, h, 5, border);
}

static void drawChip(int x, int y, const char *label, bool ok, uint16_t bg) {
    uint16_t col = ok ? C_GREEN : C_RED;
    tft->fillRoundRect(x, y, 34, 15, 4, bg);
    tft->drawRoundRect(x, y, 34, 15, 4, col);
    drawText(x + 5, y + 3, 1, col, label, 24, bg);
}

// ── progress bar + heat color ────────────────────────────────────────────────

static void drawBar(int x, int y, int w, int h, float frac, uint16_t col) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    tft->fillRect(x, y, w, h, C_STROKE);
    int fw = (int)(w * frac);
    if (fw > 0) tft->fillRect(x, y, fw, h, col);
}

static uint16_t heatCol(float frac) {
    if (frac > 0.8f) return C_RED;
    if (frac > 0.6f) return C_AMBER;
    return C_CYAN;
}

// ── sensor card icons ────────────────────────────────────────────────────────

static void drawIconTemp(int cx, int cy, uint16_t col) {
    tft->fillRoundRect(cx-1, cy, 3, 7, 1, col);
    tft->fillCircle(cx, cy+9, 3, col);
}

static void drawIconHum(int cx, int cy, uint16_t col) {
    tft->fillTriangle(cx, cy, cx-3, cy+6, cx+3, cy+6, col);
    tft->fillCircle(cx, cy+6, 3, col);
}

static void drawIconPress(int cx, int cy, uint16_t col) {
    tft->drawCircle(cx, cy+5, 5, col);
    tft->fillCircle(cx, cy+5, 1, col);
    tft->drawLine(cx, cy+5, cx+3, cy+2, col);
}

static void drawIconAir(int cx, int cy, uint16_t col) {
    for (int i = 0; i < 3; i++) {
        int y = cy + i * 3;
        tft->drawLine(cx-4, y,   cx-1, y,   col);
        tft->drawLine(cx-1, y,   cx+2, y+2, col);
        tft->drawLine(cx+2, y+2, cx+4, y,   col);
    }
}

static void drawIconCPU(int cx, int cy, uint16_t col) {
    tft->drawRect(cx-4, cy+2, 9, 8, col);
    tft->fillRect(cx-2, cy+4, 5, 4, col);
    tft->drawLine(cx-2, cy,    cx-2, cy+2,  col);
    tft->drawLine(cx+2, cy,    cx+2, cy+2,  col);
    tft->drawLine(cx-2, cy+10, cx-2, cy+12, col);
    tft->drawLine(cx+2, cy+10, cx+2, cy+12, col);
}

static void drawIconGPU(int cx, int cy, uint16_t col) {
    tft->drawRect(cx-5, cy+3, 11, 6, col);
    for (int i = -4; i <= 4; i += 2)
        tft->drawLine(cx+i, cy, cx+i, cy+3, col);
    tft->drawLine(cx-3, cy+9, cx-3, cy+12, col);
    tft->drawLine(cx+2, cy+9, cx+2, cy+12, col);
}

static void drawIconRAM(int cx, int cy, uint16_t col) {
    tft->drawRect(cx-5, cy+3, 11, 5, col);
    tft->fillRect(cx-4, cy+1, 2, 2, col);
    tft->fillRect(cx-1, cy+1, 2, 2, col);
    tft->fillRect(cx+2, cy+1, 2, 2, col);
    tft->drawLine(cx-3, cy+8, cx-3, cy+11, col);
    tft->drawLine(cx,   cy+8, cx,   cy+11, col);
    tft->drawLine(cx+3, cy+8, cx+3, cy+11, col);
}

// ── air quality helpers (BME680 gas resistance → label/color) ────────────────
//  Thresholds: >300 kΩ excellent, 150-300 good, 100-150 fair, 50-100 poor, <50 bad

static uint16_t airQColor(float gas) {
    if (gas >= 300) return 0x07E0;   // green     Excellent
    if (gas >= 150) return 0xAFE5;   // lime      Good
    if (gas >= 100) return 0xFFE0;   // yellow    Fair
    if (gas >=  50) return 0xFD20;   // orange    Poor
    return 0xF800;                    // red       Bad
}

static const char* airQLabel(float gas) {
    if (gas >= 300) return "Excellent";
    if (gas >= 150) return "Good";
    if (gas >= 100) return "Fair";
    if (gas >=  50) return "Poor";
    return "Bad";
}

// ── wind speed formatter (unit from Config.h) ────────────────────────────────

static void fmtWindSpeed(char *buf, size_t sz, float kmh) {
#if WIND_UNIT_MS
    snprintf(buf, sz, "%.1f m/s", kmh / 3.6f);
#else
    snprintf(buf, sz, "%.1f km/h", kmh);
#endif
}

// ── weather icon ─────────────────────────────────────────────────────────────
//
//  cx/cy = centre.  R scales all geometry (default 9 = small, 12 = large).

static void drawWeatherIcon(int cx, int cy, int code, int R = 9) {
    if (code < 0) {
        tft->drawLine(cx - R/2, cy, cx + R/2, cy, C_MUTED);
        return;
    }

    // ── Clear: filled circle + 8 rays ────────────────────────────────
    if (code == 0) {
        tft->fillCircle(cx, cy, R - 2, C_AMBER);
        for (int i = 0; i < 8; i++) {
            float a = i * M_PI / 4.0f;
            tft->drawLine(cx + (int)(R * cosf(a)),         cy + (int)(R * sinf(a)),
                          cx + (int)((R + R/2) * cosf(a)), cy + (int)((R + R/2) * sinf(a)),
                          C_AMBER);
        }
        return;
    }

    // ── Partly cloudy: small sun behind cloud ─────────────────────────
    if (code <= 2) {
        tft->fillCircle(cx - R/2, cy - R/3, R/2 + 1, C_AMBER);
    }

    // ── Cloud helper — all sizes derived from R ────────────────────────
    int cR1 = R * 2 / 3, cR2 = R * 5 / 9, cOff = R / 3;
    auto cloud = [&](int bx, int by) {
        tft->fillCircle(bx - cOff, by,     cR1, C_MUTED);
        tft->fillCircle(bx + cOff, by - 1, cR2, C_MUTED);
        tft->fillRect(  bx - R,    by,  R*2, R*2/3, C_MUTED);
    };

    if (code <= 3) { cloud(cx, cy + R/9); return; }

    cloud(cx, cy - R*2/9);
    int py = cy + R*2/3;
    int dx = R * 5 / 9;

    if (code <= 48) {
        for (int i = 1; i <= 3; i++)
            tft->drawLine(cx - R + i, py + i * R/3, cx + R - i, py + i * R/3, C_MUTED);
    } else if (code <= 67) {
        for (int i = -1; i <= 1; i++)
            tft->drawLine(cx + i*dx, py, cx + i*dx - 2, py + R*7/9, C_CYAN);
    } else if (code <= 77) {
        for (int i = -1; i <= 1; i++)
            tft->fillCircle(cx + i*dx, py + R*4/9, 2, TFT_WHITE);
    } else if (code <= 82) {
        for (int i = -2; i <= 1; i++)
            tft->fillRect(cx + i*dx + 2, py, 2, R*7/9, C_CYAN);
    } else if (code <= 86) {
        for (int i = -1; i <= 1; i++) {
            if (i == 0) tft->fillCircle(cx, py + R*4/9, 2, TFT_WHITE);
            else        tft->drawLine(cx + i*dx, py, cx + i*dx - 2, py + R*7/9, C_CYAN);
        }
    } else {
        int bx = cx + R/5, by = py;
        tft->fillTriangle(bx,      by,       bx - R/2,   by + R/2,   bx + R/5, by + R/2, C_AMBER);
        tft->fillTriangle(bx - R/5, by + R*4/9, bx - R*2/3, by + R, bx + R/3, by + R*4/9, C_AMBER);
    }
}

// ── taskbar ──────────────────────────────────────────────────────────────────

static void drawTaskbar(const SensorData &sensor, const UiStatus &status) {
    const uint16_t bg = C_HEADER;
    if (chromeDirty) {
        tft->fillRect(0, 0, tft->width(), TASKBAR_H, bg);
        tft->fillRect(0, TASKBAR_H - 2, tft->width(), 2, C_BLUE);
    }
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    drawChip(168, 7, "WiFi", wifiOk,         bg);
    drawChip(206, 7, "SD",   status.sdReady, bg);
    drawChip(244, 7, "API",  apiReady(),     bg);
    drawChip(282, 7, "LOG",  loggerReady(),  bg);
}

// ── tabbar ───────────────────────────────────────────────────────────────────

static void drawTabButton(int index, const char *label, bool active) {
    const int tabW = tft->width() / 4;
    int x = index * tabW;
    tft->fillRect(x, TABBAR_Y, tabW, TABBAR_H, C_HEADER);
    if (active) {
        tft->fillRoundRect(x + 3, TABBAR_Y + 3, tabW - 6, TABBAR_H - 6, 8, C_PILL);
    }
    tft->setTextFont(1);
    tft->setTextColor(active ? C_TEXT : C_MUTED, active ? C_PILL : C_HEADER);
    tft->setTextDatum(MC_DATUM);
    tft->setTextPadding(tabW - 10);
    tft->drawString(label, x + tabW / 2, TABBAR_Y + TABBAR_H / 2);
    tft->setTextPadding(0);
    tft->setTextDatum(TL_DATUM);
}

static void drawTabbar() {
    if (!chromeDirty) return;
    tft->fillRect(0, TABBAR_Y - 1, tft->width(), 1, C_STROKE);
    drawTabButton(0, "NOW",  currentTab == TAB_NOW);
    drawTabButton(1, "FCST", currentTab == TAB_FORECAST);
    drawTabButton(2, "MONIT", currentTab == TAB_MONIT);
    drawTabButton(3, "SYS",  currentTab == TAB_SYSTEM);
}

// ── tab: NOW ─────────────────────────────────────────────────────────────────
//
//  Row 1 (h=64)  ┌── BIG CLOCK ─────────────│── WEATHER ICON + TEMP ──┐
//                │  HH:MM:SS  (font 4)      │  icon   +18°C           │
//                │  Mon 01.05.2026          │  wind / PC brief        │
//                └──────────────────────────┴─────────────────────────┘
//
//  PC connected (below clock):
//    Row 2 (h=44)  [🌡 TEMP] [💧 HUM] [⊙ PRESS] [≋ AIR]  — 4 icon cards
//    Row 3 (h=62)  CPU ██████░░ 65°C 42%
//                  GPU ████░░░░ 72°C 18%
//                  RAM ████████ 14/16 MB
//
//  PC offline (below clock):
//    Row 2 (h=50)  ROOM TEMP  │  HUMIDITY
//    Row 3 (h=50)  PRESSURE   │  AIR

static void drawNow(const SensorData &sensor, const WeatherData &weather) {
    char line[64];
    const int CY   = CONTENT_Y;
    const int LCEN = 109;
    const int WCEN = 261;

    bool pcOn = _pc && pcFresh(*_pc);
    static bool s_pcOn = false;
    static int  cachedWeatherCode = -999;

    // ── mode switch: clear area below clock card ─────────────────────────────
    if (pcOn != s_pcOn) {
        s_pcOn = pcOn;
        tft->fillRect(0, CY + 70, tft->width(), 112, C_BG);
        contentDirty = true;
    }

    // ── clock card ───────────────────────────────────────────────────────────
    if (contentDirty) {
        drawCard(8, CY + 4, 304, 64, C_BLUE);
        tft->drawLine(209, CY + 12, 209, CY + 60, C_STROKE);
    }

    tft->setTextFont(4);
    tft->setTextColor(C_TEXT, C_PANEL);
    tft->setTextDatum(MC_DATUM);
    tft->setTextPadding(190);
    tft->drawString(sensor.timeStr + 12, LCEN, CY + 24);
    tft->setTextPadding(0);
    tft->setTextDatum(TL_DATUM);

    char dateLine[20], dateBuf[11];
    strncpy(dateBuf, sensor.timeStr, 10);
    dateBuf[10] = '\0';
    snprintf(dateLine, sizeof(dateLine), "%s  %s",
             sensor.rtc_ok ? DOW_NAMES[sensor.weekday % 7] : "--", dateBuf);
    tft->setTextFont(2);
    tft->setTextColor(C_MUTED, C_PANEL);
    tft->setTextDatum(MC_DATUM);
    tft->setTextPadding(190);
    tft->drawString(dateLine, LCEN, CY + 49);
    tft->setTextPadding(0);
    tft->setTextDatum(TL_DATUM);

    // weather icon + temp (right half of clock card)
    int currentCode = weather.ok ? weather.weather_code : -1;
    if (contentDirty || currentCode != cachedWeatherCode) {
        cachedWeatherCode = currentCode;
        tft->fillRect(211, CY + 5, 100, 46, C_PANEL);
        tft->drawLine(209, CY + 12, 209, CY + 60, C_STROKE);
        if (weather.ok) snprintf(line, sizeof(line), "%+.0fC", weather.temperature);
        else            snprintf(line, sizeof(line), "--");
        drawWeatherIcon(291, CY + 26, currentCode, 11);
        tft->setTextFont(4);
        tft->setTextColor(C_AMBER, C_PANEL);
        tft->setTextDatum(ML_DATUM);
        tft->setTextPadding(60);
        tft->drawString(line, 213, CY + 26);
        tft->setTextPadding(0);
        tft->setTextDatum(TL_DATUM);
    }

    // wind speed — always shown in bottom of clock card
    tft->setTextDatum(MC_DATUM);
    tft->setTextPadding(98);
    if (weather.ok) fmtWindSpeed(line, sizeof(line), weather.wind_speed);
    else            snprintf(line, sizeof(line), "--");
    tft->setTextFont(2);
    tft->setTextColor(C_CYAN, C_PANEL);
    tft->drawString(line, WCEN, CY + 55);
    tft->setTextPadding(0);
    tft->setTextDatum(TL_DATUM);

    if (pcOn) {
        // ── 4 env icon cards (y=CY+70, h=44) ────────────────────────────────
        //   x positions: 4 | 82 | 160 | 238   each 74px wide
        //   icon cx: +37 from card left
        //   value: MC_DATUM at (cx, CY+96)
        //   bar:   y=CY+108, h=4, w=56, x=card_x+9

        if (contentDirty) {
            drawCard(4,   CY + 70, 74, 44, C_AMBER);
            drawCard(82,  CY + 70, 74, 44, C_CYAN);
            drawCard(160, CY + 70, 74, 44, C_GREEN);
            drawIconTemp (41,  CY + 73, C_AMBER);
            drawIconHum  (119, CY + 73, C_CYAN);
            drawIconPress(197, CY + 73, C_GREEN);
        }

        static uint16_t s_airBdrNow = 0xFFFF;
        uint16_t airBdr = sensor.bme_ok ? airQColor(sensor.gas) : C_STROKE;
        if (contentDirty || airBdr != s_airBdrNow) {
            s_airBdrNow = airBdr;
            drawCard(238, CY + 70, 74, 44, airBdr);
            drawIconAir(275, CY + 73, airBdr);
        }

        // values + bars (every tick — setTextPadding handles overwrite)
        tft->setTextFont(2);
        tft->setTextDatum(MC_DATUM);

        // TEMP
        if (sensor.bme_ok) snprintf(line, sizeof(line), "%+.1fC", sensor.temperature);
        else                snprintf(line, sizeof(line), "--");
        tft->setTextColor(C_AMBER, C_PANEL);
        tft->setTextPadding(64);
        tft->drawString(line, 41, CY + 96);
        tft->setTextPadding(0);
        drawBar(13,  CY + 108, 56, 4,
                sensor.bme_ok ? (sensor.temperature + 10.0f) / 50.0f : 0.0f,
                C_AMBER);

        // HUM
        if (sensor.bme_ok) snprintf(line, sizeof(line), "%d%%", (int)sensor.humidity);
        else                snprintf(line, sizeof(line), "--");
        tft->setTextColor(C_CYAN, C_PANEL);
        tft->setTextPadding(64);
        tft->drawString(line, 119, CY + 96);
        tft->setTextPadding(0);
        drawBar(91,  CY + 108, 56, 4,
                sensor.bme_ok ? sensor.humidity / 100.0f : 0.0f,
                C_CYAN);

        // PRESS
        if (sensor.bme_ok) snprintf(line, sizeof(line), "%.0f", sensor.pressure);
        else                snprintf(line, sizeof(line), "--");
        tft->setTextColor(C_GREEN, C_PANEL);
        tft->setTextPadding(64);
        tft->drawString(line, 197, CY + 96);
        tft->setTextPadding(0);
        drawBar(169, CY + 108, 56, 4,
                sensor.bme_ok ? (sensor.pressure - 950.0f) / 100.0f : 0.0f,
                C_GREEN);

        // AIR
        if (sensor.bme_ok) {
            uint16_t ac = airQColor(sensor.gas);
            tft->setTextColor(ac, C_PANEL);
            tft->setTextPadding(64);
            tft->drawString(airQLabel(sensor.gas), 275, CY + 96);
            tft->setTextPadding(0);
            drawBar(247, CY + 108, 56, 4, sensor.gas / 300.0f, ac);
        } else {
            tft->setTextColor(C_MUTED, C_PANEL);
            tft->setTextPadding(64);
            tft->drawString("--", 275, CY + 96);
            tft->setTextPadding(0);
            tft->fillRect(247, CY + 108, 56, 4, C_STROKE);
        }

        tft->setTextDatum(TL_DATUM);

        // ── PC cards (y=CY+116/138/160, h=20) ───────────────────────────────
        //   static: card shell + icon + label  (contentDirty only — no flicker)
        //   dynamic: bar + value text          (on data change, no fill clear)
        static float    s_ct2 = -1, s_cl2 = -1;
        static float    s_gt2 = -1, s_gl2 = -1;
        static uint32_t s_ru2 = 0xFFFFFFFF;
        static uint16_t s_rc2 = 0xFFFF;

        if (contentDirty) {
            tft->fillRoundRect(8, CY+116, 304, 20, 3, C_PANEL);
            tft->drawRoundRect(8, CY+116, 304, 20, 3, C_AMBER);
            drawIconCPU(20, CY+120, C_AMBER);
            drawText(34, CY+120, 1, C_MUTED, "CPU", 28, C_PANEL);

            tft->fillRoundRect(8, CY+138, 304, 20, 3, C_PANEL);
            tft->drawRoundRect(8, CY+138, 304, 20, 3, C_CYAN);
            drawIconGPU(20, CY+142, C_CYAN);
            drawText(34, CY+142, 1, C_MUTED, "GPU", 28, C_PANEL);

            tft->fillRoundRect(8, CY+160, 304, 20, 3, C_PANEL);
            // RAM border + icon redrawn below (color is dynamic)
        }

        if (contentDirty || _pc->cpu_temp != s_ct2 || _pc->cpu_load != s_cl2) {
            s_ct2 = _pc->cpu_temp; s_cl2 = _pc->cpu_load;
            float lf = _pc->cpu_load / 100.0f;
            drawBar(68,   CY+120, 190, 8, lf, heatCol(lf));
            snprintf(line, sizeof(line), "%.0fC %.0f%%", _pc->cpu_temp, _pc->cpu_load);
            drawText(262, CY+120, 1, heatCol(lf), line, 50, C_PANEL);
        }

        if (contentDirty || _pc->gpu_temp != s_gt2 || _pc->gpu_load != s_gl2) {
            s_gt2 = _pc->gpu_temp; s_gl2 = _pc->gpu_load;
            float lf = _pc->gpu_load / 100.0f;
            drawBar(68,   CY+142, 190, 8, lf, heatCol(lf));
            snprintf(line, sizeof(line), "%.0fC %.0f%%", _pc->gpu_temp, _pc->gpu_load);
            drawText(262, CY+142, 1, heatCol(lf), line, 50, C_PANEL);
        }

        if (contentDirty || _pc->ram_used != s_ru2) {
            s_ru2 = _pc->ram_used;
            float rf = _pc->ram_total > 0
                       ? (float)_pc->ram_used / _pc->ram_total : 0.0f;
            uint16_t rc = heatCol(rf);
            if (contentDirty || rc != s_rc2) {
                s_rc2 = rc;
                tft->drawRoundRect(8, CY+160, 304, 20, 3, rc);
                tft->fillRect(14, CY+162, 12, 12, C_PANEL);
                drawIconRAM(20, CY+164, rc);
                drawText(34, CY+164, 1, C_MUTED, "RAM", 28, C_PANEL);
            }
            drawBar(68,   CY+164, 190, 8, rf, rc);
            snprintf(line, sizeof(line), "%.1f/%.0fG",
                     _pc->ram_used / 1024.0f, _pc->ram_total / 1024.0f);
            drawText(262, CY+164, 1, rc, line, 50, C_PANEL);
        }

    } else {
        // ── offline: 2×2 sensor cards ────────────────────────────────────────
        if (contentDirty) {
            drawCard(8,   CY + 72, 148, 50, C_CYAN);
            drawCard(164, CY + 72, 148, 50, C_CYAN);
            drawText(20,  CY + 80, 1, C_MUTED, "ROOM TEMP", 80, C_PANEL);
            drawText(176, CY + 80, 1, C_MUTED, "HUMIDITY",  80, C_PANEL);
        }
        if (sensor.bme_ok) snprintf(line, sizeof(line), "%+.1fC", sensor.temperature);
        else               snprintf(line, sizeof(line), "--");
        drawText(20, CY + 92, 2, C_TEXT, line, 120, C_PANEL);

        if (sensor.bme_ok) snprintf(line, sizeof(line), "%d%%", (int)sensor.humidity);
        else               snprintf(line, sizeof(line), "--");
        drawText(176, CY + 92, 2, C_TEXT, line, 120, C_PANEL);

        if (contentDirty) {
            drawCard(8,  CY + 126, 148, 50, C_GREEN);
            drawText(20, CY + 134, 1, C_MUTED, "PRESSURE", 80, C_PANEL);
        }
        if (sensor.bme_ok) snprintf(line, sizeof(line), "%.0f hPa", sensor.pressure);
        else               snprintf(line, sizeof(line), "--");
        drawText(20, CY + 146, 2, C_TEXT, line, 120, C_PANEL);

        static uint16_t s_lastAirBorder = 0xFFFF;
        uint16_t airBorder = sensor.bme_ok ? airQColor(sensor.gas) : C_STROKE;
        if (contentDirty || airBorder != s_lastAirBorder) {
            s_lastAirBorder = airBorder;
            drawCard(164, CY + 126, 148, 50, airBorder);
            drawText(176, CY + 132, 1, C_MUTED, "AIR", 40, C_PANEL);
        }
        if (sensor.bme_ok) {
            uint16_t aCol = airQColor(sensor.gas);
            tft->fillCircle(181, CY + 151, 4, aCol);
            drawText(190, CY + 143, 2, aCol, airQLabel(sensor.gas), 116, C_PANEL);
            snprintf(line, sizeof(line), "%.0f kOhm", sensor.gas);
            drawText(190, CY + 161, 1, C_MUTED, line, 110, C_PANEL);
        } else {
            tft->fillCircle(181, CY + 151, 4, C_PANEL);
            drawText(190, CY + 143, 2, C_MUTED, "--", 116, C_PANEL);
            drawText(190, CY + 161, 1, C_MUTED, "--", 110, C_PANEL);
        }
    }
}

// ── tab: FORECAST ────────────────────────────────────────────────────────────

static void drawForecast(const WeatherData &weather) {
    if (!contentDirty && !forecastDirty) return;

    const int CY = CONTENT_Y;
    tft->fillRect(0, CY, tft->width(), CONTENT_H, C_BG);
    drawText(10, CY + 4, 2, C_TEXT, "7-DAY FORECAST", 200);

    if (!weather.ok) {
        drawCard(8, CY + 32, 304, 54, C_RED);
        drawText(20, CY + 52, 2, C_MUTED, "No forecast data", 260, C_PANEL);
        forecastDirty = false;
        return;
    }

    char line[64];
    const int ROW_Y = CY + 26;
    const int ROW_H = 22;

    for (int i = 0; i < 7; i++) {
        int y = ROW_Y + i * ROW_H;
        const DayForecast &d = weather.forecast[i];
        bool today = (i == 0);

        drawCard(8, y, 304, ROW_H - 2, today ? C_AMBER : C_STROKE);
        drawText(18,  y + 4, 1, today ? C_AMBER : C_CYAN, today ? "TODAY" : d.day, 42, C_PANEL);
        snprintf(line, sizeof(line), "%+.0f / %+.0f", d.temp_min, d.temp_max);
        drawText(72,  y + 4, 1, C_TEXT,  line, 76, C_PANEL);
        drawText(158, y + 4, 1, C_MUTED, weatherDesc(d.weather_code), 130, C_PANEL);
    }

    forecastDirty = false;
}

// ── tab: MONIT ───────────────────────────────────────────────────────────────
//
//  PC connected:  CPU card / GPU card / RAM card / mini log strip
//  PC offline:    original DATA LOG + API endpoint view

static void drawMonit(const UiStatus &status) {
    const int CY = CONTENT_Y;
    char line[96];

    bool pc = _pc && pcFresh(*_pc);

    static bool s_pcWas = false;
    bool forceRedraw = contentDirty;
    if (pc != s_pcWas) {
        s_pcWas = pc;
        forceRedraw = true;
        tft->fillRect(0, CY, tft->width(), CONTENT_H, C_BG);
    }

    if (pc) {
        // ── PC MONITOR mode — bars ────────────────────────────────────────────
        const int BX = 54, BW = 210, BH = 8, VX = 268;

        if (forceRedraw) {
            drawText(10, CY + 4, 2, C_TEXT, "PC MONITOR", 140);
            drawText(156, CY + 8, 1, C_GREEN, "LIVE", 50);
        }

        // CPU card  (y=CY+22, h=54)
        // static: card + labels — forceRedraw only (no flicker on data update)
        static float s_ct = -1, s_cl = -1, s_cp = -1;
        if (forceRedraw) {
            drawCard(8, CY + 22, 304, 54, C_AMBER);
            drawText(14, CY + 25, 1, C_MUTED, "CPU",  34, C_PANEL);
            drawText(14, CY + 37, 1, C_MUTED, "TEMP", 34, C_PANEL);
            drawText(14, CY + 51, 1, C_MUTED, "LOAD", 34, C_PANEL);
        }
        if (forceRedraw || _pc->cpu_temp != s_ct || _pc->cpu_load != s_cl) {
            s_ct = _pc->cpu_temp; s_cl = _pc->cpu_load; s_cp = _pc->cpu_power;
            float tf = _pc->cpu_temp / 100.0f;
            uint16_t tc = heatCol(tf);
            drawBar(BX, CY + 37, BW, BH, tf, tc);
            snprintf(line, sizeof(line), "%.0fC", _pc->cpu_temp);
            drawText(VX, CY + 37, 1, tc, line, 38, C_PANEL);

            float lf = _pc->cpu_load / 100.0f;
            uint16_t lc = heatCol(lf);
            drawBar(BX, CY + 51, BW, BH, lf, lc);
            snprintf(line, sizeof(line), "%.0f%%", _pc->cpu_load);
            drawText(VX, CY + 51, 1, lc, line, 38, C_PANEL);

            if (_pc->cpu_power > 0) {
                snprintf(line, sizeof(line), "%.0f W", _pc->cpu_power);
                drawText(14, CY + 63, 1, C_MUTED, line, 60, C_PANEL);
            } else {
                tft->fillRect(14, CY + 63, 60, 8, C_PANEL);
            }
        }

        // GPU card  (y=CY+80, h=54)
        static float s_gt = -1, s_gl = -1;
        if (forceRedraw) {
            drawCard(8, CY + 80, 304, 54, C_CYAN);
            drawText(14, CY + 83,  1, C_MUTED, "GPU",  34, C_PANEL);
            drawText(14, CY + 95,  1, C_MUTED, "TEMP", 34, C_PANEL);
            drawText(14, CY + 109, 1, C_MUTED, "LOAD", 34, C_PANEL);
        }
        if (forceRedraw || _pc->gpu_temp != s_gt || _pc->gpu_load != s_gl) {
            s_gt = _pc->gpu_temp; s_gl = _pc->gpu_load;
            float tf = _pc->gpu_temp / 100.0f;
            uint16_t tc = heatCol(tf);
            drawBar(BX, CY + 95, BW, BH, tf, tc);
            snprintf(line, sizeof(line), "%.0fC", _pc->gpu_temp);
            drawText(VX, CY + 95, 1, tc, line, 38, C_PANEL);

            float lf = _pc->gpu_load / 100.0f;
            uint16_t lc = heatCol(lf);
            drawBar(BX, CY + 109, BW, BH, lf, lc);
            snprintf(line, sizeof(line), "%.0f%%", _pc->gpu_load);
            drawText(VX, CY + 109, 1, lc, line, 38, C_PANEL);

            if (_pc->gpu_vram_total > 0) {
                snprintf(line, sizeof(line), "VRAM %u / %u MB",
                         _pc->gpu_vram_used, _pc->gpu_vram_total);
                drawText(14, CY + 121, 1, C_MUTED, line, 150, C_PANEL);
            } else {
                tft->fillRect(14, CY + 121, 150, 8, C_PANEL);
            }
        }

        // RAM card  (y=CY+138, h=40)
        // border color is dynamic — redrawn only when color threshold changes
        static uint32_t s_ru  = 0;
        static uint16_t s_rbc = 0xFFFF;
        if (forceRedraw || _pc->ram_used != s_ru) {
            s_ru = _pc->ram_used;
            float rf = _pc->ram_total > 0
                       ? (float)_pc->ram_used / _pc->ram_total : 0.0f;
            uint16_t rc = heatCol(rf);
            if (forceRedraw || rc != s_rbc) {
                s_rbc = rc;
                tft->fillRoundRect(8, CY + 138, 304, 40, 5, C_PANEL);
                tft->drawRoundRect(8, CY + 138, 304, 40, 5, rc);
                drawText(14, CY + 141, 1, C_MUTED, "RAM", 34, C_PANEL);
            }
            drawBar(BX, CY + 141, BW, BH, rf, rc);
            snprintf(line, sizeof(line), "%u%%", (uint8_t)(rf * 100));
            drawText(VX, CY + 141, 1, rc, line, 38, C_PANEL);
            snprintf(line, sizeof(line), "%lu / %lu MB",
                     (unsigned long)_pc->ram_used, (unsigned long)_pc->ram_total);
            drawText(14, CY + 153, 2, rc, line, 200, C_PANEL);
        }

    } else {
        // ── DATA LOG mode (PC offline) ────────────────────────────────────────
        unsigned long ageSec = status.lastLogWriteMs
                             ? (millis() - status.lastLogWriteMs) / 1000UL : 0;
        if (forceRedraw) {
            drawText(10, CY + 4, 2, C_TEXT, "DATA LOG", 200);
            drawCard(8,  CY + 26,  304, 54, loggerReady() ? C_GREEN : C_RED);
            drawCard(8,  CY + 86,  304, 46, C_BLUE);
            drawCard(8,  CY + 138, 304, 36, C_STROKE);
            drawText(20, CY + 91, 1, C_MUTED, "API ENDPOINTS", 100, C_PANEL);
        }
        drawText(20, CY + 34, 2, C_CYAN, readingsLogPath(), 270, C_PANEL);
        snprintf(line, sizeof(line), "Every %us  Last: %lus ago",
                 DATA_LOG_INTERVAL_SEC, ageSec);
        drawText(20, CY + 55, 1, loggerReady() ? C_GREEN : C_RED, line, 270, C_PANEL);

        drawText(20,  CY + 103, 2, C_AMBER, "/api/status", 130, C_PANEL);
        drawText(164, CY + 103, 2, C_AMBER, "/api/log",    118, C_PANEL);

        snprintf(line, sizeof(line), "http://" DEVICE_NAME ".local/   %s",
                 WiFi.localIP().toString().c_str());
        drawText(20, CY + 146, 1, C_MUTED, line, 270, C_PANEL);
    }
}

// ── tab: SYSTEM ──────────────────────────────────────────────────────────────
//
//  y=CY+26  ┌── HARDWARE (h=50) ──────────────────────────────────┐
//           │ RTC     Mon 01.05.2026  14:23:45                    │
//           │ BME680  +22.1C  48%  1013 hPa  120 kOhm            │
//           │ SD      /readings.csv   32768 MB                    │
//           └────────────────────────────────────────────────────┘
//  y=CY+82  ┌── WIFI (h=44) ──────────────────────────────────────┐
//           └────────────────────────────────────────────────────┘
//  y=CY+132 ┌── DEVICE INFO (h=44) ──────────────────────────────┐
//           └────────────────────────────────────────────────────┘

static void drawSystem(const SensorData &sensor, const UiStatus &status) {
    const int CY = CONTENT_Y;
    char line[96];

    static uint16_t lastHwBorder = 0xFFFF;
    static uint16_t lastWfBorder = 0xFFFF;

    if (contentDirty) {
        drawText(10, CY + 4, 2, C_TEXT, "SYSTEM", 200);
        drawCard(8, CY + 132, 304, 44, C_STROKE);
    }

    // ── Hardware card ─────────────────────────────────────────────
    uint16_t hwBorder = (sensor.rtc_ok && sensor.bme_ok && status.sdReady)
                        ? C_GREEN : C_RED;
    if (contentDirty || hwBorder != lastHwBorder) {
        lastHwBorder = hwBorder;
        drawCard(8, CY + 26, 304, 50, hwBorder);
        drawText(20, CY + 34, 1, C_MUTED, "RTC",    46, C_PANEL);
        drawText(20, CY + 44, 1, C_MUTED, "BME680", 46, C_PANEL);
        drawText(20, CY + 54, 1, C_MUTED, "SD",     46, C_PANEL);
    }

    // RTC row
    if (sensor.rtc_ok) {
        snprintf(line, sizeof(line), "%s  %s",
                 DOW_NAMES[sensor.weekday % 7], sensor.timeStr);
        drawText(70, CY + 34, 1, C_GREEN, line, 230, C_PANEL);
    } else {
        drawText(70, CY + 34, 1, C_RED, "OFFLINE", 230, C_PANEL);
    }

    // BME680 row
    if (sensor.bme_ok) {
        snprintf(line, sizeof(line), "%+.1fC  %d%%  %.0f hPa  %.0f kOhm",
                 sensor.temperature, (int)sensor.humidity,
                 sensor.pressure, sensor.gas);
        drawText(70, CY + 44, 1, C_GREEN, line, 230, C_PANEL);
    } else {
        drawText(70, CY + 44, 1, C_RED, "OFFLINE", 230, C_PANEL);
    }

    // SD row
    if (status.sdReady) {
        snprintf(line, sizeof(line), "%s   %llu MB", readingsLogPath(), status.sdSizeMb);
        drawText(70, CY + 54, 1, C_GREEN, line, 230, C_PANEL);
    } else {
        drawText(70, CY + 54, 1, C_RED, "OFFLINE", 230, C_PANEL);
    }

    // ── WiFi card ─────────────────────────────────────────────────
    uint16_t wfBorder = WiFi.status() == WL_CONNECTED ? C_CYAN : C_RED;
    if (contentDirty || wfBorder != lastWfBorder) {
        lastWfBorder = wfBorder;
        drawCard(8, CY + 82, 304, 44, wfBorder);
        drawText(20, CY + 89, 1, C_MUTED, "WIFI", 40, C_PANEL);
    }

    if (WiFi.status() == WL_CONNECTED) {
        snprintf(line, sizeof(line), DEVICE_NAME ".local    %s",
                 WiFi.localIP().toString().c_str());
        drawText(64, CY + 87, 2, C_CYAN, line, 238, C_PANEL);
    } else {
        drawText(64, CY + 87, 2, C_RED, "OFFLINE", 238, C_PANEL);
    }

    // ── Device info card ──────────────────────────────────────────
    unsigned long upSec = millis() / 1000;
    snprintf(line, sizeof(line), "Heap %lu B     Up %luh %02um",
             ESP.getFreeHeap(), upSec / 3600, (upSec % 3600) / 60);
    drawText(20, CY + 140, 1, C_MUTED, line, 270, C_PANEL);

    snprintf(line, sizeof(line), "Weather %us    Log %us    Wind %s",
             WEATHER_UPDATE_INTERVAL_SEC, DATA_LOG_INTERVAL_SEC,
             WIND_UNIT_MS ? "m/s" : "km/h");
    drawText(20, CY + 153, 1, C_MUTED, line, 270, C_PANEL);
}

// ── public API ───────────────────────────────────────────────────────────────

void initUI(TFT_eSPI &display) {
    tft = &display;
    invalidateUI();
}

void initPCDisplay(const PCData *pc) {
    _pc = pc;
}

bool handleUI() {
    if (!tft) return false;
    if (millis() - lastTouchMs < TOUCH_DEBOUNCE_MS) return false;

    uint16_t x = 0, y = 0;
    if (!tft->getTouch(&x, &y)) return false;
    if (y < TABBAR_Y || y >= TABBAR_Y + TABBAR_H) return false;

    int tab = x / (tft->width() / 4);
    if (tab > 3 || tab == (int)currentTab) return false;

    currentTab   = static_cast<ScreenTab>(tab);
    lastTouchMs  = millis();
    chromeDirty  = true;
    contentDirty = true;
    return true;
}

void invalidateUI() {
    chromeDirty   = true;
    contentDirty  = true;
    forecastDirty = true;
}

void invalidateForecastUI() {
    forecastDirty = true;
    if (currentTab == TAB_FORECAST) contentDirty = true;
}

void drawUI(const SensorData &sensor, const WeatherData &weather, const UiStatus &status) {
    if (!tft) return;

    drawTaskbar(sensor, status);
    drawTabbar();

    if (contentDirty) {
        tft->fillRect(0, CONTENT_Y, tft->width(), CONTENT_H, C_BG);
    }

    switch (currentTab) {
        case TAB_NOW:      drawNow(sensor, weather);   break;
        case TAB_FORECAST: drawForecast(weather);       break;
        case TAB_MONIT:    drawMonit(status);             break;
        case TAB_SYSTEM:   drawSystem(sensor, status);  break;
    }

    chromeDirty  = false;
    contentDirty = false;
}
