// leds.h — NeoPixel heart-ring driver for CloseLove
//
// Controls 8 WS2812B LEDs arranged in a heart shape.
// Zone 1 (far) = single dim blue pulse.
// Zone 5 (close) = all eight LEDs, solid red at 60% brightness.
//
// Usage:
//   setup()  → ledsInit()
//   loop()   → ledsUpdate()   (call every iteration — no delay() inside)
//              setHeartZone(zone) whenever the zone changes
//
// Author: CloseLove project
// Date:   2026-04-18

#pragma once
#include <stdint.h>

// ── Hardware ───────────────────────────────────────────────────────────────

// Feather digital pin connected to LED0 DIN (first LED's data-in)
#define NEO_PIN 6

// Total number of WS2812B LEDs in the chain
#define NEO_COUNT 8

// Overall brightness scale (0–255). Reduce to dim all LEDs without
// changing zone proportions. 200 ≈ 78% of full brightness.
#define LED_BRIGHTNESS 200

// Zone cross-fade duration in milliseconds.
#define TRANSITION_MS 1500

// Heartbeat pulse period in milliseconds (1000 ms = 60 bpm).
#define HEARTBEAT_MS 1000

// ── Public API ─────────────────────────────────────────────────────────────

// Initialise the NeoPixel strip and blank all LEDs.
// Must be called once from setup() before ledsUpdate().
void ledsInit();

// Set the active proximity zone (1 – 5).
//   1 = farthest away (dim, slow blue pulse)
//   5 = closest (solid red, 60% brightness)
// The display does not change until the next ledsUpdate() call.
void setHeartZone(uint8_t zone);

// Advance the LED animation and push the new frame to the strip.
// Call every loop() iteration. Never blocks — uses millis() internally.
void ledsUpdate();

// Enable or disable the boot searching animation.
// While true, ledsUpdate() chases a single red pixel around the heart
// instead of showing a zone. Call ledsSetSearching(false) once the RSSI
// buffer is fully seeded (RSSI_SAMPLES packets received).
void ledsSetSearching(bool searching);
