#pragma once

#include <cstdint>

class StatusLed {
   public:
    // rgb: addressable WS2812 (states told apart by colour). Otherwise a plain single-colour
    // LED on a GPIO, where the states are told apart by blink pattern instead
    StatusLed(int pin, bool rgb, bool activeLow) : pin_(pin), rgb_(rgb), activeLow_(activeLow) {
    }
    void begin();
    void update(bool camOnline, bool recording, bool wifiClient);

   private:
    void updateRgb(bool camOnline, bool recording, bool wifiClient, uint32_t now);
    void updateMono(bool camOnline, bool recording, bool wifiClient, uint32_t now);
    void write(uint8_t r, uint8_t g, uint8_t b);
    void writeMono(bool on);

    int     pin_;
    bool    rgb_;
    bool    activeLow_;
    uint8_t lr_ = 1, lg_ = 1, lb_ = 1;
    int8_t  lon_ = -1;
};
