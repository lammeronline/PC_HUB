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
static bool     _tgEnabled     = false;
static String   _tgToken;
static String   _tgChatId;
static bool     _tgTempHiEn   = false;
static float    _tgTempHi     = 30.0f;
static bool     _tgTempLoEn   = false;
static float    _tgTempLo     = 15.0f;
static bool     _tgHumHiEn    = false;
static float    _tgHumHi      = 75.0f;
static bool     _tgHumLoEn    = false;
static float    _tgHumLo      = 30.0f;
static bool     _tgGasLoEn    = false;
static float    _tgGasLo      = 50.0f;
static uint16_t _tgCooldownMin = 10;

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
    _tgEnabled    = prefs.getBool("tg_en", false);
    _tgToken      = prefs.getString("tg_token", "");
    _tgChatId     = prefs.getString("tg_chat_id", "");
    _tgTempHiEn   = prefs.getBool("tg_thi_en", false);
    _tgTempHi     = prefs.getFloat("tg_thi", 30.0f);
    _tgTempLoEn   = prefs.getBool("tg_tlo_en", false);
    _tgTempLo     = prefs.getFloat("tg_tlo", 15.0f);
    _tgHumHiEn    = prefs.getBool("tg_hhi_en", false);
    _tgHumHi      = prefs.getFloat("tg_hhi", 75.0f);
    _tgHumLoEn    = prefs.getBool("tg_hlo_en", false);
    _tgHumLo      = prefs.getFloat("tg_hlo", 30.0f);
    _tgGasLoEn    = prefs.getBool("tg_glo_en", false);
    _tgGasLo      = prefs.getFloat("tg_glo", 50.0f);
    _tgCooldownMin = (uint16_t)prefs.getUInt("tg_cooldown", 10);
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

bool     tgEnabled()      { return _tgEnabled; }
String   tgToken()        { return _tgToken; }
String   tgChatId()       { return _tgChatId; }
bool     tgTempHiEn()     { return _tgTempHiEn; }
float    tgTempHi()       { return _tgTempHi; }
bool     tgTempLoEn()     { return _tgTempLoEn; }
float    tgTempLo()       { return _tgTempLo; }
bool     tgHumHiEn()      { return _tgHumHiEn; }
float    tgHumHi()        { return _tgHumHi; }
bool     tgHumLoEn()      { return _tgHumLoEn; }
float    tgHumLo()        { return _tgHumLo; }
bool     tgGasLoEn()      { return _tgGasLoEn; }
float    tgGasLo()        { return _tgGasLo; }
uint16_t tgCooldownMin()  { return _tgCooldownMin; }

void saveTgEnabled(bool enabled) {
    Preferences prefs;
    prefs.begin("pchub", false);
    prefs.putBool("tg_en", enabled);
    prefs.end();
    reload();
}

void saveTgCredentials(const String &token, const String &chatId) {
    Preferences prefs;
    prefs.begin("pchub", false);
    if (token.length() > 0) prefs.putString("tg_token", token);
    prefs.putString("tg_chat_id", chatId);
    prefs.end();
    reload();
}

void saveTgThresholds(bool tempHiEn, float tempHi,
                      bool tempLoEn, float tempLo,
                      bool humHiEn,  float humHi,
                      bool humLoEn,  float humLo,
                      bool gasLoEn,  float gasLo,
                      uint16_t cooldownMin) {
    if (cooldownMin < 1) cooldownMin = 1;
    Preferences prefs;
    prefs.begin("pchub", false);
    prefs.putBool("tg_thi_en", tempHiEn); prefs.putFloat("tg_thi", tempHi);
    prefs.putBool("tg_tlo_en", tempLoEn); prefs.putFloat("tg_tlo", tempLo);
    prefs.putBool("tg_hhi_en", humHiEn);  prefs.putFloat("tg_hhi", humHi);
    prefs.putBool("tg_hlo_en", humLoEn);  prefs.putFloat("tg_hlo", humLo);
    prefs.putBool("tg_glo_en", gasLoEn);  prefs.putFloat("tg_glo", gasLo);
    prefs.putUInt("tg_cooldown", cooldownMin);
    prefs.end();
    reload();
}

} // namespace RuntimeSettings
