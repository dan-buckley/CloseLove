# CloseLove

Two wearable proximity devices that light up a heart-shaped ring of 8 WS2812B LEDs
based on how close the paired device is. Built on the Adafruit Feather 32u4 RFM69HCW
(915 MHz). Distance is measured via radio signal strength (RSSI) and mapped to one of
five zones — from a single dim blue pulse when far apart, to a full warm-red breathing
heart when close together.

## Animation

**Zone 1 (closest):** All 8 LEDs breathe together — slow sine 30% → 100% → 30% over 2 s.

**Zones 2–5 (farther):** Growing heartbeat. LEDs expand from the bottom point outward,
one row per beat (2 s per step). Each beat follows a full sine wave:
- Stable LEDs hold a 50% floor and rise to peak together
- Incoming LEDs fade in from 0%, joining the others at the rising half
- Outgoing LEDs fade out to 0% before the step changes — no visible pop

Zone colour shifts from red (close) through orange, purple, blue-purple to blue (far).

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | Adafruit Feather 32u4 RFM69HCW 915 MHz |
| LEDs | 8 × WS2812B, daisy-chained, data on Pin 6 |
| Power | 3 × AA (4.5 V) via BAT pad — disconnect before USB |

See [docs/plan.md](docs/plan.md) for full wiring, zone tables, and build modules.

## Flashing

```bash
# Requires PlatformIO
~/.platformio/penv/bin/pio run --target upload
```

Set `NODE_ID` and `PEER_ID` in `src/main.cpp` before flashing each unit (see `docs/plan.md §7`).
