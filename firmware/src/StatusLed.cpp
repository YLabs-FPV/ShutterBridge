#include "StatusLed.h"

#include <Arduino.h>

void StatusLed::begin() {
    if (pin_ < 0)
        return;
    write(0, 0, 0);
}

void StatusLed::write(uint8_t r, uint8_t g, uint8_t b) {
    if (pin_ < 0)
        return;
    if (r == lr_ && g == lg_ && b == lb_)
        return;
    lr_ = r;
    lg_ = g;
    lb_ = b;
    neopixelWrite(pin_, g, r, b);
}

void StatusLed::update(bool camOnline, bool recording, bool wifiClient) {
    if (pin_ < 0)
        return;
    const uint32_t now = millis();

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
