// leds.cpp — NeoPixel heart-ring driver implementation
//
// Hardware: 8 × WS2812B LEDs, daisy-chained, data on Feather pin 6.
// LEDs 0–7 are numbered in chain order and laid out in a heart shape.
// See docs/plan.md §3c for the physical position of each index.
//
// Animation model
// ───────────────
// Zones 1–4 breathe: brightness follows a sine-wave envelope so the LEDs
// gently fade in and out with no visible stepping.
// Zone 5 is solid: all eight LEDs on at a fixed 60% brightness.
//
// Each call to ledsUpdate() computes the current brightness from millis(),
// builds the frame, and pushes it to the strip. No delay() is used anywhere.
//
// Author: CloseLove project
// Date:   2026-04-18

#include "leds.h"
#include <Adafruit_NeoPixel.h>
#include <math.h>

// ── Internal strip instance ────────────────────────────────────────────────

static Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

// ── Zone configuration tables (index 0 = zone 1) ──────────────────────────

// How many LEDs are lit for each zone.
static const uint8_t ZONE_LED_COUNT[5] = {1, 3, 5, 7, 8};

// Peak brightness (0–255) for each zone.
// Zones 1–4 pulse between 0 and this value.
// Zone 5 holds this value solid.
static const uint8_t ZONE_PEAK[5] = {
    20,  // Zone 1: ~8%  of full
    38,  // Zone 2: ~15%
    64,  // Zone 3: ~25%
    102, // Zone 4: ~40%
    153, // Zone 5: ~60%
};

// Base colour for each zone as a packed 0x00RRGGBB value.
// Brightness is applied by scaleColour() before writing to the strip.
static const uint32_t ZONE_COLOUR[5] = {
    0x000000FF, // Zone 1: blue
    0x004400FF, // Zone 2: blue-purple
    0x00AA00CC, // Zone 3: purple
    0x00FF4400, // Zone 4: orange-red
    0x00FF0000, // Zone 5: red
};

// ── LED fill order ─────────────────────────────────────────────────────────
//
// LEDs are switched on from the left side of the heart outward so that
// as the zone (and LED count) increases the lit area stays symmetric.
//
// Physical positions (chain index → heart location):
//   0 = left side     1 = upper-left bump   2 = upper-right bump
//   3 = right side    4 = lower-right       5 = bottom-right
//   6 = bottom-left   7 = lower-left
//
// Fill sequence: 0 → 1 → 7 → 2 → 6 → 3 → 5 → 4
static const uint8_t FILL_ORDER[NEO_COUNT] = {0, 1, 7, 2, 6, 3, 5, 4};

// ── Module state ───────────────────────────────────────────────────────────

// Active zone and previous zone for cross-fade blending (both clamped to 1–5).
static uint8_t currentZone = 1;
static uint8_t prevZone = 1;

// millis() timestamp when the last zone transition started.
static uint32_t transitionStart = 0;

// Boot searching mode: true until the RSSI buffer is fully seeded.
// ledsUpdate() shows a chaser instead of the zone animation while this is set.
static bool isSearching = true;

// ── Internal helpers ───────────────────────────────────────────────────────

// Scale a packed 0x00RRGGBB colour by brightness (0–255) then by LED_BRIGHTNESS.
static uint32_t scaleColour(uint32_t colour, uint8_t brightness)
{
    uint16_t scaled = (uint16_t)brightness * LED_BRIGHTNESS / 255;
    uint8_t r = (uint8_t)(((colour >> 16) & 0xFF) * scaled / 255);
    uint8_t g = (uint8_t)(((colour >> 8) & 0xFF) * scaled / 255);
    uint8_t b = (uint8_t)((colour & 0xFF) * scaled / 255);
    return strip.Color(r, g, b);
}

