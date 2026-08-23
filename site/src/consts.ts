export const SITE_TITLE = "ShutterBridge";
export const AUTHOR = "YarosFPV";
export const SITE_DESCRIPTION =
  "ESP32 firmware that bridges your flight controller and action camera - camera status on the OSD, RC switches mapped to camera actions.";

export const SLOGAN = "Your camera, on the OSD. Your switches, on the shutter.";

export const GITHUB_URL = "https://github.com/YLabs-FPV/ShutterBridge";
export const YOUTUBE_URL = "https://youtube.com/@yarosfpv";

export const BOARD_NAME = "Waveshare ESP32-S3-Zero";

export type Support = "yes" | "no" | "partial";

export interface Camera {
  name: string;
  tag: string;
  tested: boolean;
  notes: string;
}

export const CAMERAS: Camera[] = [
  {
    name: "DJI Osmo (Nano)",
    tag: "DUML",
    tested: true,
    notes:
      "Connect, OSD telemetry, mode readout, record and photo. Can't switch photo/video over BLE - the shutter takes a photo only when the camera is already in Photo mode.",
  },
  {
    name: "DJI Action / Osmo 360",
    tag: "DJI R-SDK",
    tested: true,
    notes:
      "Full control: mode switch, resolution/FPS readout, record and photo. R-SDK protocol with DUML for battery. Pairing via PIN. Tested on the Action 4.",
  },
  {
    name: "GoPro (HERO9+)",
    tag: "Open GoPro",
    tested: false,
    notes:
      "Preset switching, load-by-ID and clock sync from FC GPS time. Bonds on the camera. Not yet tested on real hardware.",
  },
];

export interface Feature {
  label: string;
  osmoNano: Support;
  action: Support;
  gopro: Support;
}

export const FEATURES: Feature[] = [
  {
    label: "Connect + auto-reconnect",
    osmoNano: "yes",
    action: "yes",
    gopro: "yes",
  },
  {
    label: "OSD telemetry (record, battery, SD, timers)",
    osmoNano: "yes",
    action: "yes",
    gopro: "yes",
  },
  {
    label: "Mode readout (video / photo)",
    osmoNano: "yes",
    action: "yes",
    gopro: "no",
  },
  {
    label: "Photo / video mode switch",
    osmoNano: "no",
    action: "yes",
    gopro: "yes",
  },
  {
    label: "Resolution / FPS readout",
    osmoNano: "no",
    action: "yes",
    gopro: "yes",
  },
  {
    label: "Start / stop recording",
    osmoNano: "yes",
    action: "yes",
    gopro: "yes",
  },
  { label: "Take photo", osmoNano: "partial", action: "yes", gopro: "yes" },
  { label: "Preset switching", osmoNano: "no", action: "no", gopro: "yes" },
  {
    label: "Clock sync from FC GPS time",
    osmoNano: "no",
    action: "no",
    gopro: "yes",
  },
];

export interface Component {
  name: string;
  detail: string;
}

export const REQUIRED: Component[] = [
  {
    name: BOARD_NAME,
    detail:
      "ESP32-S3FH4R2 - 4 MB flash, 2 MB PSRAM, native USB. The board ShutterBridge is built for.",
  },
  {
    name: `Betaflight 2025.12 or newer`,
    detail:
      "Flight controller running Betaflight 25.12+. Talks to the bridge over MSP; OSD elements draw the camera status.",
  },
  {
    name: "A supported action camera",
    detail:
      "A DJI Osmo / Action or a GoPro HERO9 or newer. See the compatibility list above.",
  },
];
