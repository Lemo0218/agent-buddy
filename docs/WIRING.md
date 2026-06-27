# Wiring — XIAO ESP32-S3 ↔ ST7789V 240×320 SPI LCD (Seengreat)

These connections match the pins already defined in `src/config.h`.
The ST7789 is write-only over SPI, so the display's MISO is not needed.

## Connection table

| LCD pin (aliases)        | → | XIAO pin | GPIO  | config.h        |
|--------------------------|---|----------|-------|-----------------|
| VCC                      | → | 3V3      | —     | (power)         |
| GND                      | → | GND      | —     | (ground)        |
| DIN  (SDA / MOSI)        | → | D10      | GPIO9 | `BUDDY_TFT_MOSI`|
| CLK  (SCL / SCK)         | → | D8       | GPIO7 | `BUDDY_TFT_SCK` |
| CS                       | → | D1       | GPIO2 | `BUDDY_TFT_CS`  |
| DC   (RS)                | → | D2       | GPIO3 | `BUDDY_TFT_DC`  |
| RST  (RES)               | → | D3       | GPIO4 | `BUDDY_TFT_RST` |
| BL   (BLK / backlight)   | → | D4       | GPIO5 | `BUDDY_TFT_BL`  |

> BL can instead go straight to **3V3** if you don't need brightness control
> (then set `BUDDY_TFT_BL` aside). Driving it from D4 lets the firmware dim/blink it.

## Board diagram (USB-C at the top)

```
                       ┌──────── USB-C ────────┐
                       │      XIAO ESP32-S3     │
                       ├───────────────────────┤
      CS  ◀──── D1  ●  │  D0                 D7 │  ●
      DC  ◀──── D2  ●  │  D1 ◀── CS           D8│  ● ────▶ CLK
      RST ◀──── D3  ●  │  D2 ◀── DC           D9│  ●            (D9/MISO unused)
      BL  ◀──── D4  ●  │  D3 ◀── RST         D10│  ● ────▶ DIN
               D5  ●  │  D4 ◀── BL          3V3│  ● ────▶ VCC
               D6  ●  │  D5                  GND│  ● ────▶ GND
                       │  D6                  5V │  ●
                       └───────────────────────┘
   left header                                    right header

   ┌──────────────────────────────┐
   │   ST7789V  240×320  LCD       │
   │                              │
   │  VCC GND DIN CLK CS DC RST BL │   ← 8-pin SPI header
   └──┬───┬───┬───┬──┬──┬───┬───┬──┘
      │   │   │   │  │  │   │   │
     3V3 GND D10 D8 D1 D2 D3  D4   (XIAO pins)
```

## Quick sanity rules

- **Power from 3V3, never 5V** — ST7789 logic is 3.3 V.
- Keep the SPI wires (DIN, CLK) short; long jumpers cause glitches at high clock.
- Common ground is mandatory (LCD GND ↔ XIAO GND).

## Enabling the display in firmware

1. Wire as above and double-check the silk labels on *your* Seengreat board
   (pin order varies between batches — match by **name**, not position).
2. In `src/config.h` set `#define BUDDY_HAS_DISPLAY 1`.
3. Implement the `#if BUDDY_HAS_DISPLAY` branch in `src/display.cpp`
   (add a driver such as `bodmer/TFT_eSPI` or `lovyan03/LovyanGFX` to
   `lib_deps`, init with the pins above, draw a face per mood + speech bubble).
4. `pio run -t upload`.
