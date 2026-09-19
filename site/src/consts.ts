export const SITE_TITLE = "ShutterBridge";
export const AUTHOR = "YarosFPV";

// English fallback description. Per-locale copy lives in src/i18n/locales/*.json
// (site.description) and is what the pages actually render.
export const SITE_DESCRIPTION =
  "ESP32-S3 firmware linking Betaflight to DJI Osmo, DJI Action and GoPro cameras - live camera status on your FPV OSD, RC switches mapped to record, photo and mode.";

export const GITHUB_URL = "https://github.com/YLabs-FPV/ShutterBridge";
export const YOUTUBE_URL = "https://youtube.com/@yarosfpv";

export const BOARD_NAME = "Waveshare ESP32-S3-Zero";

export type Support = "yes" | "no" | "partial";

// Brand / structural data for each camera card. The human-readable notes are
// translated per locale (compat.cameraNotes), aligned to this order.
export interface CameraMeta {
  name: string;
  tag: string;
  tested: boolean;
}

export const CAMERAS: CameraMeta[] = [
  { name: "DJI Osmo (Nano)", tag: "DUML", tested: true },
  { name: "DJI Action / Osmo 360", tag: "DJI R-SDK", tested: true },
  { name: "GoPro (HERO9+)", tag: "Open GoPro", tested: false },
];

// Support matrix, aligned to the translated feature labels (compat.featureLabels).
export interface FeatureSupport {
  osmoNano: Support;
  action: Support;
  gopro: Support;
}

export const FEATURE_SUPPORT: FeatureSupport[] = [
  { osmoNano: "yes", action: "yes", gopro: "yes" },
  { osmoNano: "yes", action: "yes", gopro: "yes" },
  { osmoNano: "yes", action: "yes", gopro: "no" },
  { osmoNano: "no", action: "yes", gopro: "yes" },
  { osmoNano: "no", action: "yes", gopro: "yes" },
  { osmoNano: "yes", action: "yes", gopro: "yes" },
  { osmoNano: "partial", action: "yes", gopro: "yes" },
  { osmoNano: "no", action: "no", gopro: "yes" },
  { osmoNano: "no", action: "no", gopro: "yes" },
];
