#pragma once

// ── RSSI zone thresholds (dBm) ─────────────────────────────────────────────
// Derived from empirical walk test in home environment.
// All comparisons use smooth_rssi (rolling average of RSSI_SAMPLES readings).
// Closer device = higher (less negative) RSSI value.
//
//   Zone 1  (intimate — same body / touching):  smooth >= RSSI_Z1
//   Zone 2  (same room):                        smooth >= RSSI_Z2
//   Zone 3  (nearby room / hallway):             smooth >= RSSI_Z3
//   Zone 4  (far end of home):                  smooth >= RSSI_Z4
//   Zone 5  (out of range / no signal):         below RSSI_Z4
//
#define RSSI_Z1 -55 // dBm — enter zone 1 (closest)
#define RSSI_Z2 -63 // dBm — enter zone 2
#define RSSI_Z3 -71 // dBm — enter zone 3
#define RSSI_Z4 -79 // dBm — enter zone 4 (furthest before signal loss)

// ── Hysteresis ─────────────────────────────────────────────────────────────
// smooth_rssi must drop this many dBm below the current zone's entry threshold
// before the zone is downgraded.  Prevents rapid LED flickering at boundaries.
#define RSSI_HYSTERESIS 3 // dBm
