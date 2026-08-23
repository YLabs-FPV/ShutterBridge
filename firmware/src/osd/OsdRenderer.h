// Renders CameraStatus into the FC's 4 configurable "Custom Msg" OSD slots.
#pragma once

#include <cstddef>
#include <cstdint>

#include "camera/Camera.h"
#include "config/Settings.h"
#include "fc/FlightController.h"

class OsdRenderer {
   public:
    OsdRenderer(FlightController& fc, Settings& s, uint32_t updateMs = 250,
                uint32_t refreshMs = 1000)
        : fc_(fc), s_(s), updateMs_(updateMs), refreshMs_(refreshMs) {
    }

    // `online` = camera connected AND telemetry is fresh.
    void update(const CameraStatus& st, bool online);

   private:
    void formatField(OsdField f, const CameraStatus& st, bool online, char* out, size_t n);

    FlightController& fc_;
    Settings&         s_;
    uint32_t          updateMs_;
    uint32_t          refreshMs_;
    uint32_t          lastMs_        = 0;
    uint32_t          lastRefreshMs_ = 0;
    char              last_[4][32]   = {{0}};
};
