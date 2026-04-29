// leds.h — NeoPixel heart-ring driver for CloseLove
//
// Controls 8 WS2812B LEDs arranged in a heart shape.
// Zone 1 (close) = all eight LEDs, warm red at 60% brightness.
// Zone 5 (far) = single dim blue pulse.
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

// How long each LED-count step holds before the sequence advances (ms).
// Must be a multiple of HEARTBEAT_MS. 2000 = two full brightness pulses
// are visible at each LED count as the heart grows and shrinks back.
#define BEAT_STEP_MS 2000

// Slow colour-pulse period in milliseconds — independent of the heartbeat.
// Very subtle shimmer: only ±6% variation so it never fights the beat.
#define COLOUR_PULSE_MS 6000

// ── Public API ─────────────────────────────────────────────────────────────

// Initialise the NeoPixel strip and blank all LEDs.
// Must be called once from setup() before ledsUpdate().
void ledsInit();

// Set the active proximity zone (1 – 5).
//   1 = closest (warm red, 60% brightness)
//   5 = farthest away (dim, slow blue pulse)
// The display does not change until the next ledsUpdate() call.
void setHeartZone(uint8_t zone);

// Switch the boot-searching chaser on (true) or off (false).
// Called from main.cpp once the RSSI buffer is fully seeded.
void ledsSetSearching(bool searching);

// Advance the LED animation and push the new frame to the strip.
// Call every loop() iteration. Never blocks — uses millis() internally.
void ledsUpdate();

// Enable or disable the boot searching animation.
// While true, ledsUpdate() chases a single red pixel around the heart
// instead of showing a zone. Call ledsSetSearching(false) once the RSSI
// buffer is fully seeded (RSSI_SAMPLES packets received).
void ledsSetSearching(bool searching);
