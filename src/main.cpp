#include <Arduino.h>
#include <SPI.h>
#include <RH_RF69.h>
#include "leds.h"
#include "config.h"

// ── Feather 32u4 RFM69HCW pin assignments ──────────────────────────────────
#define RFM69_CS 8
#define RFM69_INT 7
#define RFM69_RST 4
#define RF69_FREQ 915.0 // MHz — change to 433.0 if you have the 433 MHz variant

// ── Built-in LED (pin 13) ─────────────────────────────────────────────────
// Used as a simple RX indicator during radio testing (Module 3).
// The NeoPixel heart is driven via leds.h / leds.cpp.
#define LED_PIN 13

// ── Radio ──────────────────────────────────────────────────────────────────
RH_RF69 rf69(RFM69_CS, RFM69_INT);

// ── Node identity ──────────────────────────────────────────────────────────
// Flash one device with NODE_ID=1, the other with NODE_ID=2
#define NODE_ID 1
#define PEER_ID 2
#define NETWORK_ID 100

// ── RSSI smoothing ─────────────────────────────────────────────────────────
// Rolling average over N samples to prevent LED flickering
#define RSSI_SAMPLES 8
int rssiBuffer[RSSI_SAMPLES];
int rssiIndex = 0;
int rssiSum = 0;

// RSSI thresholds (dBm) — tune these empirically in your environment
// Closer = higher (less negative) RSSI value
#define RSSI_FAR -90  // at or below this → LED off
#define RSSI_NEAR -50 // at or above this → LED full brightness

// ── Timing ─────────────────────────────────────────────────────────────────
#define TX_INTERVAL_MS 200 // how often to transmit a ping (ms)
unsigned long lastTx = 0;

// ── Module 3 — radio timeout ──────────────────────────────────────────────
// If no packet is received within this window, the link is considered lost
// and the display falls back to Zone 1 (dim blue pulse).
#define NO_SIGNAL_TIMEOUT_MS 2000 // ms before declaring signal lost
unsigned long lastRx = 0;         // timestamp of the most recent received packet
bool hasSignal = false;           // true once the first packet has arrived
uint8_t currentZone = 1;          // zone currently displayed (1–5); reset on signal loss
uint8_t bootSamples = 0;          // counts RX packets until the RSSI buffer is fully seeded

// ── Helpers ────────────────────────────────────────────────────────────────

void initRssiBuffer()
{
    for (int i = 0; i < RSSI_SAMPLES; i++)
        rssiBuffer[i] = RSSI_FAR;
    rssiSum = RSSI_FAR * RSSI_SAMPLES;
}

int smoothRssi(int newRssi)
{
    rssiSum -= rssiBuffer[rssiIndex];
    rssiBuffer[rssiIndex] = newRssi;
    rssiSum += newRssi;
    rssiIndex = (rssiIndex + 1) % RSSI_SAMPLES;
    return rssiSum / RSSI_SAMPLES;
}

// Map a smoothed RSSI value to a PWM brightness (0–255)
int rssitoBrightness(int rssi)
{
    if (rssi <= RSSI_FAR)
        return 0;
    if (rssi >= RSSI_NEAR)
        return 255;
    return map(rssi, RSSI_FAR, RSSI_NEAR, 0, 255);
}

/**
 * Map smoothed RSSI to a display zone (1–5) with hysteresis.
 *
 * Upgrading (boards move closer, signal gets stronger) is immediate.
 * Downgrading (boards move apart) only happens once smooth_rssi has fallen
 * RSSI_HYSTERESIS dBm below the current zone's entry threshold, preventing
 * LED flicker at zone boundaries.
 *
 * Uses the global `currentZone`; caller must reset it to 1 when signal is lost.
 */
uint8_t rssiToZone(int rssi)
{
    // Entry threshold for each zone (index = zone number; indices 0 and 1 unused)
    static const int thresholds[6] = {0, 0, RSSI_Z2, RSSI_Z3, RSSI_Z4, RSSI_Z5};

    // Compute raw zone directly from config thresholds
    uint8_t raw;
    if (rssi >= RSSI_Z5)
        raw = 5;
    else if (rssi >= RSSI_Z4)
        raw = 4;
    else if (rssi >= RSSI_Z3)
        raw = 3;
    else if (rssi >= RSSI_Z2)
        raw = 2;
    else
        raw = 1;

    if (raw > currentZone)
    {
        // Getting closer — upgrade immediately
        currentZone = raw;
    }
    else if (raw < currentZone)
    {
        // Moving apart — only downgrade once past the hysteresis band
        if (rssi < thresholds[currentZone] - RSSI_HYSTERESIS)
            currentZone = raw;
    }
    return currentZone;
}