// Compute the current brightness for a breathing animation.
// Uses a sine-wave envelope: brightness cycles smoothly between 0 and
// peakBrightness with a full period of periodMs milliseconds.
static uint8_t pulseLevel(uint8_t peakBrightness, uint16_t periodMs)
{
    // phase: 0.0 at the start of each period, 1.0 at the end
    float phase = (float)(millis() % (uint32_t)periodMs) / (float)periodMs;

    // sine goes -1 → +1 over one full cycle; shift and scale to 0 → 1
    // sine goes -1 → +1; scale to 0.5 → 1.0 so the pulse never goes below 50%
    float t = (sinf(phase * 2.0f * (float)M_PI) + 1.0f) * 0.25f + 0.5f;

    return (uint8_t)(peakBrightness * t);
}

// ── Public API ─────────────────────────────────────────────────────────────

// Initialise the strip and blank all LEDs.
void ledsInit()
{
    strip.begin();
    strip.clear();
    strip.show();
}

// Store the desired zone (1–5) for the next ledsUpdate() frame.
void setHeartZone(uint8_t zone)
{
    if (zone < 1)
        zone = 1;
    if (zone > 5)
        zone = 5;
    if (zone != currentZone)
    {
        prevZone = currentZone;
        currentZone = zone;
        transitionStart = millis();
    }
}

void ledsSetSearching(bool searching)
{
    isSearching = searching;
}

// Compute and push one animation frame to the strip.
// Must be called every loop() iteration.
void ledsUpdate()
{
    if (isSearching)
    {
        // Chase a single dim-red pixel around the heart, scaled by LED_BRIGHTNESS.
        uint8_t pos = (uint8_t)((millis() / 100) % NEO_COUNT);
        uint8_t chaserBright = (uint8_t)(80U * LED_BRIGHTNESS / 255);
        strip.clear();
        strip.setPixelColor(FILL_ORDER[pos], strip.Color(chaserBright, 0, 0));
        strip.show();
        return;
    }

    // ── Zone transition blend factor (0 = previous zone, 1 = current zone) ──
    float t = 1.0f;
    uint32_t elapsed = millis() - transitionStart;
    if (elapsed < TRANSITION_MS)
        t = (float)elapsed / (float)TRANSITION_MS;

    uint8_t prevIdx = prevZone - 1;
    uint8_t curIdx = currentZone - 1;
    float inv = 1.0f - t;

    // ── Blend LED count ────────────────────────────────────────────────────
    uint8_t numLeds = (uint8_t)(ZONE_LED_COUNT[prevIdx] * inv + ZONE_LED_COUNT[curIdx] * t + 0.5f);

    // ── Blend colour channels ──────────────────────────────────────────────
    uint32_t pc = ZONE_COLOUR[prevIdx];
    uint32_t cc = ZONE_COLOUR[curIdx];
    uint8_t cr = (uint8_t)(((pc >> 16) & 0xFF) * inv + ((cc >> 16) & 0xFF) * t);
    uint8_t cg = (uint8_t)(((pc >> 8) & 0xFF) * inv + ((cc >> 8) & 0xFF) * t);
    uint8_t cb = (uint8_t)((pc & 0xFF) * inv + (cc & 0xFF) * t);
    uint32_t blendedColour = ((uint32_t)cr << 16) | ((uint32_t)cg << 8) | cb;

    // ── Blend peak brightness ──────────────────────────────────────────────
    uint8_t blendedPeak = (uint8_t)(ZONE_PEAK[prevIdx] * inv + ZONE_PEAK[curIdx] * t);

    // ── 60 bpm heartbeat pulse applied to all zones ────────────────────────
    uint8_t brightness = pulseLevel(blendedPeak, HEARTBEAT_MS);

    // ── Build frame ────────────────────────────────────────────────────────
    strip.clear();
    uint32_t scaledColour = scaleColour(blendedColour, brightness);
    for (uint8_t i = 0; i < numLeds; i++)
        strip.setPixelColor(FILL_ORDER[i], scaledColour);
    strip.show();
}
