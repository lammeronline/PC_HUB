#include "Telegram.h"
#include "RuntimeSettings.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Arduino.h>

namespace Telegram {

static const SensorData  *_sensor  = nullptr;
static const WeatherData *_weather = nullptr;

static int32_t       _lastUpdateId   = 0;
static bool          _rebootPending  = false;
static unsigned long _rebootPendingMs = 0;

static QueueHandle_t _sendQueue = nullptr;
static const int     QUEUE_DEPTH = 6;
static const int     MSG_MAX     = 512;

// ── Low-level HTTP send ───────────────────────────────────────────────────────

static bool doSend(const char *text) {
    String token  = RuntimeSettings::tgToken();
    String chatId = RuntimeSettings::tgChatId();
    if (token.isEmpty() || chatId.isEmpty()) return false;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.telegram.org/bot" + token + "/sendMessage");
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(8000);

    String body;
    body.reserve(strlen(text) + 80);
    body = "{\"chat_id\":";
    body += chatId;
    body += ",\"text\":\"";
    for (const char *p = text; *p; p++) {
        if      (*p == '"')  body += "\\\"";
        else if (*p == '\\') body += "\\\\";
        else if (*p == '\n') body += "\\n";
        else                  body += *p;
    }
    body += "\",\"parse_mode\":\"HTML\"}";

    int code = http.POST(body);
    http.end();
    return code == 200;
}

// ── Update polling & command handling ─────────────────────────────────────────

static void handleCmd(const String &text) {
    if (text == "/status" || text.startsWith("/status ")) {
        String m = "<b>PCHUB Status</b>\n";
        if (_sensor && _sensor->bme_ok) {
            m += "Temp: <b>"     + String(_sensor->temperature, 1) + " C</b>\n";
            m += "Humidity: <b>" + String(_sensor->humidity, 1)    + " %</b>\n";
            m += "Pressure: <b>" + String(_sensor->pressure, 0)    + " hPa</b>\n";
            m += "Gas: <b>"      + String(_sensor->gas, 0)         + " kOhm</b>\n";
        } else {
            m += "BME680: ERROR\n";
        }
        if (_weather && _weather->ok)
            m += "Outdoor: <b>" + String(_weather->temperature, 1) + " C</b>\n";
        if (_sensor && _sensor->rtc_ok)
            m += "Time: " + String(_sensor->timeStr) + "\n";
        doSend(m.c_str());

    } else if (text == "/help" || text.startsWith("/help ")) {
        doSend("<b>PCHUB Bot</b>\n"
               "/status \xe2\x80\x94 sensor readings\n"
               "/reboot \xe2\x80\x94 reboot device\n"
               "/help \xe2\x80\x94 this message");

    } else if (text == "/reboot") {
        _rebootPending   = true;
        _rebootPendingMs = millis();
        doSend("Send <code>/reboot confirm</code> within 30 s to confirm.");

    } else if (text == "/reboot confirm") {
        if (_rebootPending && millis() - _rebootPendingMs < 30000UL) {
            doSend("Rebooting\xe2\x80\xa6");
            vTaskDelay(pdMS_TO_TICKS(600));
            ESP.restart();
        } else {
            _rebootPending = false;
            doSend("No pending reboot or confirmation window expired.");
        }
    }
}

static void doPoll() {
    String token = RuntimeSettings::tgToken();
    if (token.isEmpty()) return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url = "https://api.telegram.org/bot" + token + "/getUpdates?offset=";
    url += (_lastUpdateId + 1);
    url += "&limit=10&timeout=0";

    http.begin(client, url);
    http.setTimeout(5000);
    int code = http.GET();
    if (code != 200) { http.end(); return; }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) return;
    if (!doc["ok"].as<bool>()) return;

