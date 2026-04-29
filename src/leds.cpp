// leds.cpp — NeoPixel heart-ring driver implementation
//
// Hardware: 8 × WS2812B LEDs, daisy-chained, data on Feather pin 6.
// LEDs 0–7 are numbered in chain order and laid out in a heart shape.
// See docs/plan.md §3c for the physical position of each index.
//
// Animation model
// ───────────────
// Zone 1 (closest): all 8 LEDs, smooth sine breathing — never fully dark.
//
// Zones 2–5 (growing heartbeat): LEDs expand from LED 4 (bottom point)
// upward one row per beat, reach the full heart, then contract back down.
// Each beat is one full sine cycle (0 → peak → 0). The starting row
// depends on zone — farther zones start smaller and travel further:
//
//   Zone 2: 7 → 8 → 7            (3 beats, LED 0 bobs in/out at top)
//   Zone 3: 5 → 7 → 8 → 7 → 5   (5 beats)
//   Zone 4: 3 → 5 → 7 → 8 → 7 → 5 → 3  (7 beats)
//   Zone 5: 1 → 3 → 5 → 7 → 8 → 7 → 5 → 3 → 1  (9 beats, starts at tip)
//
// Each call to ledsUpdate() computes the current frame from millis() and
// pushes it to the strip. No delay() is used anywhere.
//
// Author: CloseLove project
// Date:   2026-04-18

#include "leds.h"
#include <Adafruit_NeoPixel.h>
#include <math.h>

// ── Internal strip instance ────────────────────────────────────────────────

static Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

// ── Zone colour and peak brightness (index 0 = zone 1) ────────────────────

// Peak brightness (0–255) for each zone before the LED_BRIGHTNESS master scale.
static const uint8_t ZONE_PEAK[5] = {
    240, // Zone 1: ~94% (closest) — breathing is 30%→100%→30% over BEAT_STEP_MS
    90,  // Zone 2: stable floor 50% of peak (~45), peak 90
    70,  // Zone 3: stable floor ~35, peak 70
    50,  // Zone 4: stable floor ~25, peak 50
    30,  // Zone 5: stable floor ~15, peak 30 (farthest)
};

// Base colour per zone as packed 0x00RRGGBB. Scaled by scaleColour() at render time.
static const uint32_t ZONE_COLOUR[5] = {
    0x00FF0000, // Zone 1: red         (closest)
    0x00FF4400, // Zone 2: orange-red
    0x00AA00CC, // Zone 3: purple
    0x004400FF, // Zone 4: blue-purple
    0x000000FF, // Zone 5: blue        (farthest)
};

// ── Growing heartbeat LED sequences (zones 2–5) ───────────────────────────
//
// Each entry is the LED count for that beat step. LEDs are filled from
// LED 4 (bottom point) upward using BOTTOM_UP_ORDER.
// Array index 0 maps to zone 2; index 3 maps to zone 5.

static const uint8_t SEQ_Z2[] = {7, 8, 7};
static const uint8_t SEQ_Z3[] = {5, 7, 8, 7, 5};
static const uint8_t SEQ_Z4[] = {3, 5, 7, 8, 7, 5, 3};
static const uint8_t SEQ_Z5[] = {1, 3, 5, 7, 8, 7, 5, 3, 1};

static const uint8_t *const ZONE_SEQ[4] = {SEQ_Z2, SEQ_Z3, SEQ_Z4, SEQ_Z5};
static const uint8_t ZONE_SEQ_LEN[4] = {3, 5, 7, 9};

// ── LED fill order ─────────────────────────────────────────────────────────
//
// BOTTOM_UP_ORDER fills from LED 4 (bottom point) outward symmetrically:
//   Count 1 → {4}
//   Count 3 → {4, 5, 3}
//   Count 5 → {4, 5, 3, 6, 2}
//   Count 7 → {4, 5, 3, 6, 2, 7, 1}
//   Count 8 → {4, 5, 3, 6, 2, 7, 1, 0}  (0 = top-centre dip, last to join)
static const uint8_t BOTTOM_UP_ORDER[NEO_COUNT] = {4, 5, 3, 6, 2, 7, 1, 0};

// ── Module state ───────────────────────────────────────────────────────────

