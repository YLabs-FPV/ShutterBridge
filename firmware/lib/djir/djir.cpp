#include "djir.h"

namespace djir {

    uint16_t crc16(const uint8_t* d, size_t n) {
        uint16_t crc = 0x3AA3;
        for (size_t i = 0; i < n; i++) {
            crc ^= d[i];
            for (int b = 0; b < 8; b++)
                crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
        return crc;
    }

    uint32_t crc32(const uint8_t* d, size_t n) {
        uint32_t crc = 0x00003AA3;
        for (size_t i = 0; i < n; i++) {
            crc ^= d[i];
            for (int b = 0; b < 8; b++)
                crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
        return crc;
    }

    std::vector<uint8_t> buildFrame(uint8_t cmdSet, uint8_t cmdId, uint8_t cmdType,
                                    const uint8_t* payload, size_t payloadLen, uint16_t seq) {
        const size_t         total = 12 + 2 + payloadLen + 4;
        std::vector<uint8_t> f(total, 0);
        f[0]                  = 0xAA;
        const uint16_t verlen = (uint16_t)(total & 0x03FF);
        f[1]                  = verlen & 0xFF;
        f[2]                  = (verlen >> 8) & 0xFF;
        f[3]                  = cmdType;
        f[8]                  = seq & 0xFF;
        f[9]                  = (seq >> 8) & 0xFF;
        const uint16_t c16    = crc16(f.data(), 10);
        f[10]                 = c16 & 0xFF;
        f[11]                 = (c16 >> 8) & 0xFF;
        f[12]                 = cmdSet;
        f[13]                 = cmdId;
        for (size_t i = 0; i < payloadLen; i++)
            f[14 + i] = payload[i];
        const uint32_t c32 = crc32(f.data(), total - 4);
        f[total - 4]       = c32 & 0xFF;
        f[total - 3]       = (c32 >> 8) & 0xFF;
        f[total - 2]       = (c32 >> 16) & 0xFF;
        f[total - 1]       = (c32 >> 24) & 0xFF;
        return f;
    }

    void FrameScanner::feed(const uint8_t* data, size_t len, const Handler& handler) {
        buf_.insert(buf_.end(), data, data + len);
        for (;;) {
            size_t start = 0;
            while (start < buf_.size() && buf_[start] != 0xAA)
                start++;
            if (start > 0)
                buf_.erase(buf_.begin(), buf_.begin() + start);
            if (buf_.size() < 12)
                break;
            const size_t fl = (size_t)(buf_[1] | ((buf_[2] & 0x03) << 8));
            if (fl < 16 || fl > 512) {
                buf_.erase(buf_.begin());
                continue;
            }
            if (buf_.size() < fl)
                break;
            const uint8_t* f    = buf_.data();
            const uint16_t rc16 = (uint16_t)(f[10] | (f[11] << 8));
            const uint32_t rc32 = (uint32_t)f[fl - 4] | ((uint32_t)f[fl - 3] << 8) |
                                  ((uint32_t)f[fl - 2] << 16) | ((uint32_t)f[fl - 1] << 24);
            if (crc16(f, 10) == rc16 && crc32(f, fl - 4) == rc32 && fl >= 18) {
                Frame fr;
                fr.cmdType    = f[3];
                fr.seq        = (uint16_t)(f[8] | (f[9] << 8));
                fr.cmdSet     = f[12];
                fr.cmdId      = f[13];
                fr.payload    = f + 14;
                fr.payloadLen = fl - 18;
                handler(fr);
                buf_.erase(buf_.begin(), buf_.begin() + fl);
            } else {
                buf_.erase(buf_.begin());
            }
        }
        if (buf_.size() > 2048)
            buf_.clear();
    }

}  // namespace djir
