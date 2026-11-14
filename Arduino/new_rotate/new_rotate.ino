/*
 * Alt.Ctrl — BNO055 + 5 Rotary Encoders -> Unreal over OSC
 * Board: FireBeetle 2 ESP32-S3
 *
 * Data path (see Documentation/LegacyOSCControllerSetup.md):
 *   ESP32 Wi-Fi OSC -> laptop Tools/osc_bridge.py -> localhost UDP 19872 -> Unreal
 *
 * OSC messages (address and argument order must not change):
 *   /imu    -> euler.x, euler.y, euler.z, linear accel Z   (4 floats)
 *   /rotary -> angle1..angle5, 0-360 degrees               (5 floats)
 *
 * ---------------- WIRING ----------------
 * BNO055:
 *   Vin -> 3V3
 *   GND -> GND
 *   SDA -> IO1
 *   SCL -> IO2
 *
 * Encoders (VCC, GND, A, B, C):
 *   VCC -> 3V3
 *   GND -> GND
 *   C   -> unconnected
 *
 * Encoder 1: A -> IO4,  B -> IO5
 * Encoder 2: A -> IO6,  B -> IO7
 * Encoder 3: A -> IO8,  B -> IO9
 * Encoder 4: A -> IO10, B -> IO11
 * Encoder 5: A -> IO14, B -> IO38
 *
 * IMPORTANT:
 *   Confirm that encoder outputs are 3.3V-safe.
 *
 * Libraries:
 *   - Adafruit BNO055
 *   - Adafruit Unified Sensor
 *   - OSC (by CNMAT)
 *
 * Startup order:
 *   1. Unreal: open /Game/Levels/Hallyway and press Play.
 *   2. PC: python Tools/osc_bridge.py
 *   3. Power the controller. The serial monitor should show Wi-Fi connected
 *      and "target <laptop IP>:8000". osc_bridge.py prints this PC's IPs.
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <OSCMessage.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <math.h>

// ---------- Wi-Fi ----------
#include "wifi_secrets.h"
const char* ssid = ROOMSHIFT_WIFI_SSID;
const char* password = ROOMSHIFT_WIFI_PASSWORD;

// Laptop IPv4 address on the SAME hotspot. Run `ipconfig` or read the
// address list printed by Tools/osc_bridge.py. iPhone hotspots use
// 172.20.10.x; the laptop is usually 172.20.10.2 or .3.
IPAddress laptopIP(172, 20, 10, 3);
const int laptopPort = 8000;

// Local port only used to create the ESP32 UDP socket. Any free port works.
const int localUdpPort = 8001;

WiFiUDP Udp;

// ---------- BNO055 ----------
Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x28, &Wire);
bool bnoReady = false;

// ---------- Encoder configuration ----------
// Each encoder uses two GPIO pins: A and B.
const int encoderPins[5][2] = {
  {4, 5},
  {6, 7},
  {8, 9},
  {10, 11},
  {14, 38}
};

// 20 detents/revolution with x4 quadrature decoding = 80 counts.
// This is an initial estimate. Calibrate by turning one full revolution.
const float COUNTS_PER_REV = 80.0f;

// Set to -1 if an encoder rotates in the opposite direction.
const int encoderDirection[5] = {
  1, 1, 1, 1, 1
};

struct Encoder {
  int pinA;
  int pinB;
  volatile int32_t count;
  volatile uint8_t previousState;
};

Encoder encoders[5];

// Shared critical-section lock for safe access to encoder counts.
portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;

// Quadrature transition lookup table.
// Index = (previousState << 2) | currentState.
// State = (A << 1) | B.
const int8_t quadratureTable[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

// Read the current two-bit A/B state.
uint8_t readEncoderState(const Encoder* encoder) {
  uint8_t a = digitalRead(encoder->pinA);
  uint8_t b = digitalRead(encoder->pinB);
  return (a << 1) | b;
}

// Software quadrature interrupt handler.
void IRAM_ATTR encoderISR(void* arg) {
  Encoder* encoder = static_cast<Encoder*>(arg);

  uint8_t a = (uint8_t)digitalRead(encoder->pinA);
  uint8_t b = (uint8_t)digitalRead(encoder->pinB);
  uint8_t currentState = (a << 1) | b;

  portENTER_CRITICAL_ISR(&encoderMux);

  uint8_t index =
      (encoder->previousState << 2) | currentState;

  encoder->count += quadratureTable[index];
  encoder->previousState = currentState;

  portEXIT_CRITICAL_ISR(&encoderMux);
}

// Get a safe copy of the encoder count.
int32_t getEncoderCount(int index) {
  int32_t value;

  portENTER_CRITICAL(&encoderMux);
  value = encoders[index].count;
  portEXIT_CRITICAL(&encoderMux);

  return value;
}

// Convert count to a wrapped 0-360 degree angle.
float countToAngle(int32_t count, int index) {
  float angle =
      ((float)count * encoderDirection[index] / COUNTS_PER_REV)
      * 360.0f;

  angle = fmodf(angle, 360.0f);

  if (angle < 0) {
    angle += 360.0f;
  }

  return angle;
}

// Set all encoder counts to zero.
void zeroAllEncoders() {
  portENTER_CRITICAL(&encoderMux);

  for (int i = 0; i < 5; i++) {
    encoders[i].count = 0;
  }

  portEXIT_CRITICAL(&encoderMux);

  Serial.println("All encoder angles set to 0 degrees.");
}

// Initialize all five encoders.
void setupEncoders() {
  for (int i = 0; i < 5; i++) {
    encoders[i].pinA = encoderPins[i][0];
    encoders[i].pinB = encoderPins[i][1];
    encoders[i].count = 0;
    encoders[i].previousState = 0;

    pinMode(encoders[i].pinA, INPUT_PULLUP);
    pinMode(encoders[i].pinB, INPUT_PULLUP);

    // Read initial state before enabling interrupts.
    encoders[i].previousState = readEncoderState(&encoders[i]);

    attachInterruptArg(
      encoders[i].pinA,
      encoderISR,
      &encoders[i],
      CHANGE
    );

    attachInterruptArg(
      encoders[i].pinB,
      encoderISR,
      &encoders[i],
      CHANGE
    );

    Serial.print("Encoder ");
    Serial.print(i + 1);
    Serial.print(" initialized: IO");
    Serial.print(encoders[i].pinA);
    Serial.print(" / IO");
    Serial.println(encoders[i].pinB);
  }
}

// ---------- Serial commands ----------
void readSerialCommands() {
  if (!Serial.available()) {
    return;
  }

  String command = Serial.readStringUntil('\n');
  command.trim();
  command.toUpperCase();

  if (command == "ZERO") {
    zeroAllEncoders();
  } else if (command == "STATUS") {
    Serial.println("Encoder counts:");

    for (int i = 0; i < 5; i++) {
      Serial.print("Encoder ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(getEncoderCount(i));
      Serial.print(" counts, ");
      Serial.print(countToAngle(getEncoderCount(i), i));
      Serial.println(" degrees");
    }
  } else if (command == "HELP") {
    Serial.println("Commands:");
    Serial.println("ZERO   - Set all encoder angles to 0");
    Serial.println("STATUS - Print encoder counts and angles");
    Serial.println("HELP   - Show commands");
  }
}

// ---------- Network helpers ----------
void printNetworkInfo() {
  Serial.print("Wi-Fi: SSID ");
  Serial.print(WiFi.SSID());
  Serial.print("  IP ");
  Serial.print(WiFi.localIP());
  Serial.print("  RSSI ");
  Serial.print(WiFi.RSSI());
  Serial.print(" dBm  -> target ");
  Serial.print(laptopIP);
  Serial.print(":");
  Serial.println(laptopPort);
}

// Blocking first attempt; the loop retries automatically afterwards.
bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("SUCCESS! Already connected to hotspot.");
    printNetworkInfo();
    return true;
  }

  Serial.print("Connecting to hotspot: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("SUCCESS! Connected to hotspot.");
    printNetworkInfo();
    return true;
  }

  Serial.println("WARNING: Wi-Fi connection failed.");
  Serial.println("Check: hotspot is on, SSID/password are correct, and the");
  Serial.println("hotspot is 2.4 GHz (ESP32 cannot join 5 GHz-only networks).");
  Serial.println("The controller will keep retrying every 5 seconds.");
  return false;
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(10);
  delay(1000);

  Serial.println();
  Serial.println("Starting Alt.Ctrl...");

  // BNO055: explicitly use SDA = IO1, SCL = IO2.
  // IMUPLUS = accelerometer + gyroscope only. It avoids the magnetometer
  // drift/jumps that NDOF shows when the box is near metal furniture.
  Wire.begin(1, 2);

  if (bno.begin(OPERATION_MODE_IMUPLUS)) {
    bnoReady = true;
    bno.setExtCrystalUse(true);
    Serial.println("BNO055 OK (IMUPLUS fusion, external crystal).");
  } else {
    bnoReady = false;
    Serial.println("WARNING: BNO055 not found. Check wiring and power.");
    Serial.println("The view will not move until the sensor is detected.");
  }

  // Initialize all five encoders using GPIO interrupts.
  setupEncoders();

  Serial.println("All five encoders initialized.");

  // Connect to phone hotspot.
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    Udp.begin(localUdpPort);
  }

  Serial.println("Running.");
  Serial.println("Type HELP for available commands.");
}

// ---------- Main loop ----------
void loop() {
  readSerialCommands();

  // ----- Keep Wi-Fi alive and (re)create the UDP socket -----
  static unsigned long lastWifiCheck = 0;
  static bool wasConnected = false;

  if (millis() - lastWifiCheck >= 1000) {
    lastWifiCheck = millis();
    const bool connected = (WiFi.status() == WL_CONNECTED);

    if (connected && !wasConnected) {
      Serial.println("Wi-Fi connected.");
      printNetworkInfo();
      Udp.begin(localUdpPort);
    } else if (!connected) {
      // WiFi.reconnect() reuses the stored credentials and re-associates.
      if (millis() % 5000 < 1000) {
        WiFi.reconnect();
      }
    }
    wasConnected = connected;
  }

  // ----- Read BNO055 -----
  // Adafruit Euler convention: x = heading (yaw), y = roll, z = pitch.
  float eulerX = 0;
  float eulerY = 0;
  float eulerZ = 0;
  float accZ = 0;

  if (bnoReady) {
    sensors_event_t orientationData;

    bno.getEvent(
      &orientationData,
      Adafruit_BNO055::VECTOR_EULER
    );

    eulerX = orientationData.orientation.x;
    eulerY = orientationData.orientation.y;
    eulerZ = orientationData.orientation.z;

    imu::Vector<3> accel =
        bno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);

    accZ = accel.z();
  }

  // ----- Read all five encoder angles -----
  float angles[5];

  for (int i = 0; i < 5; i++) {
    angles[i] = countToAngle(getEncoderCount(i), i);
  }

  // ----- Send OSC -----
  static uint32_t sentPackets = 0;

  if (WiFi.status() == WL_CONNECTED) {
    // /imu: euler.x, euler.y, euler.z, linear accel Z (order is fixed;
    // Tools/osc_bridge.py maps index 2 to camera pitch).
    OSCMessage imuMsg("/imu");

    imuMsg.add(eulerX);
    imuMsg.add(eulerY);
    imuMsg.add(eulerZ);
    imuMsg.add(accZ);

    Udp.beginPacket(laptopIP, laptopPort);
    imuMsg.send(Udp);
    Udp.endPacket();
    imuMsg.empty();

    // /rotary: 5 floats, physical angles 0-360.
    OSCMessage rotMsg("/rotary");

    for (int i = 0; i < 5; i++) {
      rotMsg.add(angles[i]);
    }

    Udp.beginPacket(laptopIP, laptopPort);
    rotMsg.send(Udp);
    Udp.endPacket();
    rotMsg.empty();

    sentPackets++;
  }

  // ----- Serial Monitor (kept at 2 Hz so it never slows the control loop) -----
  static unsigned long lastPrint = 0;

  if (millis() - lastPrint >= 500) {
    lastPrint = millis();

    Serial.print("WiFi ");
    Serial.print(WiFi.status() == WL_CONNECTED ? "OK" : "DOWN");
    Serial.print(" | sent ");
    Serial.print(sentPackets);
    Serial.print(" | IMU x/y/z ");
    Serial.print(eulerX, 1);
    Serial.print("/");
    Serial.print(eulerY, 1);
    Serial.print("/");
    Serial.print(eulerZ, 1);
    Serial.print(" az=");
    Serial.print(accZ, 2);
    Serial.print(" | ROTARY ");

    for (int i = 0; i < 5; i++) {
      Serial.print(angles[i], 1);
      if (i < 4) {
        Serial.print(", ");
      }
    }

    Serial.println();
  }

  delay(20);
}
