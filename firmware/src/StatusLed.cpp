#include "StatusLed.h"

#include <Arduino.h>

void StatusLed::begin() {
    if (pin_ < 0)
        return;
    if (rgb_) {
        write(0, 0, 0);
    } else {
        pinMode(pin_, OUTPUT);
        writeMono(false);
    }
}

void StatusLed::write(uint8_t r, uint8_t g, uint8_t b) {
    if (r == lr_ && g == lg_ && b == lb_)
        return;
    lr_ = r;
    lg_ = g;
    lb_ = b;
    neopixelWrite(pin_, g, r, b);
}

void StatusLed::writeMono(bool on) {
    if (lon_ == (int8_t)on)
        return;
    lon_ = on;
    digitalWrite(pin_, on != activeLow_ ? HIGH : LOW);
}

void StatusLed::update(bool camOnline, bool recording, bool wifiClient) {
    if (pin_ < 0)
        return;
    const uint32_t now = millis();
    if (rgb_)
        updateRgb(camOnline, recording, wifiClient, now);
    else
        updateMono(camOnline, recording, wifiClient, now);
}

void StatusLed::updateRgb(bool camOnline, bool recording, bool wifiClient, uint32_t now) {
    uint8_t r, g, b;
    if (camOnline) {
        if (recording) {
            // blinking red = recording (like the Nano)
            const bool on = (now / 400) % 2;
            r             = on ? 60 : 0;
            g             = 0;
            b             = 0;
        } else {
            // solid green = camera connected, idle
            r = 0;
            g = 40;
            b = 0;
        }
    } else if (wifiClient) {
        // solid blue = a device is connected to the config Wi-Fi (camera offline)
        r = 0;
        g = 0;
        b = 50;
    } else {
        // slow yellow blink = searching for the camera
        const bool on = (now / 500) % 2;
        r             = on ? 40 : 0;
        g             = on ? 22 : 0;
        b             = 0;
    }

    write(r, g, b);
}

// Same states as updateRgb(), mapped onto blink rhythms for a single-colour LED
void StatusLed::updateMono(bool camOnline, bool recording, bool wifiClient, uint32_t now) {
    bool on;
    if (camOnline) {
        // fast blink = recording, solid = camera connected, idle
        on = recording ? (now / 125) % 2 : true;
    } else if (wifiClient) {
        // double flash = a device is connected to the config Wi-Fi (camera offline)
        const uint32_t t = now % 1500;
        on               = t < 100 || (t >= 250 && t < 350);
    } else {
        // slow blink = searching for the camera
        on = (now / 500) % 2;
    }

    writeMono(on);
}
