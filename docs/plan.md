# CloseLove — Project Plan

> Two wearable proximity devices that light up a heart-shaped ring of 8 NeoPixel LEDs
> based on how close the two people wearing them are to each other.

---

## 1. How it works (one-paragraph summary)

Each unit continuously broadcasts a short radio ping via its RFM69HCW transceiver.
When it receives a ping back from the paired unit it reads the signal strength (RSSI).
The stronger the signal the closer the units are. RSSI is mapped to one of five
distance zones. Zone 1 (far away) shows a single, dimly-pulsing blue LED. Zone 5
(close together) lights all eight LEDs to a warm red at 60 % brightness.
The two units are hardware-identical; the only difference is the NODE_ID value
compiled into each one (see §7).

---

## 2. Bill of materials

| Qty | Item | Notes |
|-----|------|-------|
| 2 | Adafruit Feather 32u4 RFM69HCW 915 MHz | Already owned |
| 16 | WS2812B LED modules (individual, through-hole pads) | 8 per unit |
| 2 | 3 × AA battery holder with JST or bare wires | 4.5 V supply |
| ~16 | Short lengths of hookup wire | LED chain |
| 2 | Small enclosures / wearable form-factor | TBD |

> **Power note:** Battery + connects to the Feather **BAT** pad; GND to **GND**.
> The same battery rail powers the NeoPixel VCC directly (do not take NeoPixel
> power from a Feather output pin — they cannot supply enough current).
> 3 × AA = 4.5 V, which is within the BAT pin's acceptable range (3.5 – 6 V).
>
> **⚠ Do not connect USB and the AA batteries at the same time.** The onboard
> MCP73831 LiPo charger IC sits on the BAT rail. With USB plugged in it will
> attempt to charge whatever is on BAT — which is unsafe for non-rechargeable
> AAs. Disconnect the battery holder before plugging in USB to flash firmware.
>
> **Logic-level note:** The Feather outputs 3.3 V on digital pins; WS2812B
> ideally wants a 5 V data signal. At the modest brightnesses used here (max
> 60 %) the timing tolerance is wide enough that a direct connection usually
> works. See §8 for optional protection components to add later.

---

## 3. Wiring & pinout

### 3a. Feather 32u4 pin assignments

```
Feather 32u4
─────────────────────────────────────────────
Pin  4   ── RFM69 RST       (already wired on board)
Pin  7   ── RFM69 IRQ/INT   (already wired on board)
Pin  8   ── RFM69 CS        (already wired on board)
MOSI/MISO/SCK ── RFM69 SPI  (already wired on board)

Pin  6   ── NeoPixel chain DIN (LED 0)   [direct — see §8 for optional 300 Ω upgrade]
3V3      ── (logic reference only — do NOT power NeoPixels from here)
GND      ── common ground
BAT pad  ── 3 × AA battery + (4.5 V)
⚠ Disconnect batteries before connecting USB
```

### 3b. NeoPixel chain wiring

Each WS2812B module has four connections: **VCC**, **GND**, **DIN** (data in),
**DOUT** (data out). They are daisy-chained so that each LED's DOUT connects
to the next LED's DIN.

```
Battery 4.5V ── Feather BAT pad
Battery 4.5V ──────────────────────────────────────────── NeoPixel VCC (all 8, in parallel)
Battery GND  ── Feather GND pad
Battery GND  ──────────────────────────────────────────── NeoPixel GND (all 8, in parallel)

Feather Pin 6 ── LED0 DIN   [direct for now — 300 Ω + 100 µF upgrades in §8]
                          LED0 DOUT ── LED1 DIN
                                        LED1 DOUT ── LED2 DIN
                                                      LED2 DOUT ── LED3 DIN
                                                                    LED3 DOUT ── LED4 DIN
                                                                                  LED4 DOUT ── LED5 DIN
                                                                                                LED5 DOUT ── LED6 DIN
                                                                                                              LED6 DOUT ── LED7 DIN
                                                                                                                            LED7 DOUT (leave unconnected)
```

