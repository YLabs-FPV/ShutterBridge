#include "duml.h"

namespace duml {

    uint8_t crc8(const uint8_t* data, size_t len) {
        uint8_t crc = 0x77;
        for (size_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (int b = 0; b < 8; b++)
                crc = (crc & 1) ? (uint8_t)((crc >> 1) ^ 0x8C) : (uint8_t)(crc >> 1);
        }
        return crc;
    }

    uint16_t crc16(const uint8_t* data, size_t len, uint16_t init) {
        uint16_t crc = init;
        for (size_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (int b = 0; b < 8; b++)
                crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
        }
        return crc;
    }

    std::vector<uint8_t> buildFrame(uint8_t sender, uint8_t receiver, uint8_t cmdSet, uint8_t cmdId,
                                    const uint8_t* payload, size_t payloadLen, uint8_t flags,
                                    uint16_t seq) {
        const size_t bodyLen = 7 + payloadLen;
        const size_t length  = 4 + bodyLen + 2;

        std::vector<uint8_t> f;
        f.reserve(length);
        f.push_back(0x55);
        f.push_back((uint8_t)(length & 0xFF));
        f.push_back((uint8_t)(0x04 | ((length >> 8) & 0x03)));
        f.push_back(crc8(f.data(), 3));
        f.push_back(sender);
        f.push_back(receiver);
        f.push_back((uint8_t)(seq & 0xFF));
        f.push_back((uint8_t)((seq >> 8) & 0xFF));
        f.push_back(flags);
        f.push_back(cmdSet);
        f.push_back(cmdId);
        for (size_t i = 0; i < payloadLen; i++)
            f.push_back(payload[i]);
        uint16_t c = crc16(f.data(), f.size());
        f.push_back((uint8_t)(c & 0xFF));
        f.push_back((uint8_t)((c >> 8) & 0xFF));
        return f;
    }

    bool parseFrame(const uint8_t* buf, size_t len, Frame& out, size_t& frameLen) {
        if (len < 12 || buf[0] != 0x55)
            return false;
        const size_t L = (size_t)buf[1] | (((size_t)(buf[2] & 0x03)) << 8);
        if (L < 12 || L > len)
            return false;
        if (crc8(buf, 3) != buf[3])
            return false;
        const uint16_t want = (uint16_t)buf[L - 2] | ((uint16_t)buf[L - 1] << 8);
        if (crc16(buf, L - 2) != want)
            return false;

        out.sender     = buf[4];
        out.receiver   = buf[5];
        out.seq        = (uint16_t)buf[6] | ((uint16_t)buf[7] << 8);
        out.flags      = buf[8];
        out.cmdSet     = buf[9];
        out.cmdId      = buf[10];
        out.payload    = buf + 11;
        out.payloadLen = L - 11 - 2;
        frameLen       = L;
        return true;
    }

    void FrameScanner::feed(const uint8_t* data, size_t len, const Handler& handler) {
        buf_.insert(buf_.end(), data, data + len);

        size_t pos = 0;
        while (true) {
            while (pos < buf_.size() && buf_[pos] != 0x55)
                pos++;
            if (pos >= buf_.size()) {
                buf_.clear();
                return;
            }
            const size_t avail = buf_.size() - pos;
            if (avail < 4)
                break;
            const size_t L = (size_t)buf_[pos + 1] | (((size_t)(buf_[pos + 2] & 0x03)) << 8);
            if (L < 12 || L > 400) {
                pos++;
                continue;
            }
            if (avail < L)
                break;

            Frame  f;
            size_t fl = 0;
            if (parseFrame(&buf_[pos], avail, f, fl)) {
                handler(f);
                pos += fl;
            } else {
                pos++;
            }
        }

        if (pos > 0)
            buf_.erase(buf_.begin(), buf_.begin() + pos);
        if (buf_.size() > 4000)
            buf_.clear();
    }

}  // namespace duml
