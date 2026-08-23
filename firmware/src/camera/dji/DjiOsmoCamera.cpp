#include "camera/dji/DjiOsmoCamera.h"

#include <Arduino.h>

#include "camera/BleTxPower.h"
#include "net/RadioCoex.h"

namespace {
    constexpr uint16_t UUID_SERVICE = 0xFFF0;
    constexpr uint16_t UUID_WRITE   = 0xFFF5;
    constexpr uint16_t UUID_NOTIFY  = 0xFFF4;

    // Opaque identify handshake, replayed as-is to open the control gate
    const uint8_t IDENTIFY[] = {0x55, 0x22, 0x04, 0xea, 0x02, 0x07, 0x6b, 0xb9, 0x40,
                                0x07, 0x45, 0x0f, 0x30, 0x30, 0x31, 0x37, 0x34, 0x38,
                                0x30, 0x38, 0x39, 0x34, 0x35, 0x33, 0x33, 0x35, 0x32,
                                0x04, 0x32, 0x30, 0x35, 0x30, 0xc7, 0x88};

    inline uint16_t le16(const uint8_t* p) {
        return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    }
    inline uint32_t le32(const uint8_t* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
               ((uint32_t)p[3] << 24);
    }
}  // namespace

class DjiOsmoCamera::ClientCB : public NimBLEClientCallbacks {
   public:
    explicit ClientCB(DjiOsmoCamera* owner) : owner_(owner) {
    }
    void onDisconnect(NimBLEClient*, int reason) override {
        owner_->connected_ = false;
        owner_->chWrite_   = nullptr;
        owner_->chNotify_  = nullptr;
        owner_->status_    = CameraStatus{};
        Serial.printf("[dji] disconnected (reason 0x%02x)\n", reason);
    }

   private:
    DjiOsmoCamera* owner_;
};

DjiOsmoCamera::DjiOsmoCamera(const char* macAddress, const char* namePrefix)
    : mac_(macAddress ? macAddress : ""), namePrefix_(namePrefix ? namePrefix : "") {
}

bool DjiOsmoCamera::begin() {
    NimBLEDevice::init("ShutterBridge");
    applyBleTxPower();
    cb_ = new ClientCB(this);
    return attempt();
}

bool DjiOsmoCamera::attempt() {
    if (!haveAddr_) {
        if (!resolveAddress())
            return false;
        haveAddr_ = true;
    }
    return connect();
}

bool DjiOsmoCamera::resolveAddress() {
    if (mac_.empty())
        return false;
    addr_ = NimBLEAddress(mac_, BLE_ADDR_PUBLIC);
    Serial.printf("[dji] bound MAC %s\n", addr_.toString().c_str());
    return true;
}

bool DjiOsmoCamera::connect() {
    Serial.printf("[dji] connecting to %s ...\n", addr_.toString().c_str());
    if (!client_)
        client_ = NimBLEDevice::createClient();
    client_->setClientCallbacks(cb_, /*deleteCallbacks=*/false);
    client_->setConnectTimeout(g_apHasClient ? 2000 : 3000);

    if (!client_->connect(addr_)) {
        Serial.println("[dji] connect failed");
        return false;
    }

    NimBLERemoteService* svc = client_->getService(NimBLEUUID(UUID_SERVICE));
    if (!svc) {
        Serial.println("[dji] service fff0 not found");
        client_->disconnect();
        return false;
    }
    chNotify_ = svc->getCharacteristic(NimBLEUUID(UUID_NOTIFY));
    chWrite_  = svc->getCharacteristic(NimBLEUUID(UUID_WRITE));
    if (!chNotify_ || !chWrite_) {
        Serial.println("[dji] fff4/fff5 not found");
        client_->disconnect();
        return false;
    }

    scanner_.reset();
    status_ = CameraStatus{};
    chNotify_->subscribe(
        true, [this](NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) { onNotify(d, n); });

    connected_        = true;
    status_.connected = true;
    connectedAtMs_    = millis();
    Serial.println("[dji] connected - sending identify");
    doHandshake();
    return true;
}

void DjiOsmoCamera::poll() {
    const uint32_t now = millis();

    // Drop the link when telemetry stalls: BLE supervision timeout is slow, and a reconnect
    // can "succeed" onto a dead link
    if (connected_) {
        const bool stale       = status_.lastUpdateMs != 0 && (now - status_.lastUpdateMs) > 4000;
        const bool noTelemetry = status_.lastUpdateMs == 0 && (now - connectedAtMs_) > 4000;
        if (stale || noTelemetry) {
            Serial.printf("[dji] link dead (%s) - dropping\n", stale ? "stale" : "no telemetry");
            if (client_)
                client_->disconnect();
            connected_ = false;
            status_    = CameraStatus{};
        }
    }

    // Reconnect at exponential backoff, but throttle while someone is on the Web UI
    const uint32_t kApRetryMs = 8000;
    const uint32_t interval = (g_apHasClient && backoffMs_ < kApRetryMs) ? kApRetryMs : backoffMs_;
    if (!connected_ && (now - lastAttemptMs_ > interval)) {
        lastAttemptMs_ = now;
        if (attempt()) {
            backoffMs_ = kBackoffMinMs;
        } else {
            backoffMs_ = backoffMs_ < kBackoffMaxMs ? backoffMs_ * 2 : kBackoffMaxMs;
            if (backoffMs_ > kBackoffMaxMs)
                backoffMs_ = kBackoffMaxMs;
        }
    }
}

