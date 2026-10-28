#pragma once

#include <NimBLEDevice.h>

#include <string>

// Short active scan for one camera, returning as soon as it is seen. Runs on the camera task;
// skipped (returns false) while another scan, e.g. the Web UI's, is already running
struct AdvertInfo {
    NimBLEAddress addr;
    std::string   name;
    std::string   mfr;  // manufacturer data, company id first (LE)
};

// Match by MAC when given, else by name prefix
bool probeAdvert(const std::string& mac, const std::string& namePrefix, uint32_t timeoutMs,
                 AdvertInfo& out);
