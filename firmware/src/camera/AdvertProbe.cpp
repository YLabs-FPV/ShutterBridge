#include "camera/AdvertProbe.h"

#include <Arduino.h>
#include <strings.h>

namespace {
    // Runs on the BLE host task; the camera task only reads `out` after `found` is set
    class ProbeCB : public NimBLEScanCallbacks {
       public:
        ProbeCB(const std::string& mac, const std::string& namePrefix, AdvertInfo& out)
            : mac_(mac), namePrefix_(namePrefix), out_(out) {
        }
        void onResult(const NimBLEAdvertisedDevice* d) override {
            if (found)
                return;
            const bool hit = !mac_.empty()
                                 ? strcasecmp(d->getAddress().toString().c_str(), mac_.c_str()) == 0
                                 : d->getName().rfind(namePrefix_, 0) == 0;
            if (!hit)
                return;
            out_.addr = d->getAddress();
            out_.name = d->getName();
            out_.mfr  = d->getManufacturerData();
            found     = true;
        }
        volatile bool found = false;

       private:
        const std::string& mac_;
        const std::string& namePrefix_;
        AdvertInfo&        out_;
    };
}  // namespace

bool probeAdvert(const std::string& mac, const std::string& namePrefix, uint32_t timeoutMs,
                 AdvertInfo& out) {
    if (mac.empty() && namePrefix.empty())
        return false;
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan->isScanning())
        return false;

    ProbeCB cb(mac, namePrefix, out);
    scan->setScanCallbacks(&cb, /*wantDuplicates=*/false);
    scan->setActiveScan(true);
    scan->clearResults();
    scan->start(timeoutMs, /*isContinue=*/false, /*restart=*/true);

    const uint32_t start = millis();
    while (!cb.found && scan->isScanning() && millis() - start < timeoutMs + 200)
        vTaskDelay(pdMS_TO_TICKS(20));

    if (scan->isScanning())
        scan->stop();
    scan->setScanCallbacks(nullptr);
    scan->clearResults();
    return cb.found;
}
