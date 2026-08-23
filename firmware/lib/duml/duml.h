#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// DJI DUML wire protocol (SOF 0x55): framing, CRCs and a streaming reassembler
// used by the Osmo series like the Nano (and telemetry on Action series)
namespace duml {

    enum Addr : uint8_t {
        ADDR_APP     = 0x02,
        ADDR_CAM     = 0x01,
        ADDR_BATTERY = 0x05,
        ADDR_PARAM   = 0x28,
    };

    enum Flags : uint8_t {
        FLAG_CMD  = 0x40,
        FLAG_ACK  = 0xC0,
        FLAG_PUSH = 0x00,
    };

    uint8_t  crc8(const uint8_t* data, size_t len);
    uint16_t crc16(const uint8_t* data, size_t len, uint16_t init = 0x3692);

    struct Frame {
        uint8_t        sender     = 0;
        uint8_t        receiver   = 0;
        uint16_t       seq        = 0;
        uint8_t        flags      = 0;
        uint8_t        cmdSet     = 0;
        uint8_t        cmdId      = 0;
        const uint8_t* payload    = nullptr;
        size_t         payloadLen = 0;
    };

    std::vector<uint8_t> buildFrame(uint8_t sender, uint8_t receiver, uint8_t cmdSet, uint8_t cmdId,
                                    const uint8_t* payload, size_t payloadLen,
                                    uint8_t flags = FLAG_CMD, uint16_t seq = 0);

    inline std::vector<uint8_t> buildFrame(uint8_t sender, uint8_t receiver, uint8_t cmdSet,
                                           uint8_t cmdId, const std::vector<uint8_t>& payload,
                                           uint8_t flags = FLAG_CMD, uint16_t seq = 0) {
        return buildFrame(sender, receiver, cmdSet, cmdId, payload.data(), payload.size(), flags,
                          seq);
    }

    bool parseFrame(const uint8_t* buf, size_t len, Frame& out, size_t& frameLen);

    // Notifications can pack several frames together and split large ones across packets;
    // feed raw bytes and handler runs once per complete, CRC-valid frame
    class FrameScanner {
       public:
        using Handler = std::function<void(const Frame&)>;
        void feed(const uint8_t* data, size_t len, const Handler& handler);
        void reset() {
            buf_.clear();
        }

       private:
        std::vector<uint8_t> buf_;
    };

}  // namespace duml
