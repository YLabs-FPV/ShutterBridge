#pragma once

#include <cstdint>

class StatusLed {
   public:
    StatusLed(int pin, bool rgb, bool activeLow) : pin_(pin), rgb_(rgb), activeLow_(activeLow) {
    }
    void begin();
    void update(bool camOnline, bool recording, bool wifiClient);
    void showBridge(bool clientConnected);

   private:
    void updateRgb(bool camOnline, bool recording, bool wifiClient, uint32_t now);
    void updateMono(bool camOnline, bool recording, bool wifiClient, uint32_t now);
    void showBridgeRgb(bool clientConnected);
    void showBridgeMono(bool clientConnected);
    void write(uint8_t r, uint8_t g, uint8_t b);
    void writeMono(bool on);

    int     pin_;
    bool    rgb_;
    bool    activeLow_;
    uint8_t lr_ = 1, lg_ = 1, lb_ = 1;
    int8_t  lon_ = -1;
};