### 3c. Heart-shape LED positions

LEDs are numbered 0 – 7 in chain order. Place them around the heart outline
so the wire runs continuously around the perimeter:

```
        [1]   [2]
     [0]         [3]
        [7]   [4]
           [6][5]
```

| Index | Position     | Chain order |
|-------|-------------|-------------|
| 0     | Left side    | 1st in chain |
| 1     | Upper-left bump | 2nd |
| 2     | Upper-right bump | 3rd |
| 3     | Right side   | 4th |
| 4     | Lower-right  | 5th |
| 5     | Bottom-right | 6th |
| 6     | Bottom-left  | 7th |
| 7     | Lower-left   | 8th (last) |

> When Zone 1 is active only LED 0 lights up (the first in the chain). As zones
> increase, LEDs are added outward around the heart: 0→1→7→2→6→3→5→4 so the
> lit portion always looks symmetrical from the wearer's point of view.

---

## 4. Distance zones

| Zone | Approx. distance | LEDs on | Behaviour | Colour |
|------|-----------------|---------|-----------|--------|
| 1 | Far (> ~10 m) | 1 | Slow breathing pulse, ~8 % peak brightness | Blue (#0000FF) |
| 2 | ~5 – 10 m | 3 | Slow breathing pulse, ~15 % peak | Blue-purple (#4400FF) |
| 3 | ~2 – 5 m | 5 | Gentle pulse, ~25 % peak | Purple (#AA00CC) |
| 4 | ~1 – 2 m | 7 | Faster pulse, ~40 % peak | Orange-red (#FF4400) |
| 5 | Close (< ~1 m) | 8 | Solid on, 60 % brightness | Red (#FF0000) |

> RSSI thresholds are starting-point estimates (defined in `main.cpp`). They
> **must be calibrated** in the real environment (see Module 6).

---

## 5. Modules and build order

Work through the modules in sequence. Each module has a clear done-state before
moving on.

---

### Module 1 — Hardware assembly *(do first)*

**Goal:** Both units power on, the serial monitor is reachable, and the radio
chip is detected.

| # | Task |
|---|------|
| 1.1 | Wire 3 × AA holder → Feather USB pad + common GND |
| 1.2 | Solder NeoPixel chain on a bench strip (not heart shape yet); wire to Pin 6 via 300 Ω + 100 µF cap |
| 1.3 | Upload current skeleton; confirm serial output and radio init OK for both units |

---

### Module 2 — NeoPixel driver *(LED behaviour, no radio)*

**Goal:** A single function `setHeartZone(zone)` drives the correct LEDs with
the correct colour, brightness, and pulse behaviour for zones 1 – 5.

| # | Task |
|---|------|
| 2.1 | Add `Adafruit NeoPixel` library to `lib_deps` in `platformio.ini` |
| 2.2 | Write `leds.h` / `leds.cpp` — initialise strip, define colour palette array |
| 2.3 | Implement LED-count table (how many LEDs light per zone) and LED order array (symmetric fill) |
| 2.4 | Implement breathing-pulse function using `millis()`-based sine curve (no `delay()`) |
| 2.5 | Implement `setHeartZone(zone)` — calls pulse or solid depending on zone |
| 2.6 | Test with a hard-coded zone that cycles 1 → 5 → 1 on a 2 s timer |

---

### Module 3 — Radio link *(RSSI measurement)*

**Goal:** Both units are pinging each other; RSSI is printed to serial every
200 ms and the smoothed value is stable.

| # | Task |
|---|------|
| 3.1 | Complete `initRadio()` in `main.cpp` (frequency, encryption key, power level) |
| 3.2 | Implement non-blocking TX/RX loop (already sketched in skeleton) |
| 3.3 | Verify smoothed RSSI values in serial monitor with both units at different distances |
| 3.4 | Document the raw RSSI readings at representative distances for your environment |

---

### Module 4 — RSSI → zone mapping

**Goal:** `rssiToZone(rssi)` returns a stable zone (1 – 5); zone is printed to
serial alongside raw RSSI.

| # | Task |
|---|------|
| 4.1 | Define five RSSI threshold constants in `config.h` (new header) |
| 4.2 | Implement `rssiToZone()` using a lookup against those thresholds |
| 4.3 | Add hysteresis (zone must stay the same for N consecutive readings before updating) to prevent flickering |

---

### Module 5 — Integration

**Goal:** The full behaviour works end-to-end on both units simultaneously.

| # | Task |
|---|------|
| 5.1 | Wire zone output into `setHeartZone()` call in the main loop |
| 5.2 | Handle "no signal / timeout" state — fall back to Zone 1 after 2 s of no RX |
| 5.3 | Walk-test: move the units apart and together and observe the heart react correctly |

---

### Module 6 — Calibration & field tuning

**Goal:** Zone boundaries feel natural in the intended wearable environment (bodies,
pockets, fabric all affect RSSI significantly vs. open air).

| # | Task |
|---|------|
| 6.1 | Log RSSI at 1 m, 2 m, 5 m, 10 m while units are worn (not held in the hand) |
| 6.2 | Update the five threshold constants in `config.h` to match real readings |
| 6.3 | Tune pulse speed (currently 200 ms TX interval) if LED reaction feels laggy |

---

### Module 7 — Physical build & final assembly

**Goal:** Both units are in their wearable form with the heart mounted.

| # | Task |
|---|------|
| 7.1 | Transfer NeoPixel chain to heart-shape layout (§3c above) |
| 7.2 | Secure all wiring; confirm nothing shorts when enclosure is closed |
| 7.3 | Final range test wearing the devices |

---

## 6. Code documentation standard

All new source files must include:

1. **File header** — filename, one-line purpose, author, date.
2. **Section banners** — `// ── Section name ──` dividers (already used in the skeleton).
3. **Every `#define`** — inline comment explaining units and how to tune it.
4. **Every function** — a short doc-comment above the signature:
   ```cpp
   // Smooths incoming RSSI with a rolling average.
   // Returns the average of the last RSSI_SAMPLES readings.
   int smoothRssi(int newRssi);
   ```
5. **Magic numbers** — none; everything in `config.h` with a comment.

---

## 7. Device identity — which unit is which?

> **Both units are hardware-identical. The only difference is a single
> `#define` compiled into the firmware.**

Before uploading, open `main.cpp` and set:

| Unit | Change |
|------|--------|
| Person A's device | `#define NODE_ID 1` and `#define PEER_ID 2` |
| Person B's device | `#define NODE_ID 2` and `#define PEER_ID 1` |

Label each unit (e.g. a small sticker inside the enclosure) so you know which
is which when re-flashing.

---

## 8. Future enhancements / backlog (out of scope for v1)

**Hardware protection (add when parts are available)**
- 300 Ω resistor on Pin 6 → LED0 DIN (suppresses data-line ringing; reduces risk of colour glitches)
- 100 µF electrolytic capacitor across NeoPixel VCC/GND near the first LED (smooths power spikes on strip startup)
- 74AHCT125 level-shifter between Pin 6 and LED DIN if colour glitches persist without the resistor

**USB / battery safety reminder (to address before regular use)**
- ⚠ The MCP73831 LiPo charger sits on the BAT rail. With USB plugged in it will attempt to charge whatever is on BAT — unsafe with non-rechargeable AAs. Consider adding a physical switch or a diode to isolate the battery when flashing, or simply always disconnect the AA holder before plugging in USB.

**Other**
- Low-power sleep between radio pings to extend battery life
- Battery-level indicator (fade everything red when AA voltage drops below 3.6 V)
- Haptic motor pulse on zone change
- Over-the-air NODE_ID selection (button hold at boot)
