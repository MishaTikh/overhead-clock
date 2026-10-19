# Hardware

## What you need
- **Ulanzi TC001 Smart Pixel Clock** (about $40–50). Inside: ESP32, 8x32 RGB LED matrix, three buttons, light sensor,
  temperature/humidity sensor, battery-backed real-time clock, buzzer and battery. Overhead uses all of them except
  the battery (it just runs from USB power).
- A **USB-C data cable** (some cables only charge) and a computer with Chrome or Edge, once, to install the firmware.
- A **USB power adapter** where the clock will live (any 5 V phone charger).

No soldering, no wiring, no enclosure needed. If you'd like a custom look, print a shell or a front diffuser for the
TC001; the matrix area is 32 x 8 pixels.

## Where to put it
Anywhere with Wi-Fi. The clock gets plane positions from the internet, so it doesn't need a view of the sky.

## Pins (for developers)
From the AWTRIX 3 project's TC001 pin map: LED matrix data GPIO32 (WS2812, rows zigzag from the top left),
buttons left GPIO26 / middle GPIO27 / right GPIO14, light sensor GPIO35, buzzer GPIO15, I2C SDA 21 / SCL 22
(SHT3x sensor at 0x44, DS1307 real-time clock at 0x68).

## Going back to the stock firmware
Before installing Overhead you can back up the stock firmware with
`esptool.py read_flash 0 0x800000 tc001-stock.bin`, and write it back later with
`esptool.py write_flash 0 tc001-stock.bin`. The AWTRIX 3 web installer also works on the TC001.
