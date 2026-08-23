// Cross-module radio-coexistence hint. Wi-Fi and BLE share one 2.4 GHz antenna
// (time-division multiplexed), so a BLE connect/scan steals airtime from the Wi-Fi AP
// and makes the Web UI crawl. WebConfig sets this true while the config AP is serving;
// the BLE camera backends read it to throttle radio-heavy connect attempts so the UI
// stays responsive
#pragma once

extern volatile bool g_apActive;
extern volatile bool g_apHasClient;
