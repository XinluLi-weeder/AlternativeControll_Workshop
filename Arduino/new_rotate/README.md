# Existing five-encoder + BNO055 OSC firmware

Open `new_rotate.ino` in Arduino IDE for the FireBeetle 2 ESP32-S3.
Copy `wifi_secrets.h.example` to `wifi_secrets.h` and enter your hotspot settings locally.
Set `laptopIP` to the Windows computer's Wi-Fi IPv4 address; keep port 8000.
The local secrets header is intentionally ignored by Git.

In Room Shift v0.3.0 select Settings → Physical controller. The game receives
OSC directly; do not run the old Python bridge alongside it. Existing uploaded
firmware does not need reflashing if its Wi-Fi settings and destination IP are correct.

Messages: `/rotary` with five float angles, `/imu` with four floats (heading,
roll, pitch, linear acceleration Z). Each physical encoder revolution must be
calibrated against `COUNTS_PER_REV`; the current estimate is 80 counts.
