#include "MQTT.h"
#include "RuntimeSettings.h"
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>

namespace MQTT {

static const SensorData  *_sensor  = nullptr;
static const WeatherData *_weather = nullptr;

static WiFiClient   _wifiClient;
static PubSubClient _client(_wifiClient);

static unsigned long _lastPublish   = 0;
static unsigned long _lastReconnect = 0;
static String        _activeBroker;
static uint16_t      _activePort = 0;

// ── Helpers ───────────────────────────────────────────────────────────────────

static String topic(const char *suffix) {
    return RuntimeSettings::mqttPrefix() + "/" + RuntimeSettings::hostname() + "/" + suffix;
}

static void pub(const char *suffix, const String &value, bool retain = true) {
    _client.publish(topic(suffix).c_str(), value.c_str(), retain);
}

// ── Command callback ──────────────────────────────────────────────────────────

static void onMessage(const char * /*top*/, byte *payload, unsigned int len) {
    char buf[32];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, payload, n);
    buf[n] = '\0';
    if (strcmp(buf, "reboot") == 0) ESP.restart();
}

// ── Connect ───────────────────────────────────────────────────────────────────

static bool tryConnect() {
    String broker = RuntimeSettings::mqttBroker();
    uint16_t port = RuntimeSettings::mqttPort();

    _client.setServer(broker.c_str(), port);
    _client.setCallback(onMessage);

    String clientId = "pchub-" + RuntimeSettings::hostname();
    String user = RuntimeSettings::mqttUser();
    String pass = RuntimeSettings::mqttPassword();

    bool ok = user.isEmpty()
        ? _client.connect(clientId.c_str())
        : _client.connect(clientId.c_str(), user.c_str(), pass.c_str());

    if (ok) {
        _client.subscribe(topic("cmd").c_str());
        _activeBroker = broker;
        _activePort   = port;
        Serial.printf("MQTT: connected to %s:%u\n", broker.c_str(), port);
    } else {
        Serial.printf("MQTT: connect failed, rc=%d\n", _client.state());
    }
    return ok;
}

// ── Publish ───────────────────────────────────────────────────────────────────

static void publishAll() {
    if (_sensor && _sensor->bme_ok) {
        pub("temperature", String(_sensor->temperature, 1));
        pub("humidity",    String(_sensor->humidity, 1));
        pub("pressure",    String(_sensor->pressure, 0));
        pub("gas",         String(_sensor->gas, 0));
    }
    if (_weather && _weather->ok) {
        pub("outdoor/temperature", String(_weather->temperature, 1));
        pub("outdoor/code",        String(_weather->weather_code));
    }

    JsonDocument doc;
    if (_sensor && _sensor->bme_ok) {
        doc["temperature"] = serialized(String(_sensor->temperature, 1));
        doc["humidity"]    = serialized(String(_sensor->humidity, 1));
        doc["pressure"]    = serialized(String(_sensor->pressure, 0));
        doc["gas"]         = serialized(String(_sensor->gas, 0));
    }
    if (_weather && _weather->ok) {
        doc["outdoor_temp"] = serialized(String(_weather->temperature, 1));
        doc["weather_code"] = _weather->weather_code;
    }
    if (_sensor) doc["rtc_ok"] = _sensor->rtc_ok;
    String state;
    serializeJson(doc, state);
    pub("state", state);
}

// ── Public API ────────────────────────────────────────────────────────────────

bool connected() { return _client.connected(); }

void begin(const SensorData *sensor, const WeatherData *weather) {
    _sensor  = sensor;
    _weather = weather;
}

void handle() {
    if (!RuntimeSettings::mqttEnabled()) return;
    if (RuntimeSettings::mqttBroker().isEmpty()) return;

    if (!_client.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnect >= 15000UL) {
            _lastReconnect = now;
            tryConnect();
        }
        return;
    }

    _client.loop();

    unsigned long now = millis();
    unsigned long interval = (unsigned long)RuntimeSettings::mqttIntervalSec() * 1000UL;
    if (now - _lastPublish >= interval) {
        _lastPublish = now;
        publishAll();
    }
}

} // namespace MQTT