// ── Setup ──────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    // Wait up to 3 s for USB serial to connect (safe on battery — times out)
    {
        unsigned long t = millis();
        while (!Serial && millis() - t < 3000)
            delay(10);
    }
    delay(100);

    pinMode(LED_PIN, OUTPUT);

    // Hard reset the radio
    pinMode(RFM69_RST, OUTPUT);
    digitalWrite(RFM69_RST, HIGH);
    delay(10);
    digitalWrite(RFM69_RST, LOW);
    delay(10);

    if (!rf69.init())
    {
        Serial.println(F("RFM69 init failed"));
        while (true)
            ;
    }
    if (!rf69.setFrequency(RF69_FREQ))
    {
        Serial.println(F("setFrequency failed"));
        while (true)
            ;
    }

    // TX power: 14–20 dBm. Lower = shorter range, better RSSI gradient.
    rf69.setTxPower(14, true);

    // Simple encryption key — must be identical on both devices
    uint8_t key[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                     0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    rf69.setEncryptionKey(key);

    rf69.setThisAddress(NODE_ID);
    rf69.setHeaderFrom(NODE_ID);
    rf69.setHeaderTo(PEER_ID);
    rf69.setHeaderId(NETWORK_ID);

    initRssiBuffer();

    // Initialise the NeoPixel strip — blanks all LEDs, starts at Zone 1
    ledsInit();
    setHeartZone(1);

    Serial.print(F("CloseLove node "));
    Serial.print(NODE_ID);
    Serial.println(F(" ready — waiting for peer..."));
    Serial.println(F("  tx_ms | raw_rssi | smooth_rssi | zone | note"));
    Serial.println(F("--------|----------|-------------|------|---------------------"));
}

// ── Loop ───────────────────────────────────────────────────────────────────

void loop()
{
    unsigned long now = millis();

    // ── Transmit a short ping every TX_INTERVAL_MS ──────────────────────────
    if (now - lastTx >= TX_INTERVAL_MS)
    {
        lastTx = now;
        uint8_t ping[] = "ping";
        rf69.send(ping, sizeof(ping));
        rf69.waitPacketSent();
    }

    // ── Check for an incoming packet ────────────────────────────────────────
    if (rf69.available())
    {
        uint8_t buf[RH_RF69_MAX_MESSAGE_LEN];
        uint8_t len = sizeof(buf);
        if (rf69.recv(buf, &len))
        {
            lastRx = now;
            hasSignal = true;

            int rawRssi = rf69.lastRssi();
            int smoothRssi_ = smoothRssi(rawRssi);

            // Exit searching animation once the buffer is fully seeded
            if (bootSamples < RSSI_SAMPLES)
            {
                if (++bootSamples == RSSI_SAMPLES)
                    ledsSetSearching(false);
            }

            // Map to zone and drive NeoPixel heart
            setHeartZone(rssiToZone(smoothRssi_));

            // Built-in LED: full on when signal is good, dim when weak
            int bright = rssitoBrightness(smoothRssi_);
            analogWrite(LED_PIN, bright);

            // Print a fixed-width row for easy copy-paste into the calibration log
            // Format: tx_ms | raw_rssi | smooth_rssi | zone | note
            Serial.print(now);
            Serial.print(F(" | "));
            Serial.print(rawRssi);
            Serial.print(F("       | "));
            Serial.print(smoothRssi_);
            Serial.print(F("          | "));
            Serial.print(currentZone);
            Serial.println(F("    | RX OK"));
        }
    }

    // ── No-signal timeout: fall back to Zone 1 and log the drop ────────────
    if (hasSignal && (now - lastRx > NO_SIGNAL_TIMEOUT_MS))
    {
        hasSignal = false;
        currentZone = 1; // reset so next signal starts fresh from zone 1
        setHeartZone(1);
        initRssiBuffer(); // reset smoothing so stale values don't linger
        Serial.println(F("** signal lost — back to zone 1 **"));
    }

    // ── Advance NeoPixel animation ───────────────────────────────────────────
    // Must be called every loop iteration to keep the pulse smooth.
    ledsUpdate();
}