void DjiOsmoCamera::doHandshake() {
    writeRaw(IDENTIFY, sizeof(IDENTIFY));
    delay(300);
    const uint8_t p_init[]  = {0x00, 0x00, 0x00, 0x00};
    const uint8_t p_info[]  = {0x31, 0x31, 0x00, 0x00, 0x00};
    const uint8_t p_cap[]   = {0x00, 0x01, 0x1c, 0x00};
    const uint8_t p_name[]  = {0xa0};
    const uint8_t p_token[] = {0xb3};
    writeCmd(0x53, 0x10, p_init, sizeof(p_init));
    writeCmd(0x00, 0x32, p_info, sizeof(p_info));
    writeCmd(0x02, 0x8e, p_cap, sizeof(p_cap));
    writeCmd(0x07, 0x07, p_name, sizeof(p_name));
    writeCmd(0x07, 0x0e, p_token, sizeof(p_token));
    writeCmd(0x07, 0x0c, nullptr, 0);
}

bool DjiOsmoCamera::writeRaw(const uint8_t* data, size_t len) {
    if (!connected_ || !chWrite_)
        return false;
    return chWrite_->writeValue(data, len, /*response=*/false);
}

bool DjiOsmoCamera::writeCmd(uint8_t cmdSet, uint8_t cmdId, const uint8_t* payload,
                             size_t payloadLen) {
    auto frame = duml::buildFrame(duml::ADDR_APP, duml::ADDR_CAM, cmdSet, cmdId, payload,
                                  payloadLen, duml::FLAG_CMD, seq_++);
    return writeRaw(frame.data(), frame.size());
}

bool DjiOsmoCamera::startRecord() {
    if (!status_.ready) {  // asleep/standby: commanding it now wakes it erratically
        Serial.println("[dji] startRecord ignored - camera not in a shooting mode");
        return false;
    }
    const uint8_t p = 0x01;
    return writeCmd(0x02, 0x02, &p, 1);
}
bool DjiOsmoCamera::stopRecord() {
    const uint8_t p = 0x00;
    return writeCmd(0x02, 0x02, &p, 1);
}
bool DjiOsmoCamera::takePhoto() {
    if (!status_.ready)
        return false;
    const uint8_t p = 0x01;
    return writeCmd(0x02, 0x01, &p, 1);  // captures only while the camera is in photo mode
}

void DjiOsmoCamera::onNotify(const uint8_t* data, size_t len) {
    scanner_.feed(data, len, [this](const duml::Frame& f) { handleFrame(f); });
}

void DjiOsmoCamera::handleFrame(const duml::Frame& f) {
    const uint8_t* p = f.payload;
    const size_t   n = f.payloadLen;

    if (f.cmdSet == 0x02 && f.cmdId == 0x80 && n >= 33) {  // camera state
        const uint32_t masked   = le32(p + 0);
        const uint8_t  workMode = p[4];  // 0 photo, 1 video, else playback/sleep/standby
        if (workMode != lastWorkMode_) {
            Serial.printf("[dji] work_mode=%u%s\n", workMode,
                          workMode > 1 ? " (not shooting)" : "");
            lastWorkMode_ = workMode;
        }
        status_.ready        = (workMode <= 1);  // only photo/video are awake shooting states
        status_.mode         = (workMode == 0)   ? CamMode::Photo
                               : (workMode == 1) ? CamMode::Video
                                                 : CamMode::Unknown;
        status_.recState     = static_cast<RecState>((masked >> 6) & 0x3);
        status_.sdFreeMB     = le32(p + 9);
        status_.recLeftS     = le32(p + 17);
        status_.recElapsedS  = le32(p + 29);
        status_.lastUpdateMs = millis();
        return;
    }
    if (f.cmdSet == 0x0d && f.cmdId == 0x02 && n >= 21) {  // battery
        status_.batteryMv  = le16(p + 1);
        status_.batteryPct = p[20];
        return;
    }
    if (f.cmdSet == 0x07 && f.cmdId == 0x45 && n >= 2) {
        Serial.printf("[dji] identify ACK %02x%02x %s\n", p[0], p[1],
                      (p[0] == 0x00 && p[1] == 0x01) ? "OK" : "?");
        return;
    }
}
