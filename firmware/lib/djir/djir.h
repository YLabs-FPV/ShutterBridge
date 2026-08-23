#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// DJI R SDK wire protocol (SOF 0xAA), used by the Action series and Osmo 360 for pairing
// and control (https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/main/docs/protocol.md)
namespace djir {

    enum CmdType : uint8_t {
        CMD_NO_RESPONSE     = 0x00,
        CMD_RESPONSE_OR_NOT = 0x01,
        CMD_WAIT_RESULT     = 0x02,
        ACK_NO_RESPONSE     = 0x20,
    };

    uint16_t crc16(const uint8_t* d, size_t n);
    uint32_t crc32(const uint8_t* d, size_t n);

    struct Frame {
        uint8_t        cmdType    = 0;
        uint16_t       seq        = 0;
        uint8_t        cmdSet     = 0;
        uint8_t        cmdId      = 0;
        const uint8_t* payload    = nullptr;
        size_t         payloadLen = 0;
        bool           isResponse() const {
            return (cmdType & 0x20) != 0;
        }
    };

    std::vector<uint8_t> buildFrame(uint8_t cmdSet, uint8_t cmdId, uint8_t cmdType,
                                    const uint8_t* payload, size_t payloadLen, uint16_t seq);

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

}  // namespace djir
