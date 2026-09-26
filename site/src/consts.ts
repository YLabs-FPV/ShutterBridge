export const SITE_TITLE = "ShutterBridge";
export const AUTHOR = "YarosFPV";

// English fallback description. Per-locale copy lives in src/i18n/locales/*.json
// (site.description) and is what the pages actually render.
export const SITE_DESCRIPTION =
  "ESP32-S3 firmware linking Betaflight to DJI Osmo, DJI Action and GoPro cameras - live camera status on your FPV OSD, RC switches mapped to record, photo and mode.";

export const GITHUB_URL = "https://github.com/YLabs-FPV/ShutterBridge";
export const YOUTUBE_URL = "https://youtube.com/@yarosfpv";

export const BOARD_NAME = "Waveshare ESP32-S3-Zero";

export type Support = "yes" | "no" | "partial" | "untested";

// Brand / structural data for each camera card. The human-readable notes are
// translated per locale (compat.cameraNotes), aligned to this order.
export interface CameraMeta {
  name: string;
  tag: string;
  tested: boolean;
}

export const CAMERAS: CameraMeta[] = [
  { name: "DJI Osmo (Nano)", tag: "DUML", tested: true },
  { name: "DJI Action (3+) / Osmo 360", tag: "DJI R-SDK", tested: true },
  { name: "GoPro (HERO9+)", tag: "Open GoPro", tested: true },
];

// Support matrix, aligned to the translated feature labels (compat.featureLabels).
export interface FeatureSupport {
  osmoNano: Support;
  action: Support;
  gopro: Support;
}

export const FEATURE_SUPPORT: FeatureSupport[] = [
  { osmoNano: "yes", action: "yes", gopro: "yes" }, // Connect + auto-reconnect
  { osmoNano: "yes", action: "yes", gopro: "yes" }, // OSD telemetry
  { osmoNano: "yes", action: "yes", gopro: "no" }, // Mode readout
  { osmoNano: "no", action: "yes", gopro: "untested" }, // Photo / video mode switch
  { osmoNano: "no", action: "yes", gopro: "untested" }, // Resolution / FPS readout
  { osmoNano: "yes", action: "yes", gopro: "yes" }, // Start / stop recording
  { osmoNano: "partial", action: "yes", gopro: "untested" }, // Take photo
  { osmoNano: "no", action: "no", gopro: "untested" }, // Preset switching
  { osmoNano: "no", action: "no", gopro: "untested" }, // Clock sync from FC GPS time
];

export type TestResult = "worksLimited" | "worksFull" | "worksPartial";
export type TestedBy = "maintainer" | "community";

export interface TestedCamera {
  camera: string;
  backend: string;
  result: TestResult;
  by: TestedBy;
}

export const TESTED_ON: TestedCamera[] = [
  {
    camera: "DJI Osmo Nano",
    backend: "Osmo (Nano)",
    result: "worksLimited",
    by: "maintainer",
  },
  {
    camera: "DJI Osmo Action 4",
    backend: "Action / 360",
    result: "worksFull",
    by: "maintainer",
  },
  {
    camera: "GoPro (HERO 12 Black)",
    backend: "GoPro",
    result: "worksPartial",
    by: "community",
  },
];
