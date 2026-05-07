#include "RuntimeSettings.h"
#include "Config.h"
#include <Preferences.h>

namespace RuntimeSettings {

static String _wifiSsid = WIFI_SSID;
static String _wifiPassword = WIFI_PASSWORD;
static String _ntpServer = NTP_SERVER;
static String _deviceName = DEVICE_NAME;
static String _hostname = DEVICE_NAME;
static String _weatherCity = WEATHER_CITY;
static bool _windMetric = WIND_UNIT_MS != 0;
static long _ntpOffsetSec = NTP_OFFSET;
static uint8_t _backlightPercent = 100;
static bool _backlightInverted = false;
static bool _autoBacklight = false;
static uint8_t _ledMode = 0;
static bool _pcEnabled = true;

static long clampOffset(long offsetSec) {
    if (offsetSec < -12L * 3600L) return -12L * 3600L;
    if (offsetSec >  14L * 3600L) return  14L * 3600L;
    return offsetSec;
}

static uint8_t clampPercent(int percent) {
    if (percent < 0) return 0;
    if (percent > 100) return 100;
    return (uint8_t)percent;
}

static String cleanLabel(String value, const char *fallback) {
    value.trim();
    if (value.length() == 0) return String(fallback);
    if (value.length() > 31) value = value.substring(0, 31);
    return value;
}

static String cleanHostname(String value, const char *fallback) {
    value.toLowerCase();
    value.trim();
    String out;
    out.reserve(value.length());
    for (size_t i = 0; i < value.length() && out.length() < 31; i++) {
        char c = value[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
            out += c;
        } else if (c == '_' || c == ' ' || c == '.') {
            out += '-';
        }
    }
    while (out.startsWith("-")) out.remove(0, 1);
    while (out.endsWith("-")) out.remove(out.length() - 1);
    return out.length() ? out : String(fallback);
}

void reload() {
    Preferences prefs;
    prefs.begin("pchub", true);
    _wifiSsid = prefs.getString("wifi_ssid", WIFI_SSID);
    _wifiPassword = prefs.getString("wifi_pass", WIFI_PASSWORD);
    _ntpServer = prefs.getString("ntp_server", NTP_SERVER);
    _deviceName = cleanLabel(prefs.getString("dev_name", DEVICE_NAME), DEVICE_NAME);
    _hostname = cleanHostname(prefs.getString("hostname", DEVICE_NAME), DEVICE_NAME);
    _weatherCity = prefs.getString("weather_city", WEATHER_CITY);
    _windMetric = prefs.getBool("wind_ms", WIND_UNIT_MS != 0);
    _ntpOffsetSec = clampOffset(prefs.getLong("ntp_offset", NTP_OFFSET));
    _backlightPercent = clampPercent(prefs.getUInt("bl_pct", 100));
    _backlightInverted = prefs.getBool("bl_inv", false);
    _autoBacklight     = prefs.getBool("bl_auto", false);
    _ledMode = prefs.getUChar("led_mode", 0);
    _pcEnabled = prefs.getBool("pc_enabled", true);
    prefs.end();
}

void begin() {
    reload();
}

String wifiSsid() {
    return _wifiSsid;
}

String wifiPassword() {
    return _wifiPassword;
}

String ntpServer() {
    return _ntpServer;
}

String deviceName() {
    return _deviceName;
}

String hostname() {
    return _hostname;
}

String weatherCity() {
    return _weatherCity;
}

bool windMetric() {
    return _windMetric;
}

long ntpOffsetSec() {
    return _ntpOffsetSec;
}

uint8_t backlightPercent() {
    return _backlightPercent;
}

bool backlightInverted() {
    return _backlightInverted;
}

bool autoBacklight() {
    return _autoBacklight;
}

uint8_t ledMode() {
    return _ledMode;
}

bool pcEnabled() {
    return _pcEnabled;
}

void saveWifi(const String &ssid, const String &password) {
    Preferences prefs;
    prefs.begin("pchub", false);
    if (ssid.length() > 0) prefs.putString("wifi_ssid", ssid);
    if (password.length() > 0) prefs.putString("wifi_pass", password);
    prefs.end();
    reload();
}

void saveNtp(const String &server, long offsetSec) {
    Preferences prefs;
    prefs.begin("pchub", false);
    if (server.length() > 0) prefs.putString("ntp_server", server);
    prefs.putLong("ntp_offset", clampOffset(offsetSec));
    prefs.end();
    reload();
}

void saveDeviceIdentity(const String &deviceName, const String &hostname) {
    String cleanName = cleanLabel(deviceName, DEVICE_NAME);
    String cleanHost = cleanHostname(hostname, DEVICE_NAME);

    Preferences prefs;
    prefs.begin("pchub", false);
    prefs.putString("dev_name", cleanName);
    prefs.putString("hostname", cleanHost);
    prefs.end();
    reload();
}

void saveWeatherCity(const String &city) {
    String clean = city;
    clean.trim();
    if (clean.length() == 0) return;

    Preferences prefs;
    prefs.begin("pchub", false);
    prefs.putString("weather_city", clean);
    prefs.end();
    reload();
}

void saveWindMetric(bool metric) {
    Preferences prefs;
    prefs.begin("pchub", false);
    prefs.putBool("wind_ms", metric);
    prefs.end();
    reload();
}

void saveBacklight(uint8_t percent) {
    Preferences prefs;
    prefs.begin("pchub", false);
    prefs.putUInt("bl_pct", clampPercent(percent));
    prefs.end();
    reload();
}

void saveBacklightInverted(bool inverted) {
    Preferences prefs;
    prefs.begin("pchub", false);
    prefs.putBool("bl_inv", inverted);
    prefs.end();
    reload();
}

void saveAutoBacklight(bool enabled) {
    Preferences prefs;
    prefs.begin("pchub", false);
    prefs.putBool("bl_auto", enabled);
    prefs.end();
    reload();
}

void saveLedMode(uint8_t mode) {
    if (mode > 3) mode = 0;
    Preferences prefs;
    prefs.begin("pchub", false);
    prefs.putUChar("led_mode", mode);
    prefs.end();
    reload();
}

void savePcEnabled(bool enabled) {
    Preferences prefs;
    prefs.begin("pchub", false);
    prefs.putBool("pc_enabled", enabled);
    prefs.end();
    reload();
}

} // namespace RuntimeSettings
