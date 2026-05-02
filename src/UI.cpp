#include "UI.h"
#include "API.h"
#include "Config.h"
#include "Logger.h"
#include <WiFi.h>

// ── Константы Layout'а и Цветов ──────────────────────────────────────────────
static const unsigned long TOUCH_DEBOUNCE_MS = 250;
static const int TASKBAR_H = 22;                     
static const int TABBAR_Y  = 212;
static const int TABBAR_H  = 28;
static const int CONTENT_Y = 22;                     
static const int CONTENT_H = TABBAR_Y - CONTENT_Y;   // 190

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
        tft->fillRoundRect(x, y, w, h, 5, C_PANEL);
        tft->drawRoundRect(x, y, w, h, 5, border);
    }

    void drawChip(int x, int y, const char *label, bool ok, uint16_t bg) {
        uint16_t col = ok ? C_GREEN : C_RED;
        tft->fillRoundRect(x, y, 32, 14, 3, bg);
        tft->drawRoundRect(x, y, 32, 14, 3, col);
        drawText(x + 4, y + 2, 1, col, label, 24, bg);
    }

    void drawBar(int x, int y, int w, int h, float frac, uint16_t col) {
        frac = constrain(frac, 0.0f, 1.0f);
        tft->fillRect(x, y, w, h, C_STROKE);
        int fw = (int)(w * frac);
        if (fw > 0) tft->fillRect(x, y, fw, h, col);
    }

    uint16_t heatCol(float frac) {
        if (frac > 0.8f) return C_RED;
        if (frac > 0.6f) return C_AMBER;
        return C_CYAN;
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
        if (gas >= 300) return 0x07E0;
        if (gas >= 150) return 0xAFE5;
        if (gas >= 100) return 0xFFE0;
        if (gas >=  50) return 0xFD20;
        return 0xF800;
    }

    const char* airQLabel(float gas) {
        if (gas >= 300) return "Great"; 
        if (gas >= 150) return "Good";
        if (gas >= 100) return "Fair";
        if (gas >=  50) return "Poor";
        return "Bad";
    }

    void fmtWindSpeed(char *buf, size_t sz, float kmh) {
#if WIND_UNIT_MS
        snprintf(buf, sz, "%.1f m/s", kmh / 3.6f);
#else
        snprintf(buf, sz, "%.1f km/h", kmh);
#endif
    }

    void drawWeatherIcon(int cx, int cy, int code, int R = 9) {
        if (code < 0) { tft->drawLine(cx - R/2, cy, cx + R/2, cy, C_MUTED); return; }
        if (code == 0) {
            tft->fillCircle(cx, cy, R - 2, C_AMBER);
            const float dx[] = {1.0f, 0.707f, 0.0f, -0.707f, -1.0f, -0.707f, 0.0f, 0.707f};
            const float dy[] = {0.0f, 0.707f, 1.0f, 0.707f, 0.0f, -0.707f, -1.0f, -0.707f};
            for (int i = 0; i < 8; i++) {
                tft->drawLine(cx + (int)(R*dx[i]), cy + (int)(R*dy[i]), cx + (int)((R+R/2)*dx[i]), cy + (int)((R+R/2)*dy[i]), C_AMBER);
            }
            return;
        }
        if (code <= 2) tft->fillCircle(cx - R/2, cy - R/3, R/2 + 1, C_AMBER);
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
    void drawTaskbar(const UiStatus &status) {
        const uint16_t bg = C_HEADER;
        if (chromeDirty) {
            tft->fillRect(0, 0, tft->width(), TASKBAR_H, bg);
            tft->fillRect(0, TASKBAR_H - 2, tft->width(), 2, C_BLUE);
        }
        bool wifiOk = (WiFi.status() == WL_CONNECTED);
        int right = tft->width();
        drawChip(right - 146, 4, "WiFi", wifiOk,         bg);
        drawChip(right - 110, 4, "SD",   status.sdReady, bg);
        drawChip(right - 74,  4, "API",  apiReady(),     bg);
        drawChip(right - 38,  4, "LOG",  loggerReady(),  bg);
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
        const int LCEN = 109;
        const int WCEN = 261;

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

        tft->setTextFont(4); 
        tft->setTextColor(C_TEXT, C_PANEL);
        tft->setTextDatum(MC_DATUM);
        tft->setTextPadding(190);
        tft->drawString(sensor.timeStr + 12, LCEN, CY + 28); 
        
        char dateLine[32];
        snprintf(dateLine, sizeof(dateLine), "%s  %.10s", sensor.rtc_ok ? DOW_NAMES[sensor.weekday % 7] : "--", sensor.timeStr);
        tft->setTextFont(2);
        tft->setTextColor(C_MUTED, C_PANEL);
        tft->drawString(dateLine, LCEN, CY + 52);

        int currentCode = weather.ok ? weather.weather_code : -1;
        // Обновляем погоду, если изменился код, температура ИЛИ был полный сброс экрана
        if (contentDirty || currentCode != cache.weatherCode || weather.temperature != cache.weatherTemp) {
            cache.weatherCode = currentCode;
            cache.weatherTemp = weather.temperature;
            
            tft->fillRect(211, CY + 3, 100, 46, C_PANEL);
            tft->drawLine(209, CY + 10, 209, CY + 56, C_STROKE);
            drawWeatherIcon(291, CY + 26, currentCode, 11);
            
            tft->setTextFont(4);
            tft->setTextColor(C_AMBER, C_PANEL);
            tft->setTextDatum(ML_DATUM);
            tft->setTextPadding(0); // Мы очистили фон вручную fillRect, паддинг не нужен

            if (weather.ok) {
                // Отрисовка температуры
                char tNum[16];
                snprintf(tNum, sizeof(tNum), "%.0f", weather.temperature);
                int tw = tft->textWidth(tNum);
                tft->drawString(tNum, 213, CY + 26);
                
                // РУЧНАЯ ОТРИСОВКА КРУЖКА ГРАДУСА (геометрия)
                int circX = 213 + tw + 4;
                int circY = CY + 18;
                tft->drawCircle(circX, circY, 3, C_AMBER);
                tft->drawCircle(circX, circY, 2, C_AMBER); // Делаем кружок толще
                
                // Отрисовка буквы C
                tft->drawString("C", circX + 6, CY + 26);
            } else {
                tft->drawString("--", 213, CY + 26);
            }
        }

        tft->setTextDatum(MC_DATUM);
        tft->setTextPadding(98);
        if (weather.ok) fmtWindSpeed(line, sizeof(line), weather.wind_speed);
        else            snprintf(line, sizeof(line), "--");
        tft->setTextFont(2);
        tft->setTextColor(C_CYAN, C_PANEL);
        tft->drawString(line, WCEN, CY + 53);
        tft->setTextPadding(0);
        tft->setTextDatum(TL_DATUM);

        // 2. Блок ПОКАЗАНИЙ
        if (pcOn) {
            int w = 73, gap = 4;
            int c1 = 8, c2 = c1+w+gap, c3 = c2+w+gap, c4 = c3+w+gap;
            int cx1 = c1+w/2, cx2 = c2+w/2, cx3 = c3+w/2, cx4 = c4+w/2; 
            
            if (contentDirty) {
                drawCard(c1, CY + 70, w, 46, C_AMBER);
                drawCard(c2, CY + 70, w, 46, C_CYAN);
                drawCard(c3, CY + 70, w, 46, C_GREEN);
                drawIconTemp (cx1, CY + 74, C_AMBER);
                drawIconHum  (cx2, CY + 74, C_CYAN);
                drawIconPress(cx3, CY + 74, C_GREEN);
            }

            uint16_t airBdr = sensor.bme_ok ? airQColor(sensor.gas) : C_STROKE;
            if (contentDirty || airBdr != cache.airBorderNow) {
                cache.airBorderNow = airBdr;
                drawCard(c4, CY + 70, w, 46, airBdr);
                drawIconAir(cx4, CY + 74, airBdr);
            }

            tft->setTextFont(2);
            tft->setTextDatum(MC_DATUM);
            tft->setTextPadding(50); 

            snprintf(line, sizeof(line), sensor.bme_ok ? "%.1fC" : "--", sensor.temperature);
            tft->setTextColor(C_AMBER, C_PANEL);
            tft->drawString(line, cx1, CY + 98);
            drawBar(cx1-25, CY + 110, 50, 4, sensor.bme_ok ? (sensor.temperature + 10.0f) / 50.0f : 0.0f, C_AMBER);

            snprintf(line, sizeof(line), sensor.bme_ok ? "%d%%" : "--", (int)sensor.humidity);
            tft->setTextColor(C_CYAN, C_PANEL);
            tft->drawString(line, cx2, CY + 98);
            drawBar(cx2-25, CY + 110, 50, 4, sensor.bme_ok ? sensor.humidity / 100.0f : 0.0f, C_CYAN);

            snprintf(line, sizeof(line), sensor.bme_ok ? "%.0f" : "--", sensor.pressure);
            tft->setTextColor(C_GREEN, C_PANEL);
            tft->drawString(line, cx3, CY + 98);
            drawBar(cx3-25, CY + 110, 50, 4, sensor.bme_ok ? (sensor.pressure - 950.0f) / 100.0f : 0.0f, C_GREEN);

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
                snprintf(line, sizeof(line), "%.0fC %.0f%%", _pc->cpu_temp, _pc->cpu_load);
                drawText(textX, row1 - 4, 1, heatCol(lf), line, 80, C_PANEL);
            }

            if (contentDirty || _pc->gpu_temp != cache.gpuTemp || _pc->gpu_load != cache.gpuLoad) {
                cache.gpuTemp = _pc->gpu_temp; cache.gpuLoad = _pc->gpu_load;
                float lf = _pc->gpu_load / 100.0f;
                drawBar(barX, row2 - 3, barW, 6, lf, heatCol(lf));
                snprintf(line, sizeof(line), "%.0fC %.0f%%", _pc->gpu_temp, _pc->gpu_load);
                drawText(textX, row2 - 4, 1, heatCol(lf), line, 80, C_PANEL);
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
            if (contentDirty) {
                drawCard(8,   CY + 74, 148, 52, C_CYAN);
                drawIconTemp(22, CY + 86, C_CYAN);
                drawText(36, CY + 81, 1, C_MUTED, "ROOM TEMP", 100, C_PANEL);
                
                drawCard(164, CY + 74, 148, 52, C_CYAN);
                drawIconHum(178, CY + 86, C_CYAN);
                drawText(192, CY + 81, 1, C_MUTED, "HUMIDITY", 100, C_PANEL);
            }
            snprintf(line, sizeof(line), sensor.bme_ok ? "%.1fC" : "--", sensor.temperature);
            drawText(20, CY + 97, 2, C_TEXT, line, 120, C_PANEL);

            snprintf(line, sizeof(line), sensor.bme_ok ? "%d%%" : "--", (int)sensor.humidity);
            drawText(176, CY + 97, 2, C_TEXT, line, 120, C_PANEL);

            if (contentDirty) {
                drawCard(8,  CY + 130, 148, 52, C_GREEN);
                drawIconPress(22, CY + 142, C_GREEN);
                drawText(36, CY + 137, 1, C_MUTED, "PRESSURE", 100, C_PANEL);
            }
            snprintf(line, sizeof(line), sensor.bme_ok ? "%.0f hPa" : "--", sensor.pressure);
            drawText(20, CY + 153, 2, C_TEXT, line, 120, C_PANEL);

            uint16_t airBorder = sensor.bme_ok ? airQColor(sensor.gas) : C_STROKE;
            if (contentDirty || airBorder != cache.airBorderOffline) {
                cache.airBorderOffline = airBorder;
                drawCard(164, CY + 130, 148, 52, airBorder);
                drawIconAir(178, CY + 142, airBorder);
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
        drawText(10, CY + 4, 2, C_TEXT, "7-DAY FORECAST", 200);

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
            drawCard(8, y, SW - 16, ROW_H - 2, today ? C_AMBER : C_STROKE);
            drawText(18,  y + 4, 1, today ? C_AMBER : C_CYAN, today ? "TODAY" : d.day, 42, C_PANEL);
            snprintf(line, sizeof(line), "%+.0f / %+.0f", d.temp_min, d.temp_max);
            drawText(72,  y + 4, 1, C_TEXT,  line, 86, C_PANEL);
            drawText(164, y + 4, 1, C_MUTED, weatherDesc(d.weather_code), 130, C_PANEL);
        }
        forecastDirty = false;
    }

    void drawMonitTab(const UiStatus &status) {
        const int CY = CONTENT_Y;
        const int SW = tft->width();
        char tLine[16], lLine[16], eLine[16];

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
                    tft->fillRoundRect(8, CY + 108, SW - 16, 36, 5, C_PANEL);
                    tft->drawRoundRect(8, CY + 108, SW - 16, 36, 5, rc);
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
                drawText(10, CY + 4, 2, C_TEXT, "DATA LOG", 200);
                drawCard(8,  CY + 26,  SW - 16, 54, loggerReady() ? C_GREEN : C_RED);
                drawCard(8,  CY + 86,  SW - 16, 46, C_BLUE);
                drawCard(8,  CY + 138, SW - 16, 36, C_STROKE);
                drawText(20, CY + 91, 1, C_MUTED, "API ENDPOINTS", 100, C_PANEL);
            }
            drawText(20, CY + 34, 2, C_CYAN, readingsLogPath(), 270, C_PANEL);
            snprintf(tLine, sizeof(tLine), "Every %us  Last: %lus ago", DATA_LOG_INTERVAL_SEC, ageSec);
            drawText(20, CY + 55, 1, loggerReady() ? C_GREEN : C_RED, tLine, 270, C_PANEL);

            drawText(20,  CY + 103, 2, C_AMBER, "/api/status", 130, C_PANEL);
            drawText(164, CY + 103, 2, C_AMBER, "/api/log",    118, C_PANEL);

            snprintf(tLine, sizeof(tLine), "http://" DEVICE_NAME ".local/   %s", WiFi.localIP().toString().c_str());
            drawText(20, CY + 146, 1, C_MUTED, tLine, 270, C_PANEL);
        }
    }

    void drawSystemTab(const SensorData &sensor, const UiStatus &status) {
        const int CY = CONTENT_Y;
        const int SW = tft->width();
        char line[96];

        if (contentDirty) {
            drawText(10, CY + 4, 2, C_TEXT, "SYSTEM", 200);
            drawCard(8, CY + 134, SW - 16, 44, C_STROKE);
        }

        uint16_t hwBorder = (sensor.rtc_ok && sensor.bme_ok && status.sdReady) ? C_GREEN : C_RED;
        if (contentDirty || hwBorder != cache.hwBorder) {
            cache.hwBorder = hwBorder;
            drawCard(8, CY + 28, SW - 16, 50, hwBorder);
            drawText(20, CY + 36, 1, C_MUTED, "RTC",    46, C_PANEL);
            drawText(20, CY + 46, 1, C_MUTED, "BME680", 46, C_PANEL);
            drawText(20, CY + 56, 1, C_MUTED, "SD",     46, C_PANEL);
        }

        if (sensor.rtc_ok) {
            snprintf(line, sizeof(line), "%s  %.10s", DOW_NAMES[sensor.weekday % 7], sensor.timeStr);
            drawText(70, CY + 36, 1, C_GREEN, line, 230, C_PANEL);
        } else {
            drawText(70, CY + 36, 1, C_RED, "OFFLINE", 230, C_PANEL);
        }

        if (sensor.bme_ok) {
            snprintf(line, sizeof(line), "%+.1fC  %d%%  %.0f hPa  %.0f kOhm",
                     sensor.temperature, (int)sensor.humidity, sensor.pressure, sensor.gas);
            drawText(70, CY + 46, 1, C_GREEN, line, 230, C_PANEL);
        } else {
            drawText(70, CY + 46, 1, C_RED, "OFFLINE", 230, C_PANEL);
        }

        if (status.sdReady) {
            snprintf(line, sizeof(line), "%s   %llu MB", readingsLogPath(), status.sdSizeMb);
            drawText(70, CY + 56, 1, C_GREEN, line, 230, C_PANEL);
        } else {
            drawText(70, CY + 56, 1, C_RED, "OFFLINE", 230, C_PANEL);
        }

        uint16_t wfBorder = WiFi.status() == WL_CONNECTED ? C_CYAN : C_RED;
        if (contentDirty || wfBorder != cache.wfBorder) {
            cache.wfBorder = wfBorder;
            drawCard(8, CY + 84, SW - 16, 44, wfBorder);
            drawText(20, CY + 91, 1, C_MUTED, "WIFI", 40, C_PANEL);
        }

        if (WiFi.status() == WL_CONNECTED) {
            snprintf(line, sizeof(line), DEVICE_NAME ".local    %s", WiFi.localIP().toString().c_str());
            drawText(64, CY + 89, 2, C_CYAN, line, 238, C_PANEL);
        } else {
            drawText(64, CY + 89, 2, C_RED, "OFFLINE", 238, C_PANEL);
        }

        unsigned long upSec = millis() / 1000;
        snprintf(line, sizeof(line), "Heap %lu B     Up %luh %02um",
                 (unsigned long)ESP.getFreeHeap(), upSec / 3600, (unsigned int)((upSec % 3600) / 60));
        drawText(20, CY + 142, 1, C_MUTED, line, 270, C_PANEL);

        snprintf(line, sizeof(line), "Weather %us    Log %us    Wind %s",
                 WEATHER_UPDATE_INTERVAL_SEC, DATA_LOG_INTERVAL_SEC, WIND_UNIT_MS ? "m/s" : "km/h");
        drawText(20, CY + 155, 1, C_MUTED, line, 270, C_PANEL);
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

        drawTaskbar(status);
        drawTabbar();

        if (contentDirty) {
            tft->fillRect(0, CONTENT_Y, tft->width(), CONTENT_H, C_BG);
        }

        switch (currentTab) {
            case TAB_NOW:      drawNowTab(sensor, weather);   break;
            case TAB_FORECAST: drawForecastTab(weather);      break;
            case TAB_MONIT:    drawMonitTab(status);          break;
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