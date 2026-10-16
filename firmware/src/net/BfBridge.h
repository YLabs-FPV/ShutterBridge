// Betaflight bridge mode: a dumb byte pump between a network client (Betaflight
// desktop Configurator, over raw TCP) and the FC's MSP UART
#pragma once

#include <Arduino.h>
#include <WiFi.h>

class BfBridge {
   public:
    explicit BfBridge(HardwareSerial& fcSerial) : fc_(fcSerial) {
    }

    void begin();
    void loop();

    // A real bridge client (TCP) is connected, not just a Wi-Fi station
    bool clientConnected();

    static constexpr uint16_t TCP_PORT = 5761;  // Betaflight desktop "TCP"

   private:
    HardwareSerial& fc_;
    WiFiServer      tcp_{TCP_PORT};
    WiFiClient      tcpClient_;
    bool            started_ = false;
};