    String allowed = RuntimeSettings::tgChatId();
    for (JsonObject upd : doc["result"].as<JsonArray>()) {
        int32_t uid = upd["update_id"] | 0;
        if (uid > _lastUpdateId) _lastUpdateId = uid;

        JsonObject msg = upd["message"];
        if (!msg) continue;
        String text = msg["text"] | "";
        text.trim();
        if (text.isEmpty()) continue;

        char cidBuf[24];
        snprintf(cidBuf, sizeof(cidBuf), "%lld", (long long)(msg["chat"]["id"] | 0LL));
        if (!allowed.isEmpty() && String(cidBuf) != allowed) continue;

        handleCmd(text);
    }
}

// ── Alert engine ─────────────────────────────────────────────────────────────

struct AlertState {
    unsigned long lastSentMs = 0;
    bool active = false;
};

static AlertState _aTempHi, _aTempLo, _aHumHi, _aHumLo, _aGasLo;

static void evalAlert(AlertState &st, bool cond, unsigned long coolMs,
                      const char *fmt, float val, float thr) {
    if (cond) {
        unsigned long now = millis();
        if (!st.active || now - st.lastSentMs >= coolMs) {
            char buf[160];
            snprintf(buf, sizeof(buf), fmt, val, thr);
            doSend(buf);
            st.lastSentMs = now;
        }
        st.active = true;
    } else {
        st.active = false;
    }
}

static void doCheckAlerts() {
    if (!_sensor || !_sensor->bme_ok) return;
    unsigned long coolMs = (unsigned long)RuntimeSettings::tgCooldownMin() * 60000UL;
    float t = _sensor->temperature;
    float h = _sensor->humidity;
    float g = _sensor->gas;

    if (RuntimeSettings::tgTempHiEn())
        evalAlert(_aTempHi, t >= RuntimeSettings::tgTempHi(), coolMs,
                  "High temp: %.1f C (limit %.1f C)", t, RuntimeSettings::tgTempHi());
    if (RuntimeSettings::tgTempLoEn())
        evalAlert(_aTempLo, t <= RuntimeSettings::tgTempLo(), coolMs,
                  "Low temp: %.1f C (limit %.1f C)", t, RuntimeSettings::tgTempLo());
    if (RuntimeSettings::tgHumHiEn())
        evalAlert(_aHumHi, h >= RuntimeSettings::tgHumHi(), coolMs,
                  "High humidity: %.1f%% (limit %.1f%%)", h, RuntimeSettings::tgHumHi());
    if (RuntimeSettings::tgHumLoEn())
        evalAlert(_aHumLo, h <= RuntimeSettings::tgHumLo(), coolMs,
                  "Low humidity: %.1f%% (limit %.1f%%)", h, RuntimeSettings::tgHumLo());
    if (RuntimeSettings::tgGasLoEn())
        evalAlert(_aGasLo, g <= RuntimeSettings::tgGasLo(), coolMs,
                  "Bad air quality: %.0f kOhm (limit %.0f kOhm)", g, RuntimeSettings::tgGasLo());
}

// ── FreeRTOS task (runs on core 0) ────────────────────────────────────────────

static void telegramTask(void *) {
    static unsigned long lastAlertMs = 0;
    char msgBuf[MSG_MAX];

    for (;;) {
        if (RuntimeSettings::tgEnabled()) {
            while (xQueueReceive(_sendQueue, msgBuf, 0) == pdTRUE)
                doSend(msgBuf);

            doPoll();

            unsigned long now = millis();
            if (now - lastAlertMs >= 10000UL) {
                lastAlertMs = now;
                doCheckAlerts();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

bool sendMessage(const String &text) {
    if (!_sendQueue) return false;
    char buf[MSG_MAX];
    strlcpy(buf, text.c_str(), sizeof(buf));
    return xQueueSend(_sendQueue, buf, 0) == pdTRUE;
}

void begin(const SensorData *sensor, const WeatherData *weather) {
    _sensor    = sensor;
    _weather   = weather;
    _sendQueue = xQueueCreate(QUEUE_DEPTH, MSG_MAX);
    xTaskCreatePinnedToCore(telegramTask, "telegram", 8192, nullptr, 1, nullptr, 0);
}

void handle() {}

} // namespace Telegram
