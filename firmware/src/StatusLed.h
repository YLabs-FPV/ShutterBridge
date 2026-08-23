#pragma once

#include <cstdint>

class StatusLed {
   public:
    explicit StatusLed(int pin) : pin_(pin) {
    }
    void begin();
    void update(bool camOnline, bool recording, bool wifiClient);

   private:
    void write(uint8_t r, uint8_t g, uint8_t b);

    int     pin_;
    uint8_t lr_ = 1, lg_ = 1, lb_ = 1;
};
