#pragma once

// Copy this file to secrets.h and fill in the values for your own device.
// Keep secrets.h private. It is ignored by Git.
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// Local development example: "http://192.168.1.20:3000/api/readings"
// Deployed example: "https://your-app.example.com/api/readings"
#define API_URL "https://your-app.example.com/api/readings"
#define DEVICE_API_KEY "paste-the-same-value-as-the-server-DEVICE_API_KEY"
#define PATIENT_ID 1

// For HTTPS only: paste the root CA certificate for your hosting domain as a
// PEM string. Leave empty for local HTTP. Do not use an insecure TLS bypass.
#define SERVER_ROOT_CA ""
