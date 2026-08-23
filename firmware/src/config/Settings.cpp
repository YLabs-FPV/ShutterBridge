#include "config/Settings.h"

#include <Preferences.h>

Settings g_settings;

static Preferences        prefs;
static constexpr uint32_t MAGIC = 0x53425247;

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
    const size_t n  = prefs.getBytesLength("cfg");
    bool         ok = false;
    if (n == sizeof(Settings)) {
        Settings tmp;
        prefs.getBytes("cfg", &tmp, sizeof(tmp));
        if (tmp.magic == MAGIC && tmp.version == version) {
            *this = tmp;
            ok    = true;
        }
    }
    prefs.end();
    if (!ok) {
        setDefaults();
        save();
    }
}

void Settings::save() {
    prefs.begin("shutter", /*readOnly=*/false);
    prefs.putBytes("cfg", this, sizeof(*this));
    prefs.end();
}
