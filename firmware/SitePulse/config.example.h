#pragma once
// Copy to config.h. Never commit the filled-in file.
const char* WIFI_SSID = "YOUR_2_4_GHZ_WIFI";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* ENDPOINT = "https://script.google.com/macros/s/YOUR_DEPLOYMENT_ID/exec";
const char* WRITE_TOKEN = "YOUR_WRITE_TOKEN";
const char* DEVICE = "esp32_home";
// Resolve this hostname once per session, or use an IPv4 address for a fixed target.
const char* TARGET = "chatgpt.com";
const char* HTTPS_URL = "https://chatgpt.com/";
const int SAMPLES = 60;
const int UPLOAD_BATCH_SIZE = 5;
const uint32_t PAIR_INTERVAL_MS = 1000;
const uint32_t SESSION_INTERVAL_SECONDS = 1800; // 300 = every five minutes
const int CAPACITY = 360; // RAM only; new rows are dropped when full

// Optional Quantic portal adapter; leave disabled for ordinary Wi-Fi.
const bool USE_QUANTIC_PORTAL = false;
// Enable only if the portal's statement is true for your own account.
const bool CONFIRM_MY_DEVICES = false;
const char* QUANTIC_EMAIL = "TON_EMAIL_QUANTIC";
const char* QUANTIC_PASSWORD = "TON_MOT_DE_PASSE_QUANTIC";
const char* QUANTIC_URL = "https://www.quantic-telecom.net/connexion-reseau";
