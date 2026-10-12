# ESP32 firmware

This folder contains the Arduino-framework firmware for an ESP32 DevKit/WROOM
connected to a MAX30102. It samples the optical sensor, estimates heart rate
and SpO2 with SparkFun's MAX3010x/Maxim algorithm, then posts valid readings to
the JavaScript server's `POST /api/readings` endpoint.

## Wiring

| MAX30102 | ESP32 DevKit/WROOM |
| --- | --- |
| VIN | 3V3 |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

Use the breakout board's documented supply range; this starter setup assumes a
3.3 V-compatible MAX30102 breakout. Keep the sensor steady and cover it with a
finger while taking a reading. These estimates are for a project prototype,
not a medical device.

## Configure and build

1. Install PlatformIO (VS Code extension or CLI) and the ESP32 DevKit board support.
2. Copy `esp32_health_monitor/include/secrets.example.h` to
   `esp32_health_monitor/include/secrets.h`.
3. Set Wi-Fi, API URL, device key, and patient ID in `secrets.h`. For local
   development, use the computer's LAN address and `http://...:3000/api/readings`.
   For deployment, use the public HTTPS URL and put the server's
   `DEVICE_API_KEY` in the device config. HTTPS also needs the host's root CA PEM
   in `SERVER_ROOT_CA`.
4. Build with `pio run` from `esp32_health_monitor/`.
5. To flash from PlatformIO use `pio run --target upload`; for browser flashing,
   use the install page described below.

The Node server must have a patient with the configured ID. On a deployed
server set `NODE_ENV=production` and a long random `DEVICE_API_KEY`. Use a
persistent data volume for `DATA_DIR` on the hosting platform.

## Browser USB installer

The JavaScript app serves `/flash.html`, using ESP Web Tools over Web Serial.
Before publishing firmware for installation:

1. Build the firmware with your private `secrets.h`.
2. Copy the PlatformIO bootloader, partition table, boot app, and application
   binaries to `public/firmware/` using the commands in `build-web-firmware.ps1`.
3. Open the deployed HTTPS site in Chrome, Edge, or Firefox, connect the ESP32
   with a data-capable USB cable, then select **Install firmware** and choose
   its serial port.

The installer downloads the binary, so any values compiled into that binary
(including Wi-Fi credentials and the API key) are recoverable by its recipient.
For a public deployment, issue separate keys per device or use a device
provisioning flow before sharing firmware. The starter uses one configured key
for a controlled prototype.

`secrets.h` and built binaries are excluded by the parent `.gitignore`.
