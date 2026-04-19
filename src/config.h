#pragma once

// ── RSSI zone thresholds (dBm) ─────────────────────────────────────────────
// Derived from empirical walk test in home environment.
// All comparisons use smooth_rssi (rolling average of RSSI_SAMPLES readings).
// Closer device = higher (less negative) RSSI value.
//
//   Zone 5  (intimate — same body / touching):  smooth >= RSSI_Z5
//   Zone 4  (same room):                        smooth >= RSSI_Z4
//   Zone 3  (nearby room / hallway):             smooth >= RSSI_Z3
//   Zone 2  (far end of home):                  smooth >= RSSI_Z2
//   Zone 1  (out of range / no signal):         below RSSI_Z2
//
#define RSSI_Z5 -55 // dBm — enter zone 5 (closest)
#define RSSI_Z4 -63 // dBm — enter zone 4
#define RSSI_Z3 -71 // dBm — enter zone 3
#define RSSI_Z2 -79 // dBm — enter zone 2 (furthest before signal loss)

// ── Hysteresis ─────────────────────────────────────────────────────────────
// smooth_rssi must drop this many dBm below the current zone's entry threshold
// before the zone is downgraded.  Prevents rapid LED flickering at boundaries.
#define RSSI_HYSTERESIS 3 // dBm