// Active zone and previous zone for cross-fade blending (both clamped to 1–5).
static uint8_t currentZone = 5;
static uint8_t prevZone = 5;

// millis() timestamp when the last zone transition started.
static uint32_t transitionStart = 0;

// Boot searching mode: true until the RSSI buffer is fully seeded.
// ledsUpdate() shows a bottom-to-top chaser while this is set.
static bool isSearching = true;

// Step tracking for the growing heartbeat (zones 2–5).
// Resets whenever the active zone changes.
static uint8_t beatStepZone = 0;       // zone these values belong to
static uint8_t beatStepCur = 0;        // committed step index
static uint8_t beatStepPending = 0xFF; // next step waiting to apply (0xFF = none)
static uint8_t beatStepPrevCount = 0;  // LED count from the previous step (for incoming fade)

// ── Internal helpers ───────────────────────────────────────────────────────

// Scale a packed 0x00RRGGBB colour by brightness (0–255).
// LED_BRIGHTNESS is applied globally via strip.setBrightness() in ledsInit().
static uint32_t scaleColour(uint32_t colour, uint8_t brightness)
{
    uint8_t r = (uint8_t)(((colour >> 16) & 0xFF) * brightness / 255);
    uint8_t g = (uint8_t)(((colour >> 8) & 0xFF) * brightness / 255);
    uint8_t b = (uint8_t)((colour & 0xFF) * brightness / 255);
    return strip.Color(r, g, b);
}

// ── Wave helpers ──────────────────────────────────────────────────────────
//
// Full sine over one HEARTBEAT_MS period:
//   phase 0.0 / 1.0 = trough (0%)   phase 0.5 = peak (100%)
//   phase 0.25      = rising 50%    phase 0.75 = falling 50%
//
// Stable LEDs use max(50%, sineLevel) — floor at 50%, never dark.
// Incoming/outgoing LEDs use sineLevel directly — fade in/out through 0%.

// Brightness fraction (0.0–1.0) following a full sine over HEARTBEAT_MS.
static float sineLevel(float phase)
{
    return (1.0f - cosf(phase * 2.0f * (float)M_PI)) * 0.5f;
}

// Very subtle colour-pulse factor (±6%) at COLOUR_PULSE_MS.
static float colourPulse()
{
    float phase = (float)(millis() % (uint32_t)COLOUR_PULSE_MS) / (float)COLOUR_PULSE_MS;
    return 0.94f + 0.06f * ((sinf(phase * 2.0f * (float)M_PI) + 1.0f) * 0.5f);
}

// Zone 1 breathing: 30%→100%→30% over BEAT_STEP_MS period so it is
// clearly visible as a slow swell independent of the heartbeat rhythm.
static uint8_t breathLevel(uint8_t peakBrightness)
{
    float phase = (float)(millis() % (uint32_t)BEAT_STEP_MS) / (float)BEAT_STEP_MS;
    float t = 0.65f - 0.35f * cosf(phase * 2.0f * (float)M_PI);
    return (uint8_t)(peakBrightness * t);
}

// ── Public API ─────────────────────────────────────────────────────────────

