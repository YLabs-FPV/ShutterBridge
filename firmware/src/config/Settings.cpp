#include "config/Settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstddef>
#include <cstring>

Settings g_settings;

static Preferences        prefs;
static constexpr uint32_t MAGIC = 0x53425247;

struct CompatVersion {
    uint16_t version;
    size_t   validBytes;
};
static const CompatVersion COMPAT[] = {
    {15, offsetof(Settings, autoWake)},
};

const char* osdFieldName(OsdField f) {
    switch (f) {
        case OsdField::Off:
            return "Off";
        case OsdField::RecStatus:
            return "Rec status";
        case OsdField::Battery:
            return "Battery %";
        case OsdField::Mode:
            return "Mode";
        case OsdField::SdFree:
            return "SD free";
        case OsdField::TimeLeft:
            return "Time left";
        case OsdField::Resolution:
            return "Resolution";
        case OsdField::Fps:
            return "FPS";
        default:
            return "?";
    }
}

void Settings::setDefaults() {
    *this = Settings();  // reset all members to their in-class defaults
    // Shutter on AUX1 high by default; in video that records while the switch is up (2-pos)
    modes[(int)Func::Shutter] = {/*aux=*/1, /*min=*/1800, /*max=*/2100};
    shutterVideoMode          = (uint8_t)ShutterVideoMode::TwoPos;
}

void Settings::load() {
    prefs.begin("shutter", /*readOnly=*/true);
    const size_t n        = prefs.getBytesLength("cfg");
    bool         ok       = false;
    bool         migrated = false;

    if (n >= offsetof(Settings, opMode) && n <= sizeof(Settings)) {
        uint8_t buf[sizeof(Settings)];
        prefs.getBytes("cfg", buf, n);
        uint32_t magic;
        uint16_t ver;
        memcpy(&magic, buf + offsetof(Settings, magic), sizeof(magic));
        memcpy(&ver, buf + offsetof(Settings, version), sizeof(ver));
        if (magic == MAGIC && ver == version && n == sizeof(Settings)) {
            memcpy(this, buf, n);
            ok = true;
        }
        for (const CompatVersion& c : COMPAT) {
            if (ok || magic != MAGIC || ver != c.version || n < c.validBytes)
                continue;
            const uint16_t current = version;
            setDefaults();  // fields added since that version keep their defaults
            memcpy(this, buf, c.validBytes);
            version = current;
            ok = migrated = true;
            Serial.printf("[cfg] migrated settings v%u -> v%u\n", ver, current);
            break;
        }
    }
    prefs.end();
    if (!ok)
        setDefaults();
    if (!ok || migrated)
        save();
}

void Settings::save() {
    prefs.begin("shutter", /*readOnly=*/false);
    prefs.putBytes("cfg", this, sizeof(*this));
    prefs.end();
}
