#include "UI.h"
#include "API.h"
#include "Config.h"
#include "Logger.h"
#include "RuntimeSettings.h"
#include <WiFi.h>

// ── Константы Layout'а и Цветов ──────────────────────────────────────────────
static const unsigned long TOUCH_DEBOUNCE_MS = 250;
static const int TASKBAR_H = 22;                     
static const int TABBAR_Y  = 212;
static const int TABBAR_H  = 28;
static const int CONTENT_Y = 22;                     
static const int CONTENT_H = TABBAR_Y - CONTENT_Y;   // 190

static const uint16_t C_BG     = 0x0801; // #080d17
static const uint16_t C_HEADER = 0x1083; // #101827
static const uint16_t C_PANEL  = 0x1105; // #111a2a
static const uint16_t C_STROKE = 0x21E9; // #20324f
static const uint16_t C_BAR_BG = 0x2A8A; // #263551
static const uint16_t C_TEXT   = 0xEFFF; // #edf5ff
static const uint16_t C_MUTED  = 0x7C96; // #7890b3
static const uint16_t C_CYAN   = 0x367F; // #38c7ff
static const uint16_t C_VIOLET = 0xA45F; // #a884ff
static const uint16_t C_GREEN  = 0x474F; // #43e884
static const uint16_t C_AMBER  = 0xFDC5; // #ffbd2e
static const uint16_t C_ORANGE = 0xFC85; // #ff922e
static const uint16_t C_RED    = 0xFAAB; // #ff4f5f
static const uint16_t C_BLUE   = 0x367F;
static const uint16_t C_PILL   = 0x1948; // #1a2a44

