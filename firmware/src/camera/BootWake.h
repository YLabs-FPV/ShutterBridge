#pragma once

#include <Arduino.h>

class BootWake {
   public:
    void start() {
        startedMs_ = millis();
        active_    = true;
    }
    void finish() {
        active_ = false;
    }
    bool active() const {
        return active_;
    }
    uint32_t ageMs() const {
        return millis() - startedMs_;
    }
    bool expired() const {
        return active_ && ageMs() > kTimeoutMs;
    }

    static constexpr uint32_t kTimeoutMs = 30000;

   private:
    bool     active_    = false;
    uint32_t startedMs_ = 0;
};
