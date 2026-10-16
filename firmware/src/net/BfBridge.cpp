#include "net/BfBridge.h"

void BfBridge::begin() {
    if (started_)
        return;

    // Drop stale MSP reply bytes the FC poller left in the RX buffer
    while (fc_.available())
        fc_.read();

    tcp_.begin();
    tcp_.setNoDelay(true);  // MSP is latency-sensitive

    started_ = true;
}

bool BfBridge::clientConnected() {
    return tcpClient_ && tcpClient_.connected();
}

void BfBridge::loop() {
    if (!started_)
        return;

    // Accept a client; newest connection wins
    WiFiClient nc = tcp_.accept();
    if (nc) {
        if (tcpClient_)
            tcpClient_.stop();
        tcpClient_ = nc;
        tcpClient_.setNoDelay(true);
    }
    const bool up = tcpClient_ && tcpClient_.connected();

    // FC -> client; one burst per loop, leftovers picked up next loop
    uint8_t buf[256];
    size_t  n = 0;
    while (fc_.available() && n < sizeof(buf))
        buf[n++] = (uint8_t)fc_.read();
    if (n && up)
        tcpClient_.write(buf, n);

    // client -> FC
    if (up) {
        while (tcpClient_.available())
            fc_.write((uint8_t)tcpClient_.read());
    }
}
