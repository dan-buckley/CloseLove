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
    153, // Zone 5: ~60% (solid, no pulse)
};

// Breathing period in milliseconds for zones 1–4.
// Shorter = faster pulse. Zone 5 entry is unused (solid).
static const uint16_t ZONE_PERIOD_MS[5] = {
    4000, // Zone 1: long, slow breath
    3000, // Zone 2: medium breath
    2000, // Zone 3: quicker breath
    1000, // Zone 4: fast pulse
    0,    // Zone 5: solid — not used
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

// Active zone, clamped to 1–5.
static uint8_t currentZone = 1;

// ── Internal helpers ───────────────────────────────────────────────────────

// Scale a packed 0x00RRGGBB colour by brightness (0–255).
// Each channel is multiplied by (brightness / 255) using integer arithmetic.
static uint32_t scaleColour(uint32_t colour, uint8_t brightness)
{
    uint8_t r = (uint8_t)(((colour >> 16) & 0xFF) * brightness / 255);
    uint8_t g = (uint8_t)(((colour >> 8) & 0xFF) * brightness / 255);
    uint8_t b = (uint8_t)((colour & 0xFF) * brightness / 255);
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
    float t = (sinf(phase * 2.0f * (float)M_PI) + 1.0f) * 0.5f;

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
    currentZone = zone;
}

// Compute and push one animation frame to the strip.
// Must be called every loop() iteration.
void ledsUpdate()
{
    uint8_t zoneIdx = currentZone - 1; // convert 1-based zone to 0-based index
    uint8_t numLeds = ZONE_LED_COUNT[zoneIdx];
    uint32_t colour = ZONE_COLOUR[zoneIdx];
    uint8_t peak = ZONE_PEAK[zoneIdx];

    // Zone 5 is solid; all other zones breathe.
    uint8_t brightness;
    if (currentZone == 5)
    {
        brightness = peak;
    }
    else
    {
        brightness = pulseLevel(peak, ZONE_PERIOD_MS[zoneIdx]);
    }

    // Build the frame: light only the LEDs for this zone, in symmetric order.
    strip.clear();
    uint32_t scaledColour = scaleColour(colour, brightness);
    for (uint8_t i = 0; i < numLeds; i++)
    {
        strip.setPixelColor(FILL_ORDER[i], scaledColour);
    }

    strip.show();
}
