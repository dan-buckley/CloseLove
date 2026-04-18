#include <Arduino.h>
#include <SPI.h>
#include <RH_RF69.h>

// ── Feather 32u4 RFM69HCW pin assignments ──────────────────────────────────
#define RFM69_CS   8
#define RFM69_INT  7
#define RFM69_RST  4
#define RF69_FREQ  915.0   // MHz — change to 433.0 if you have the 433 MHz variant

// ── LED ────────────────────────────────────────────────────────────────────
// Built-in red LED on pin 13; swap for an external LED + PWM pin if desired
#define LED_PIN    13

// ── Radio ──────────────────────────────────────────────────────────────────
RH_RF69 rf69(RFM69_CS, RFM69_INT);

// ── Node identity ──────────────────────────────────────────────────────────
// Flash one device with NODE_ID=1, the other with NODE_ID=2
#define NODE_ID    1
#define PEER_ID    2
#define NETWORK_ID 100

// ── RSSI smoothing ─────────────────────────────────────────────────────────
// Rolling average over N samples to prevent LED flickering
#define RSSI_SAMPLES 8
int rssiBuffer[RSSI_SAMPLES];
int rssiIndex = 0;
int rssiSum   = 0;

// RSSI thresholds (dBm) — tune these empirically in your environment
// Closer = higher (less negative) RSSI value
#define RSSI_FAR   -90   // at or below this → LED off
#define RSSI_NEAR  -50   // at or above this → LED full brightness

// ── Timing ─────────────────────────────────────────────────────────────────
#define TX_INTERVAL_MS 200   // how often to transmit a ping (ms)
unsigned long lastTx = 0;

// ── Helpers ────────────────────────────────────────────────────────────────

void initRssiBuffer() {
  for (int i = 0; i < RSSI_SAMPLES; i++) rssiBuffer[i] = RSSI_FAR;
  rssiSum = RSSI_FAR * RSSI_SAMPLES;
}

int smoothRssi(int newRssi) {
  rssiSum -= rssiBuffer[rssiIndex];
  rssiBuffer[rssiIndex] = newRssi;
  rssiSum += newRssi;
  rssiIndex = (rssiIndex + 1) % RSSI_SAMPLES;
  return rssiSum / RSSI_SAMPLES;
}

// Map a smoothed RSSI value to a PWM brightness (0–255)
int rssitoBrightness(int rssi) {
  if (rssi <= RSSI_FAR)  return 0;
  if (rssi >= RSSI_NEAR) return 255;
  return map(rssi, RSSI_FAR, RSSI_NEAR, 0, 255);
}

// ── Setup ──────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  // Give the serial monitor time to connect during development
  // Remove or shorten this delay once the sketch is stable
  delay(500);

  pinMode(LED_PIN, OUTPUT);

  // Hard reset the radio
  pinMode(RFM69_RST, OUTPUT);
  digitalWrite(RFM69_RST, HIGH); delay(10);
  digitalWrite(RFM69_RST, LOW);  delay(10);

  if (!rf69.init()) {
    Serial.println(F("RFM69 init failed"));
    while (true);
  }
  if (!rf69.setFrequency(RF69_FREQ)) {
    Serial.println(F("setFrequency failed"));
    while (true);
  }

  // TX power: 14–20 dBm. Lower = shorter range, better RSSI gradient.
  rf69.setTxPower(14, true);

  // Simple encryption key — must be identical on both devices
  uint8_t key[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
  rf69.setEncryptionKey(key);

  rf69.setThisAddress(NODE_ID);
  rf69.setHeaderFrom(NODE_ID);
  rf69.setHeaderTo(PEER_ID);
  rf69.setHeaderId(NETWORK_ID);

  initRssiBuffer();

  Serial.print(F("CloseLove node "));
  Serial.print(NODE_ID);
  Serial.println(F(" ready"));
}

// ── Loop ───────────────────────────────────────────────────────────────────

void loop() {
  unsigned long now = millis();

  // ── Transmit a short ping every TX_INTERVAL_MS ──────────────────────────
  if (now - lastTx >= TX_INTERVAL_MS) {
    lastTx = now;
    uint8_t ping[] = "ping";
    rf69.send(ping, sizeof(ping));
    rf69.waitPacketSent();
  }

  // ── Check for an incoming packet ────────────────────────────────────────
  if (rf69.available()) {
    uint8_t buf[RH_RF69_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);
    if (rf69.recv(buf, &len)) {
      int rssi    = rf69.lastRssi();
      int smooth  = smoothRssi(rssi);
      int bright  = rssitoBrightness(smooth);

      analogWrite(LED_PIN, bright);

      Serial.print(F("RSSI: ")); Serial.print(rssi);
      Serial.print(F("  smooth: ")); Serial.print(smooth);
      Serial.print(F("  brightness: ")); Serial.println(bright);
    }
  }
}
