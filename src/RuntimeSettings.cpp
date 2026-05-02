#include "RuntimeSettings.h"
#include "Config.h"
#include <Preferences.h>

namespace RuntimeSettings {

static String _wifiSsid = WIFI_SSID;
static String _wifiPassword = WIFI_PASSWORD;
static String _ntpServer = NTP_SERVER;
static long _ntpOffsetSec = NTP_OFFSET;
static uint8_t _backlightPercent = 100;
static bool _backlightInverted = false;

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

void reload() {
    Preferences prefs;
    prefs.begin("pchub", true);
    _wifiSsid = prefs.getString("wifi_ssid", WIFI_SSID);
    _wifiPassword = prefs.getString("wifi_pass", WIFI_PASSWORD);
    _ntpServer = prefs.getString("ntp_server", NTP_SERVER);
    _ntpOffsetSec = clampOffset(prefs.getLong("ntp_offset", NTP_OFFSET));
    _backlightPercent = clampPercent(prefs.getUInt("bl_pct", 100));
    _backlightInverted = prefs.getBool("bl_inv", false);
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

long ntpOffsetSec() {
    return _ntpOffsetSec;
}

uint8_t backlightPercent() {
    return _backlightPercent;
}

bool backlightInverted() {
    return _backlightInverted;
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

} // namespace RuntimeSettings