static const char* DOW_NAMES[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

enum ScreenTab { TAB_NOW = 0, TAB_FORECAST, TAB_MONIT, TAB_SYSTEM };

// ── Главный класс управления UI ──────────────────────────────────────────────
class UIManager {
private:
    TFT_eSPI* tft;
    const PCData* _pc;
    
    ScreenTab currentTab;
    bool chromeDirty;
    bool contentDirty;
    bool forecastDirty;
    unsigned long lastTouchMs;

    struct StateCache {
        bool pcOn = false;
        int weatherCode = -999;
        bool weatherIsDay = true;
        float weatherTemp = -999.0f; // Добавлен кеш температуры погоды
        uint16_t airBorderNow = 0xFFFF;
        uint16_t airBorderOffline = 0xFFFF;
        
        float cpuTemp = -1, cpuLoad = -1;
        float gpuTemp = -1, gpuLoad = -1;
        uint32_t ramUsed = 0xFFFFFFFF;
        uint16_t ramBorder = 0xFFFF;
        
        bool monitPcWas = false;
        uint16_t hwBorder = 0xFFFF;
        uint16_t wfBorder = 0xFFFF;
        uint8_t sysFlags = 0xFF;
    } cache;

    // ── Примитивы отрисовки ──────────────────────────────────────────────────
    void drawText(int x, int y, int font, uint16_t color, const char *text, int pad, uint16_t bg = C_BG) {
        tft->setTextFont(font);
        tft->setTextColor(color, bg);
        tft->setTextPadding(pad);
        tft->drawString(text, x, y);
        tft->setTextPadding(0);
    }

    void drawCard(int x, int y, int w, int h, uint16_t border = C_STROKE) {
        tft->fillRoundRect(x, y, w, h, 7, C_PANEL);
        tft->drawRoundRect(x, y, w, h, 7, C_STROKE);
        if (border != C_STROKE) tft->fillRect(x + 2, y + 1, w - 4, 2, border);
    }

    void drawStatusIcon(int x, int y, uint16_t col, uint8_t kind) {
        tft->fillRect(x, y, 14, 14, C_HEADER);
        switch (kind) {
            case 0: // wifi
                tft->fillCircle(x + 7, y + 10, 1, col);
                tft->drawLine(x + 4, y + 9, x + 10, y + 9, col);
                tft->drawLine(x + 3, y + 7, x + 11, y + 7, col);
                tft->drawLine(x + 5, y + 5, x + 9, y + 5, col);
                break;
            case 1: // sd
                tft->fillRect(x + 4, y + 3, 6, 8, col);
                tft->fillRect(x + 5, y + 5, 4, 1, C_HEADER);
                tft->fillRect(x + 6, y + 8, 2, 2, C_HEADER);
                break;
            case 2: // api
                tft->fillRect(x + 6, y + 3, 2, 8, col);
                tft->fillRect(x + 3, y + 6, 8, 2, col);
                break;
            default: // log
                tft->fillRect(x + 4, y + 3, 6, 8, col);
                tft->fillRect(x + 5, y + 5, 4, 1, C_HEADER);
                tft->fillRect(x + 5, y + 7, 4, 1, C_HEADER);
                tft->fillRect(x + 5, y + 9, 3, 1, C_HEADER);
                break;
        }
    }

    void drawBar(int x, int y, int w, int h, float frac, uint16_t col) {
        frac = constrain(frac, 0.0f, 1.0f);
        tft->fillRoundRect(x, y, w, h, h / 2, C_BAR_BG);
        int fw = (int)(w * frac);
        if (fw > 0) tft->fillRoundRect(x, y, fw, h, h / 2, col);
    }

    uint16_t heatCol(float frac) {
        if (frac > 0.8f) return C_RED;
        if (frac > 0.6f) return C_AMBER;
        return C_CYAN;
    }

    uint16_t tempColor(float v) {
        if (v < 18.0f) return C_CYAN;
        if (v < 26.0f) return C_GREEN;
        if (v < 30.0f) return C_AMBER;
        return C_RED;
    }

    uint16_t humColor(float v) {
        if (v < 30.0f) return C_AMBER;
        if (v < 60.0f) return C_GREEN;
        if (v < 70.0f) return C_VIOLET;
        return C_RED;
    }

    uint16_t pressColor(float v) {
        if (v < 990.0f) return C_AMBER;
        if (v > 1025.0f) return C_AMBER;
        return C_GREEN;
    }

    // ── Иконки ───────────────────────────────────────────────────────────────
    void drawIconTemp(int cx, int cy, uint16_t col) { tft->fillRoundRect(cx-1, cy, 3, 7, 1, col); tft->fillCircle(cx, cy+9, 3, col); }
    void drawIconHum(int cx, int cy, uint16_t col) { tft->fillTriangle(cx, cy, cx-3, cy+6, cx+3, cy+6, col); tft->fillCircle(cx, cy+6, 3, col); }
    void drawIconPress(int cx, int cy, uint16_t col) { tft->drawCircle(cx, cy+5, 5, col); tft->fillCircle(cx, cy+5, 1, col); tft->drawLine(cx, cy+5, cx+3, cy+2, col); }
    void drawIconAir(int cx, int cy, uint16_t col) { for (int i=0; i<3; i++) { int y=cy+i*3; tft->drawLine(cx-4,y,cx-1,y,col); tft->drawLine(cx-1,y,cx+2,y+2,col); tft->drawLine(cx+2,y+2,cx+4,y,col); } }
    void drawIconCPU(int cx, int cy, uint16_t col) { tft->drawRect(cx-4, cy+2, 9, 8, col); tft->fillRect(cx-2, cy+4, 5, 4, col); tft->drawLine(cx-2, cy, cx-2, cy+2, col); tft->drawLine(cx+2, cy, cx+2, cy+2, col); tft->drawLine(cx-2, cy+10, cx-2, cy+12, col); tft->drawLine(cx+2, cy+10, cx+2, cy+12, col); }
    void drawIconGPU(int cx, int cy, uint16_t col) { tft->drawRect(cx-5, cy+3, 11, 6, col); for (int i=-4; i<=4; i+=2) tft->drawLine(cx+i, cy, cx+i, cy+3, col); tft->drawLine(cx-3, cy+9, cx-3, cy+12, col); tft->drawLine(cx+2, cy+9, cx+2, cy+12, col); }
    void drawIconRAM(int cx, int cy, uint16_t col) { tft->drawRect(cx-5, cy+3, 11, 5, col); tft->fillRect(cx-4, cy+1, 2, 2, col); tft->fillRect(cx-1, cy+1, 2, 2, col); tft->fillRect(cx+2, cy+1, 2, 2, col); tft->drawLine(cx-3, cy+8, cx-3, cy+11, col); tft->drawLine(cx, cy+8, cx, cy+11, col); tft->drawLine(cx+3, cy+8, cx+3, cy+11, col); }

    // ── Хелперы ──────────────────────────────────────────────────────────────
    uint16_t airQColor(float gas) {
        if (gas >= 300) return C_GREEN;
        if (gas >= 150) return C_GREEN;
        if (gas >= 100) return C_AMBER;
        if (gas >=  50) return C_RED;
        return C_VIOLET;
    }

    const char* airQLabel(float gas) {
        if (gas >= 300) return "Great"; 
        if (gas >= 150) return "Good";
        if (gas >= 100) return "Fair";
        if (gas >=  50) return "Poor";
        return "Bad";
    }

    void fmtWindSpeed(char *buf, size_t sz, float kmh) {
        if (RuntimeSettings::windMetric()) snprintf(buf, sz, "%.1f m/s", kmh / 3.6f);
        else snprintf(buf, sz, "%.1f km/h", kmh);
    }

    void drawSun(int cx, int cy, int R) {
        tft->fillCircle(cx, cy, R - 2, C_AMBER);
        const float dx[] = {1.0f, 0.707f, 0.0f, -0.707f, -1.0f, -0.707f, 0.0f, 0.707f};
        const float dy[] = {0.0f, 0.707f, 1.0f, 0.707f, 0.0f, -0.707f, -1.0f, -0.707f};
        for (int i = 0; i < 8; i++) {
            tft->drawLine(cx + (int)(R*dx[i]), cy + (int)(R*dy[i]), cx + (int)((R+R/2)*dx[i]), cy + (int)((R+R/2)*dy[i]), C_AMBER);
        }
    }

    void drawMoon(int cx, int cy, int R) {
        tft->fillCircle(cx, cy, R - 2, C_TEXT);
        tft->fillCircle(cx + R / 3, cy - R / 4, R - 2, C_PANEL);
    }

    void drawWeatherIcon(int cx, int cy, int code, bool isDay, int R = 9) {
        tft->fillRect(cx - R * 2, cy - R * 2, R * 4, R * 4, C_PANEL);
        if (code < 0) { tft->drawLine(cx - R/2, cy, cx + R/2, cy, C_MUTED); return; }
        if (code == 0) {
            if (isDay) drawSun(cx, cy, R);
            else       drawMoon(cx, cy, R);
            return;
        }
        if (code <= 2) {
            if (isDay) drawSun(cx - R/2, cy - R/3, R/2 + 3);
            else       drawMoon(cx - R/2, cy - R/3, R/2 + 3);
        }
        int cR1 = R * 2 / 3, cR2 = R * 5 / 9, cOff = R / 3;
        auto cloud = [&](int bx, int by) {
            tft->fillCircle(bx - cOff, by, cR1, C_MUTED);
            tft->fillCircle(bx + cOff, by - 1, cR2, C_MUTED);
            tft->fillRect(bx - R, by, R*2, R*2/3, C_MUTED);
        };
        if (code <= 3) { cloud(cx, cy + R/9); return; }
        cloud(cx, cy - R*2/9);
        int py = cy + R*2/3, dx_cloud = R * 5 / 9;
        if (code <= 48) { for (int i=1; i<=3; i++) tft->drawLine(cx-R+i, py+i*R/3, cx+R-i, py+i*R/3, C_MUTED); }
        else if (code <= 67) { for (int i=-1; i<=1; i++) tft->drawLine(cx+i*dx_cloud, py, cx+i*dx_cloud-2, py+R*7/9, C_CYAN); }
        else if (code <= 77) { for (int i=-1; i<=1; i++) tft->fillCircle(cx+i*dx_cloud, py+R*4/9, 2, TFT_WHITE); }
        else if (code <= 82) { for (int i=-2; i<=1; i++) tft->fillRect(cx+i*dx_cloud+2, py, 2, R*7/9, C_CYAN); }
        else if (code <= 86) { for (int i=-1; i<=1; i++) { if (i==0) tft->fillCircle(cx, py+R*4/9, 2, TFT_WHITE); else tft->drawLine(cx+i*dx_cloud, py, cx+i*dx_cloud-2, py+R*7/9, C_CYAN); } }
        else {
            int bx = cx+R/5, by = py;
            tft->fillTriangle(bx, by, bx-R/2, by+R/2, bx+R/5, by+R/2, C_AMBER);
            tft->fillTriangle(bx-R/5, by+R*4/9, bx-R*2/3, by+R, bx+R/3, by+R*4/9, C_AMBER);
        }
    }

    // ── Отрисовка элементов UI ───────────────────────────────────────────────
    void drawTaskbar(const SensorData &sensor, const UiStatus &status) {
        const uint16_t bg = C_HEADER;
        if (chromeDirty) {
            tft->fillRect(0, 0, tft->width(), TASKBAR_H, bg);
            tft->fillRect(0, TASKBAR_H - 1, tft->width(), 1, C_STROKE);
        }
        char dateLine[40];
        if (currentTab == TAB_NOW) {
            snprintf(dateLine, sizeof(dateLine), "%s  %.10s", sensor.rtc_ok ? DOW_NAMES[sensor.weekday % 7] : "--", sensor.timeStr);
        } else {
            snprintf(dateLine, sizeof(dateLine), "%s  %.10s  %.5s",
                     sensor.rtc_ok ? DOW_NAMES[sensor.weekday % 7] : "--",
                     sensor.timeStr,
                     sensor.timeStr + 12);
        }
        drawText(8, 5, 2, C_MUTED, dateLine, 220, C_HEADER);
        bool wifiOk = (WiFi.status() == WL_CONNECTED);
        int right = tft->width();
        drawStatusIcon(right - 74, 4,  wifiOk ? C_GREEN : C_RED, 0);
        drawStatusIcon(right - 56, 4,  status.sdReady ? C_GREEN : C_RED, 1);
        drawStatusIcon(right - 38, 4,  apiReady() ? C_GREEN : C_RED, 2);
        drawStatusIcon(right - 20, 4,  loggerReady() ? C_GREEN : C_RED, 3);
    }

    void drawTabbar() {
        if (!chromeDirty) return;
        tft->fillRect(0, TABBAR_Y - 1, tft->width(), 1, C_STROKE);
        const char* labels[] = {"NOW", "FCST", "MONIT", "SYS"};
        const int tabW = tft->width() / 4;
        for (int i = 0; i < 4; i++) {
            bool active = (currentTab == static_cast<ScreenTab>(i));
            int x = i * tabW;
            tft->fillRect(x, TABBAR_Y, tabW, TABBAR_H, C_HEADER);
            if (active) tft->fillRoundRect(x + 3, TABBAR_Y + 3, tabW - 6, TABBAR_H - 6, 8, C_PILL);
            tft->setTextFont(1);
            tft->setTextColor(active ? C_TEXT : C_MUTED, active ? C_PILL : C_HEADER);
            tft->setTextDatum(MC_DATUM);
            tft->setTextPadding(tabW - 10);
            tft->drawString(labels[i], x + tabW / 2, TABBAR_Y + TABBAR_H / 2);
        }
        tft->setTextPadding(0);
        tft->setTextDatum(TL_DATUM);
    }

    void drawNowTab(const SensorData &sensor, const WeatherData &weather) {
        char line[64];
        const int CY   = CONTENT_Y;
        const int SW   = tft->width();
        const int CLOCK_CX = 108;

        bool pcOn = _pc && pcFresh(*_pc);
        if (pcOn != cache.pcOn) {
            cache.pcOn = pcOn;
            tft->fillRect(0, CY + 66, SW, 124, C_BG);
            contentDirty = true;
        }

        // 1. Блок ЧАСОВ
        if (contentDirty) {
            drawCard(8, CY + 2, SW - 16, 64, C_BLUE);
            tft->drawLine(209, CY + 10, 209, CY + 56, C_STROKE);
        }

        tft->setTextFont(6); 
        tft->setTextColor(C_TEXT, C_PANEL);
        tft->setTextDatum(MC_DATUM);
        tft->setTextPadding(190);
        tft->drawString(sensor.timeStr + 12, CLOCK_CX, CY + 40); 
        
        int currentCode = weather.ok ? weather.weather_code : -1;
        bool currentIsDay = weather.ok ? weather.is_day : true;
        bool weatherUpdated = contentDirty || currentCode != cache.weatherCode ||
                              currentIsDay != cache.weatherIsDay ||
                              weather.temperature != cache.weatherTemp;
        if (weatherUpdated) {
            cache.weatherCode = currentCode;
            cache.weatherTemp = weather.temperature;
            cache.weatherIsDay = currentIsDay;

            tft->fillRoundRect(206, CY + 2, 106, 64, 7, C_PANEL);
            tft->drawRoundRect(8, CY + 2, SW - 16, 64, 7, C_STROKE);
            tft->drawLine(209, CY + 10, 209, CY + 56, C_STROKE);
            tft->fillRect(208, CY + 3, 102, 2, C_BLUE);

            tft->setTextPadding(0); // сброс унаследованного padding от часов
            tft->setTextFont(2);
            tft->setTextColor(C_MUTED, C_PANEL);
            tft->setTextDatum(MC_DATUM);
            String city = RuntimeSettings::weatherCity();
            tft->drawString(city.c_str(), 255, CY + 14);

            tft->setTextFont(4);
            tft->setTextColor(C_AMBER, C_PANEL);
            tft->setTextDatum(MR_DATUM);
            tft->setTextPadding(0);

            if (weather.ok) {
                char tNum[16];
                snprintf(tNum, sizeof(tNum), "%+.0f", weather.temperature);
                const int tempRight = SW - 62;
                tft->drawString(tNum, tempRight, CY + 37);

                int circX = tempRight + 4;
                int circY = CY + 29;
                tft->drawCircle(circX, circY, 3, C_AMBER);
                tft->drawCircle(circX, circY, 2, C_AMBER);
            } else {
                tft->drawString("--", SW - 84, CY + 37);
            }
        }

        // Скорость ветра — всегда, но ПЕРЕД иконкой чтобы не срезать лучи
        tft->setTextDatum(MR_DATUM);
        tft->setTextPadding(48);
        if (weather.ok) fmtWindSpeed(line, sizeof(line), weather.wind_speed);
        else            snprintf(line, sizeof(line), "--");
        tft->setTextFont(2);
        tft->setTextColor(C_CYAN, C_PANEL);
        tft->drawString(line, 272, CY + 53);
        tft->setTextPadding(0);
        tft->setTextDatum(TL_DATUM);

        // Иконка каждый кадр — поверх фона текста скорости ветра
        drawWeatherIcon(SW - 29, CY + 40, currentCode, currentIsDay, 10);

        // 2. Блок ПОКАЗАНИЙ
        if (pcOn) {
            int w = 73, gap = 4;
            int c1 = 8, c2 = c1+w+gap, c3 = c2+w+gap, c4 = c3+w+gap;
            int cx1 = c1+w/2, cx2 = c2+w/2, cx3 = c3+w/2, cx4 = c4+w/2; 
            uint16_t tempCol = sensor.bme_ok ? tempColor(sensor.temperature) : C_STROKE;
            uint16_t humCol = sensor.bme_ok ? humColor(sensor.humidity) : C_STROKE;
            uint16_t pressCol = sensor.bme_ok ? pressColor(sensor.pressure) : C_STROKE;
            
            if (contentDirty) {
                drawCard(c1, CY + 70, w, 46, C_CYAN);
                drawIconTemp(cx1, CY + 77, C_CYAN);
            }
            if (contentDirty) {
                drawCard(c2, CY + 70, w, 46, C_VIOLET);
                drawIconHum(cx2, CY + 77, C_VIOLET);
            }
            if (contentDirty) {
                drawCard(c3, CY + 70, w, 46, C_GREEN);
                drawIconPress(cx3, CY + 77, C_GREEN);
            }

            uint16_t airBdr = sensor.bme_ok ? airQColor(sensor.gas) : C_STROKE;
            if (contentDirty) {
                cache.airBorderNow = airBdr;
                drawCard(c4, CY + 70, w, 46, C_ORANGE);
                drawIconAir(cx4, CY + 77, C_ORANGE);
            }

            tft->setTextFont(2);
            tft->setTextDatum(MC_DATUM);
            tft->setTextPadding(50); 

            snprintf(line, sizeof(line), sensor.bme_ok ? "%.1fC" : "--", sensor.temperature);
            tft->setTextColor(tempCol, C_PANEL);
            tft->drawString(line, cx1, CY + 98);
            drawBar(cx1-25, CY + 110, 50, 4, sensor.bme_ok ? (sensor.temperature + 10.0f) / 50.0f : 0.0f, tempCol);

            snprintf(line, sizeof(line), sensor.bme_ok ? "%d%%" : "--", (int)sensor.humidity);
            tft->setTextColor(humCol, C_PANEL);
            tft->drawString(line, cx2, CY + 98);
            drawBar(cx2-25, CY + 110, 50, 4, sensor.bme_ok ? sensor.humidity / 100.0f : 0.0f, humCol);

            snprintf(line, sizeof(line), sensor.bme_ok ? "%.0f" : "--", sensor.pressure);
            tft->setTextColor(pressCol, C_PANEL);
            tft->drawString(line, cx3, CY + 98);
            drawBar(cx3-25, CY + 110, 50, 4, sensor.bme_ok ? (sensor.pressure - 950.0f) / 100.0f : 0.0f, pressCol);

            if (sensor.bme_ok) {
                uint16_t ac = airQColor(sensor.gas);
                tft->setTextColor(ac, C_PANEL);
                tft->drawString(airQLabel(sensor.gas), cx4, CY + 98);
                drawBar(cx4-25, CY + 110, 50, 4, sensor.gas / 300.0f, ac);
            } else {
                tft->setTextColor(C_MUTED, C_PANEL);
                tft->drawString("--", cx4, CY + 98);
                tft->fillRect(cx4-25, CY + 110, 50, 4, C_STROKE);
            }
            tft->setTextPadding(0);
            tft->setTextDatum(TL_DATUM);

            // Нижняя карточка ПК (строго в ряд)
            int row1 = CY + 132;
            int row2 = CY + 152;
            int row3 = CY + 172;

            if (contentDirty) {
                drawCard(8, CY + 120, SW - 16, 64, C_STROKE);
                drawIconCPU(22, row1 - 6, C_AMBER);
                drawText(36, row1 - 4, 1, C_MUTED, "CPU", 28, C_PANEL);
                drawIconGPU(22, row2 - 6, C_CYAN);
                drawText(36, row2 - 4, 1, C_MUTED, "GPU", 28, C_PANEL);
            }

            int barX = 66, barW = 150, textX = 226;

            if (contentDirty || _pc->cpu_temp != cache.cpuTemp || _pc->cpu_load != cache.cpuLoad) {
                cache.cpuTemp = _pc->cpu_temp; cache.cpuLoad = _pc->cpu_load;
                float lf = _pc->cpu_load / 100.0f;
                drawBar(barX, row1 - 3, barW, 6, lf, heatCol(lf));
                char loadLine[12];
                char tempLine[12];
                snprintf(loadLine, sizeof(loadLine), "%.0f%%", _pc->cpu_load);
                snprintf(tempLine, sizeof(tempLine), "%.0fC", _pc->cpu_temp);
                tft->setTextFont(1);
                tft->setTextColor(C_TEXT, C_PANEL);
                tft->setTextDatum(TR_DATUM);
                tft->setTextPadding(28);
                tft->drawString(loadLine, 250, row1 - 4);
                tft->setTextColor(heatCol(lf), C_PANEL);
                tft->setTextDatum(TL_DATUM);
                tft->setTextPadding(34);
                tft->drawString(tempLine, 256, row1 - 4);
            }

            if (contentDirty || _pc->gpu_temp != cache.gpuTemp || _pc->gpu_load != cache.gpuLoad) {
                cache.gpuTemp = _pc->gpu_temp; cache.gpuLoad = _pc->gpu_load;
                float lf = _pc->gpu_load / 100.0f;
                drawBar(barX, row2 - 3, barW, 6, lf, heatCol(lf));
                char loadLine[12];
                char tempLine[12];
                snprintf(loadLine, sizeof(loadLine), "%.0f%%", _pc->gpu_load);
                snprintf(tempLine, sizeof(tempLine), "%.0fC", _pc->gpu_temp);
                tft->setTextFont(1);
                tft->setTextColor(C_TEXT, C_PANEL);
                tft->setTextDatum(TR_DATUM);
                tft->setTextPadding(28);
                tft->drawString(loadLine, 250, row2 - 4);
                tft->setTextColor(heatCol(lf), C_PANEL);
                tft->setTextDatum(TL_DATUM);
                tft->setTextPadding(34);
                tft->drawString(tempLine, 256, row2 - 4);
            }

            if (contentDirty || _pc->ram_used != cache.ramUsed) {
                cache.ramUsed = _pc->ram_used;
                float rf = _pc->ram_total > 0 ? (float)_pc->ram_used / _pc->ram_total : 0.0f;
                uint16_t rc = heatCol(rf);
                if (contentDirty || rc != cache.ramBorder) {
                    cache.ramBorder = rc;
                    tft->fillRect(14, row3 - 6, 48, 14, C_PANEL);
                    drawIconRAM(22, row3 - 6, rc);
                    drawText(36, row3 - 4, 1, C_MUTED, "RAM", 28, C_PANEL);
                }
                drawBar(barX, row3 - 3, barW, 6, rf, rc);
                snprintf(line, sizeof(line), "%.1f/%.0fG", _pc->ram_used / 1024.0f, _pc->ram_total / 1024.0f);
                drawText(textX, row3 - 4, 1, rc, line, 80, C_PANEL);
            }

        } else {
            // Режим ОФФЛАЙН
            uint16_t tempCol = sensor.bme_ok ? tempColor(sensor.temperature) : C_MUTED;
            uint16_t humCol = sensor.bme_ok ? humColor(sensor.humidity) : C_MUTED;
            uint16_t pressCol = sensor.bme_ok ? pressColor(sensor.pressure) : C_MUTED;

            if (contentDirty) {
                drawCard(8,   CY + 74, 148, 52, C_CYAN);
                drawIconTemp(24, CY + 82, C_CYAN);
                drawText(36, CY + 81, 1, C_MUTED, "ROOM TEMP", 100, C_PANEL);
            }
            if (contentDirty) {
                drawCard(164, CY + 74, 148, 52, C_VIOLET);
                drawIconHum(180, CY + 82, C_VIOLET);
                drawText(192, CY + 81, 1, C_MUTED, "HUMIDITY", 100, C_PANEL);
            }
            snprintf(line, sizeof(line), sensor.bme_ok ? "%.1fC" : "--", sensor.temperature);
            drawText(20, CY + 97, 2, sensor.bme_ok ? tempCol : C_MUTED, line, 120, C_PANEL);

            snprintf(line, sizeof(line), sensor.bme_ok ? "%d%%" : "--", (int)sensor.humidity);
            drawText(176, CY + 97, 2, sensor.bme_ok ? humCol : C_MUTED, line, 120, C_PANEL);

            if (contentDirty) {
                drawCard(8,  CY + 130, 148, 52, C_GREEN);
                drawIconPress(24, CY + 138, C_GREEN);
                drawText(36, CY + 137, 1, C_MUTED, "PRESSURE", 100, C_PANEL);
            }
            snprintf(line, sizeof(line), sensor.bme_ok ? "%.0f hPa" : "--", sensor.pressure);
            drawText(20, CY + 153, 2, sensor.bme_ok ? pressCol : C_MUTED, line, 120, C_PANEL);

            uint16_t airBorder = sensor.bme_ok ? airQColor(sensor.gas) : C_MUTED;
            if (contentDirty) {
                cache.airBorderOffline = airBorder;
                drawCard(164, CY + 130, 148, 52, C_ORANGE);
                drawIconAir(180, CY + 138, C_ORANGE);
                drawText(192, CY + 137, 1, C_MUTED, "AIR QUALITY", 100, C_PANEL);
            }
            
            if (sensor.bme_ok) {
                snprintf(line, sizeof(line), "%s (%.0fk)", airQLabel(sensor.gas), sensor.gas);
                drawText(176, CY + 153, 2, airQColor(sensor.gas), line, 130, C_PANEL);
            } else {
                drawText(176, CY + 153, 2, C_MUTED, "--", 130, C_PANEL);
            }
        }
    }

    void drawForecastTab(const WeatherData &weather) {
        if (!contentDirty && !forecastDirty) return;
        const int CY = CONTENT_Y;
        const int SW = tft->width();
        
        tft->fillRect(0, CY, SW, CONTENT_H, C_BG);
        char title[48];
        String city = RuntimeSettings::weatherCity();
        snprintf(title, sizeof(title), "%.24s 7 days forecast", city.c_str());
        drawText(10, CY + 4, 2, C_TEXT, title, SW - 20);

        if (!weather.ok) {
            drawCard(8, CY + 32, SW - 16, 54, C_RED);
            drawText(20, CY + 52, 2, C_MUTED, "No forecast data", 260, C_PANEL);
            forecastDirty = false;
            return;
        }

        char line[64];
        const int ROW_Y = CY + 28;
        const int ROW_H = 22;

        for (int i = 0; i < 7; i++) {
            int y = ROW_Y + i * ROW_H;
            const DayForecast &d = weather.forecast[i];
            bool today = (i == 0);
            drawCard(8, y, SW - 16, ROW_H - 2, C_STROKE);
            drawText(18, y + 4, 1, today ? C_AMBER : C_CYAN, d.date, 38, C_PANEL);
            drawText(58, y + 4, 1, today ? C_AMBER : C_CYAN, d.day, 24, C_PANEL);
            snprintf(line, sizeof(line), "%+.0f/%+.0f", d.temp_max, d.temp_min);
            drawText(86, y + 4, 1, C_TEXT, line, 52, C_PANEL);
            drawWeatherIcon(148, y + 10, d.weather_code, true, 4);
            drawText(166, y + 4, 1, C_MUTED, weatherDesc(d.weather_code), 62, C_PANEL);
            fmtWindSpeed(line, sizeof(line), d.wind_speed);
            drawText(232, y + 4, 1, C_CYAN, line, 70, C_PANEL);
        }
        forecastDirty = false;
    }

    void drawMonitTab(const SensorData &sensor, const WeatherData &weather, const UiStatus &status) {
        const int CY = CONTENT_Y;
        const int SW = tft->width();
        char tLine[16], lLine[16], eLine[16];
        char line[64];

        bool pc = _pc && pcFresh(*_pc);
        bool forceRedraw = contentDirty;
        
        if (pc != cache.monitPcWas) {
            cache.monitPcWas = pc;
            forceRedraw = true;
            tft->fillRect(0, CY, SW, CONTENT_H, C_BG);
        }

        if (pc) {
            int row1 = CY + 42; 
            int row2 = CY + 84; 
            int row3 = CY + 126;

            if (forceRedraw) {
                drawText(10, CY + 4, 2, C_TEXT, "PC MONITOR", 140);
                drawText(156, CY + 8, 1, C_GREEN, "LIVE", 50);
                
                drawCard(8, CY + 24, SW - 16, 36, C_AMBER);
                drawIconCPU(24, row1 - 6, C_AMBER); 
                drawText(38, row1 - 4, 1, C_MUTED, "CPU", 30, C_PANEL); 

                drawCard(8, CY + 66, SW - 16, 36, C_CYAN);
                drawIconGPU(24, row2 - 6, C_CYAN);
                drawText(38, row2 - 4, 1, C_MUTED, "GPU", 30, C_PANEL);
            }
            
            // Динамика CPU (Обмен местами: Сначала Бар -> Загрузка -> Температура -> Ватты)
            if (forceRedraw || _pc->cpu_temp != cache.cpuTemp || _pc->cpu_load != cache.cpuLoad) {
                cache.cpuTemp = _pc->cpu_temp; cache.cpuLoad = _pc->cpu_load;
                float tf = _pc->cpu_temp / 100.0f;
                uint16_t tc = heatCol(tf);
                
                drawBar(70, row1 - 4, 110, 8, _pc->cpu_load/100.0f, tc);
                
                snprintf(lLine, sizeof(lLine), "%.0f%%", _pc->cpu_load);
                snprintf(tLine, sizeof(tLine), "%.0fC", _pc->cpu_temp);
                if (_pc->cpu_power > 0) snprintf(eLine, sizeof(eLine), "%.0fW", _pc->cpu_power);
                else eLine[0] = '\0';

                drawText(186, row1 - 4, 1, C_TEXT, lLine, 36, C_PANEL);  // ПРОЦЕНТ ЗАГРУЗКИ
                drawText(228, row1 - 4, 1, tc, tLine, 40, C_PANEL);     // ТЕМПЕРАТУРА
                drawText(274, row1 - 4, 1, C_MUTED, eLine, 36, C_PANEL); // ВАТТЫ
            }

            // Динамика GPU
            if (forceRedraw || _pc->gpu_temp != cache.gpuTemp || _pc->gpu_load != cache.gpuLoad) {
                cache.gpuTemp = _pc->gpu_temp; cache.gpuLoad = _pc->gpu_load;
                float tf = _pc->gpu_temp / 100.0f;
                uint16_t tc = heatCol(tf);
                
                drawBar(70, row2 - 4, 110, 8, _pc->gpu_load/100.0f, tc);
                
                snprintf(lLine, sizeof(lLine), "%.0f%%", _pc->gpu_load);
                snprintf(tLine, sizeof(tLine), "%.0fC", _pc->gpu_temp);
                if (_pc->gpu_vram_total > 0) snprintf(eLine, sizeof(eLine), "%.1fG", _pc->gpu_vram_used / 1024.0f);
                else eLine[0] = '\0';

                drawText(186, row2 - 4, 1, C_TEXT, lLine, 36, C_PANEL);  // ПРОЦЕНТ ЗАГРУЗКИ
                drawText(228, row2 - 4, 1, tc, tLine, 40, C_PANEL);     // ТЕМПЕРАТУРА
                drawText(274, row2 - 4, 1, C_MUTED, eLine, 36, C_PANEL); // VRAM
            }

            // Динамика RAM
            if (forceRedraw || _pc->ram_used != cache.ramUsed) {
                cache.ramUsed = _pc->ram_used;
                float rf = _pc->ram_total > 0 ? (float)_pc->ram_used / _pc->ram_total : 0.0f;
                uint16_t rc = heatCol(rf);
                
                if (forceRedraw || rc != cache.ramBorder) {
                    cache.ramBorder = rc;
                    drawCard(8, CY + 108, SW - 16, 36, rc);
                    drawIconRAM(24, row3 - 6, rc);
                    drawText(38, row3 - 4, 1, C_MUTED, "RAM", 30, C_PANEL);
                }
                
                drawBar(70, row3 - 4, 110, 8, rf, rc);
                
                snprintf(lLine, sizeof(lLine), "%.1fG", _pc->ram_used / 1024.0f);
                snprintf(tLine, sizeof(tLine), "%.0fG", _pc->ram_total / 1024.0f);

                drawText(186, row3 - 4, 1, rc, lLine, 36, C_PANEL);
                drawText(228, row3 - 4, 1, C_MUTED, tLine, 40, C_PANEL);
            }

        } else {
            unsigned long ageSec = status.lastLogWriteMs ? (millis() - status.lastLogWriteMs) / 1000UL : 0;
            if (forceRedraw) {
                drawText(10, CY + 4, 2, C_TEXT, "LOCAL MONITOR", 170);
                drawText(232, CY + 8, 1, C_AMBER, "PC AGENT OFF", 76);

                drawCard(8, CY + 26, 148, 58, sensor.bme_ok ? C_CYAN : C_RED);
                drawIconTemp(24, CY + 38, C_CYAN);
                drawText(38, CY + 34, 1, C_MUTED, "ROOM", 80, C_PANEL);

                drawCard(164, CY + 26, 148, 58, weather.ok ? C_AMBER : C_RED);
                drawText(176, CY + 34, 1, C_MUTED, "OUTDOOR", 76, C_PANEL);

                drawCard(8, CY + 92, 148, 44, sensor.bme_ok ? C_GREEN : C_RED);
                drawIconPress(24, CY + 101, C_GREEN);
                drawText(38, CY + 99, 1, C_MUTED, "AIR", 60, C_PANEL);

                drawCard(164, CY + 92, 148, 44, loggerReady() ? C_GREEN : C_RED);
                drawText(176, CY + 99, 1, C_MUTED, "LOGGER", 70, C_PANEL);

                drawCard(8, CY + 144, SW - 16, 34, C_STROKE);
            }

            if (sensor.bme_ok) {
                snprintf(line, sizeof(line), "%+.1fC  %d%%", sensor.temperature, (int)sensor.humidity);
                drawText(20, CY + 56, 2, tempColor(sensor.temperature), line, 126, C_PANEL);
            } else {
                drawText(20, CY + 56, 2, C_RED, "NO SENSOR", 126, C_PANEL);
            }

            if (weather.ok) {
                snprintf(line, sizeof(line), "%+.0fC %s", weather.temperature, weatherDesc(weather.weather_code));
                drawText(176, CY + 50, 1, C_TEXT, line, 84, C_PANEL);
                fmtWindSpeed(line, sizeof(line), weather.wind_speed);
                drawText(176, CY + 65, 1, C_CYAN, line, 84, C_PANEL);
                drawWeatherIcon(292, CY + 58, weather.weather_code, weather.is_day, 8);
            } else {
                drawText(176, CY + 56, 2, C_RED, "NO DATA", 84, C_PANEL);
            }

            if (sensor.bme_ok) {
                snprintf(line, sizeof(line), "%.0f hPa", sensor.pressure);
                drawText(20, CY + 116, 1, C_TEXT, line, 78, C_PANEL);
                snprintf(line, sizeof(line), "%.0f kOhm", sensor.gas);
                drawText(98, CY + 116, 1, airQColor(sensor.gas), line, 48, C_PANEL);
            } else {
                drawText(20, CY + 116, 1, C_RED, "OFFLINE", 126, C_PANEL);
            }

            snprintf(line, sizeof(line), loggerReady() ? "OK  %lus ago" : "ERROR", ageSec);
            drawText(176, CY + 116, 1, loggerReady() ? C_GREEN : C_RED, line, 126, C_PANEL);

            unsigned long pcAge = (_pc && _pc->lastMs) ? (millis() - _pc->lastMs) / 1000UL : 0;
            if (pcAge > 0) snprintf(line, sizeof(line), "CPU/GPU paused  Last PC: %lus ago", pcAge);
            else snprintf(line, sizeof(line), "Start PC Agent to show CPU/GPU/RAM");
            drawText(20, CY + 154, 1, C_MUTED, line, 274, C_PANEL);
        }
    }

    void drawSystemTab(const SensorData &sensor, const UiStatus &status) {
        const int CY = CONTENT_Y;
        const int SW = tft->width();
        char line[96];
        const bool wifiOk = WiFi.status() == WL_CONNECTED;
        const bool sdOk = status.sdReady;
        const bool logOk = loggerReady();
        const bool sensorsOk = sensor.rtc_ok && sensor.bme_ok;
        const uint8_t sysFlags = (sensor.rtc_ok ? 0x01 : 0) |
                                 (sensor.bme_ok ? 0x02 : 0) |
                                 (sdOk ? 0x04 : 0) |
                                 (wifiOk ? 0x08 : 0) |
                                 (logOk ? 0x10 : 0);
        const bool layoutDirty = contentDirty || sysFlags != cache.sysFlags;
        cache.sysFlags = sysFlags;

        auto okColor = [](bool ok) -> uint16_t { return ok ? C_GREEN : C_RED; };

        auto drawPill = [&](int x, int y, const char* label, bool ok) {
            const uint16_t col = okColor(ok);
            tft->fillRoundRect(x, y, 70, 18, 6, C_PANEL);
            tft->drawRoundRect(x, y, 70, 18, 6, C_STROKE);
            tft->fillCircle(x + 9, y + 9, 3, col);
            drawText(x + 18, y + 5, 1, C_TEXT, label, 31, C_PANEL);
            drawText(x + 50, y + 5, 1, col, ok ? "OK" : "ERR", 18, C_PANEL);
        };

        auto drawSection = [&](int x, int y, int w, int h, uint16_t col, const char* title) {
            drawCard(x, y, w, h, col);
            drawText(x + 10, y + 7, 1, col, title, w - 18, C_PANEL);
        };

        auto drawRow = [&](int x, int y, const char* label, const char* value, uint16_t col = C_TEXT) {
            drawText(x, y, 1, C_MUTED, label, 48, C_PANEL);
            drawText(x + 50, y, 1, col, value, 88, C_PANEL);
        };

        if (layoutDirty) {
            tft->fillRect(0, CY, SW, CONTENT_H, C_BG);
            drawText(10, CY + 4, 2, C_TEXT, "SYSTEM STATUS", 180);
            drawPill(8,   CY + 26, "RTC",  sensor.rtc_ok);
            drawPill(84,  CY + 26, "BME",  sensor.bme_ok);
            drawPill(160, CY + 26, "SD",   sdOk);
            drawPill(236, CY + 26, "WIFI", wifiOk);
            drawSection(8, CY + 50, 148, 58, sensorsOk ? C_GREEN : C_RED, "SENSORS");
            drawSection(164, CY + 50, 148, 58, (sdOk && logOk) ? C_GREEN : C_RED, "STORAGE");
            drawSection(8, CY + 116, 148, 62, wifiOk ? C_CYAN : C_RED, "WIFI");
            drawSection(164, CY + 116, 148, 62, C_AMBER, "ESP32");
        }

        if (sensor.rtc_ok) {
            snprintf(line, sizeof(line), "%s %.5s", DOW_NAMES[sensor.weekday % 7], sensor.timeStr + 12);
            drawRow(18, CY + 69, "RTC", line, C_GREEN);
        } else {
            drawRow(18, CY + 69, "RTC", "OFFLINE", C_RED);
        }
        if (sensor.bme_ok) {
            snprintf(line, sizeof(line), "%+.1fC  %d%%", sensor.temperature, (int)sensor.humidity);
            drawRow(18, CY + 82, "BME", line, C_GREEN);
            snprintf(line, sizeof(line), "%.0fhPa %.0fk", sensor.pressure, sensor.gas);
            drawRow(18, CY + 95, "AIR", line, C_MUTED);
        } else {
            drawRow(18, CY + 82, "BME", "OFFLINE", C_RED);
            drawRow(18, CY + 95, "AIR", "--", C_MUTED);
        }

        if (sdOk) {
            snprintf(line, sizeof(line), "%llu MB", status.sdSizeMb);
            drawRow(174, CY + 69, "CARD", line, C_GREEN);
        } else {
            drawRow(174, CY + 69, "CARD", "OFFLINE", C_RED);
        }
        drawRow(174, CY + 82, "LOG", logOk ? "READY" : "ERROR", okColor(logOk));
        if (status.lastLogWriteMs) {
            snprintf(line, sizeof(line), "%lus ago", (millis() - status.lastLogWriteMs) / 1000UL);
            drawRow(174, CY + 95, "LAST", line, C_MUTED);
        } else {
            drawRow(174, CY + 95, "LAST", "--", C_MUTED);
        }

        if (wifiOk) {
            snprintf(line, sizeof(line), "%.15s", WiFi.SSID().c_str());
            drawRow(18, CY + 135, "SSID", line, C_CYAN);
            snprintf(line, sizeof(line), "%s", WiFi.localIP().toString().c_str());
            drawRow(18, CY + 148, "IP", line, C_TEXT);
            snprintf(line, sizeof(line), "%d dBm", WiFi.RSSI());
            drawRow(18, CY + 161, "RSSI", line, WiFi.RSSI() > -70 ? C_GREEN : C_AMBER);
        } else {
            drawRow(18, CY + 135, "SSID", "OFFLINE", C_RED);
            drawRow(18, CY + 148, "IP", "--", C_MUTED);
            drawRow(18, CY + 161, "RSSI", "--", C_MUTED);
        }

        unsigned long upSec = millis() / 1000;
        snprintf(line, sizeof(line), "%luh %02um", upSec / 3600, (unsigned int)((upSec % 3600) / 60));
        drawRow(174, CY + 135, "UP", line, C_TEXT);
        snprintf(line, sizeof(line), "%lu KB", (unsigned long)ESP.getFreeHeap() / 1024UL);
        drawRow(174, CY + 148, "HEAP", line, C_GREEN);
        snprintf(line, sizeof(line), "%u MHz", ESP.getCpuFreqMHz());
        drawRow(174, CY + 161, "CPU", line, C_MUTED);
    }

public:
    UIManager() : tft(nullptr), _pc(nullptr), currentTab(TAB_NOW),
                  chromeDirty(true), contentDirty(true), forecastDirty(true), lastTouchMs(0) {}

    void init(TFT_eSPI *display) {
        tft = display;
        invalidate();
    }

    void setPCData(const PCData *pc) {
        _pc = pc;
    }

    void invalidate() {
        chromeDirty   = true;
        contentDirty  = true;
        forecastDirty = true;
    }

    void invalidateForecast() {
        forecastDirty = true;
        if (currentTab == TAB_FORECAST) contentDirty = true;
    }

    bool handleTouch() {
        if (!tft) return false;
        if (millis() - lastTouchMs < TOUCH_DEBOUNCE_MS) return false;

        uint16_t x = 0, y = 0;
        if (!tft->getTouch(&x, &y)) return false;
        if (y < TABBAR_Y || y >= TABBAR_Y + TABBAR_H) return false;

        int tab = x / (tft->width() / 4);
        if (tab > 3 || tab == (int)currentTab) return false;

        currentTab   = static_cast<ScreenTab>(tab);
        lastTouchMs  = millis();
        invalidate();
        return true;
    }

    void draw(const SensorData &sensor, const WeatherData &weather, const UiStatus &status) {
        if (!tft) return;

        drawTaskbar(sensor, status);
        drawTabbar();

        if (contentDirty) {
            tft->fillRect(0, CONTENT_Y, tft->width(), CONTENT_H, C_BG);
        }

        switch (currentTab) {
            case TAB_NOW:      drawNowTab(sensor, weather);   break;
            case TAB_FORECAST: drawForecastTab(weather);      break;
            case TAB_MONIT:    drawMonitTab(sensor, weather, status); break;
            case TAB_SYSTEM:   drawSystemTab(sensor, status); break;
        }

        chromeDirty  = false;
        contentDirty = false;
    }
};

static UIManager ui;

void initUI(TFT_eSPI &display) { ui.init(&display); }
void initPCDisplay(const PCData *pc) { ui.setPCData(pc); }
bool handleUI() { return ui.handleTouch(); }
void invalidateUI() { ui.invalidate(); }
void invalidateForecastUI() { ui.invalidateForecast(); }
void drawUI(const SensorData &sensor, const WeatherData &weather, const UiStatus &status) { ui.draw(sensor, weather, status); }