// Initialise the strip and blank all LEDs.
void ledsInit()
{
    strip.begin();
    strip.setBrightness(LED_BRIGHTNESS);
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
        // Single red pixel chases from bottom point up through the heart,
        // pulsing in brightness for a more alive searching feel.
        uint8_t pos = (uint8_t)((millis() / 100) % NEO_COUNT);
        float phase = (float)(millis() % 800U) / 800.0f;
        float t = (sinf(phase * 2.0f * (float)M_PI) + 1.0f) * 0.5f;
        uint8_t chaserBright = (uint8_t)(40 + 80.0f * t);
        strip.clear();
        strip.setPixelColor(BOTTOM_UP_ORDER[pos], strip.Color(chaserBright, 0, 0));
        strip.show();
        return;
    }

    // ── Zone transition blend factor (0.0 = previous zone, 1.0 = current) ──
    float blend = 1.0f;
    uint32_t elapsed = millis() - transitionStart;
    if (elapsed < TRANSITION_MS)
        blend = (float)elapsed / (float)TRANSITION_MS;

    uint8_t prevIdx = prevZone - 1;
    uint8_t curIdx = currentZone - 1;
    float inv = 1.0f - blend;

    // ── Blend colour channels ──────────────────────────────────────────────
    uint32_t pc = ZONE_COLOUR[prevIdx];
    uint32_t cc = ZONE_COLOUR[curIdx];
    uint8_t cr = (uint8_t)(((pc >> 16) & 0xFF) * inv + ((cc >> 16) & 0xFF) * blend);
    uint8_t cg = (uint8_t)(((pc >> 8) & 0xFF) * inv + ((cc >> 8) & 0xFF) * blend);
    uint8_t cb = (uint8_t)((pc & 0xFF) * inv + (cc & 0xFF) * blend);
    // Apply slow colour pulse — gently modulates the zone colour independently
    // of the heartbeat brightness, giving a living "glow" feel.
    float cp = colourPulse();
    cr = (uint8_t)(cr * cp);
    cg = (uint8_t)(cg * cp);
    cb = (uint8_t)(cb * cp);
    uint32_t blendedColour = ((uint32_t)cr << 16) | ((uint32_t)cg << 8) | cb;

    // ── Blend peak brightness ──────────────────────────────────────────────
    uint8_t blendedPeak = (uint8_t)(ZONE_PEAK[prevIdx] * inv + ZONE_PEAK[curIdx] * blend);

    // ── Build and push frame ───────────────────────────────────────────────
    strip.clear();

    if (currentZone == 1)
    {
        // Zone 1: all 8 LEDs, 30%→100%→30% slow breath over BEAT_STEP_MS.
        uint8_t brightness = breathLevel(blendedPeak);
        uint32_t c = scaleColour(blendedColour, brightness);
        for (uint8_t i = 0; i < NEO_COUNT; i++)
            strip.setPixelColor(BOTTOM_UP_ORDER[i], c);
    }
    else
    {
        // Zones 2–5: growing heartbeat.
        // Stable LEDs (same before and after step): sine with 50% floor.
        // Incoming LEDs (just added this step): full sine from 0% up to join at 50%.
        // Outgoing LEDs (leaving next step): full sine back down to 0%.
        // Step commits at the trough (sineLevel≈0) so entries/exits are seamless.
        uint8_t zoneIdx = currentZone - 2;
        uint8_t seqLen = ZONE_SEQ_LEN[zoneIdx];
        uint32_t now = millis();

        // Reset on zone change.
        if (currentZone != beatStepZone)
        {
            beatStepZone = currentZone;
            beatStepCur = (uint8_t)((now / BEAT_STEP_MS) % seqLen);
            beatStepPending = 0xFF;
            beatStepPrevCount = ZONE_SEQ[zoneIdx][beatStepCur];
        }

        float beatPhase = (float)(now % (uint32_t)HEARTBEAT_MS) / (float)HEARTBEAT_MS;
        uint8_t newStep = (uint8_t)((now / BEAT_STEP_MS) % seqLen);
        if (newStep != beatStepCur)
            beatStepPending = newStep;
        // Commit at trough so incoming LEDs start from 0% and outgoing finish at 0%.
        if (beatStepPending != 0xFF && beatPhase < 0.05f)
        {
            beatStepPrevCount = ZONE_SEQ[zoneIdx][beatStepCur];
            beatStepCur = beatStepPending;
            beatStepPending = 0xFF;
        }

        uint8_t currentCount = ZONE_SEQ[zoneIdx][beatStepCur];
        uint8_t nextStep = (uint8_t)((beatStepCur + 1) % seqLen);
        uint8_t nextCount = ZONE_SEQ[zoneIdx][nextStep];
        float sine = sineLevel(beatPhase);

        for (uint8_t i = 0; i < currentCount; i++)
        {
            // Incoming (wasn't lit last step) or outgoing (won't be lit next step):
            // follow full sine so they fade in from 0% and out to 0%.
            bool transitioning = (i >= beatStepPrevCount) || (i >= nextCount);
            float level = transitioning ? sine : fmaxf(0.5f, sine);
            uint8_t brightness = (uint8_t)(blendedPeak * level);
            strip.setPixelColor(BOTTOM_UP_ORDER[i], scaleColour(blendedColour, brightness));
        }
    }

    strip.show();
}
