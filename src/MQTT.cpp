#include "MQTT.h"
#include "RuntimeSettings.h"
#include "Version.h"
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

// ── HA MQTT Discovery ─────────────────────────────────────────────────────────

static void pubDiscovery(const char *sensor_id, const char *name,
                         const char *state_suffix, const char *unit,
                         const char *device_class, const char *icon = nullptr) {
    String uid       = RuntimeSettings::hostname() + "_" + sensor_id;
    String cfg_topic = "homeassistant/sensor/" + uid + "/config";

    JsonDocument doc;
    doc["name"]        = name;
    doc["unique_id"]   = uid;
    doc["state_topic"] = topic(state_suffix);
    doc["state_class"] = "measurement";
    if (unit && *unit)                 doc["unit_of_measurement"] = unit;
    if (device_class && *device_class) doc["device_class"]        = device_class;
    if (icon && *icon)                 doc["icon"]                = icon;

    JsonObject dev        = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = RuntimeSettings::hostname();
    dev["name"]           = RuntimeSettings::deviceName();
    dev["model"]          = "PCHUB";
    dev["sw_version"]     = FW_VERSION;

    String payload;
    serializeJson(doc, payload);
    _client.publish(cfg_topic.c_str(), payload.c_str(), true);
}

static void publishDiscovery() {
    pubDiscovery("temperature", "Temperature",        "temperature",        "\xc2\xb0""C", "temperature",   nullptr);
    pubDiscovery("humidity",    "Humidity",           "humidity",           "%",            "humidity",      nullptr);
    pubDiscovery("pressure",    "Pressure",           "pressure",           "hPa",          "pressure",      nullptr);
    pubDiscovery("co2",         "CO2",                "co2",                "ppm",          "carbon_dioxide",nullptr);
    pubDiscovery("iaq",         "IAQ",                "iaq",                nullptr,        nullptr,         "mdi:air-filter");
    pubDiscovery("gas",         "Gas Resistance",     "gas",                "k\xe2\x84\xa6",nullptr,        "mdi:leak");
    if (!RuntimeSettings::weatherCity().isEmpty()) {
        pubDiscovery("outdoor_temp", "Outdoor Temperature", "outdoor/temperature",
                     "\xc2\xb0""C", "temperature", nullptr);
    }
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
        publishDiscovery();
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
        if (_sensor->iaq_accuracy > 0) {
            pub("gas", String(_sensor->gas, 0));
            pub("iaq", String(_sensor->iaq, 0));
            pub("co2", String(_sensor->co2, 0));
        }
    }
    if (_weather && _weather->ok) {
        pub("outdoor/temperature", String(_weather->temperature, 1));
        pub("outdoor/code",        String(_weather->weather_code));
    }

    JsonDocument doc;
    if (_sensor && _sensor->bme_ok) {
        doc["temperature"]    = serialized(String(_sensor->temperature, 1));
        doc["humidity"]       = serialized(String(_sensor->humidity, 1));
        doc["pressure"]       = serialized(String(_sensor->pressure, 0));
        doc["iaq_accuracy"]   = _sensor->iaq_accuracy;
        if (_sensor->iaq_accuracy > 0) {
            doc["gas"] = serialized(String(_sensor->gas, 0));
            doc["iaq"] = serialized(String(_sensor->iaq, 0));
            doc["co2"] = serialized(String(_sensor->co2, 0));
        }
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
    _client.setBufferSize(512);
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
