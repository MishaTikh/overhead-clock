# Hardware

## What you need
- **Ulanzi TC001 Smart Pixel Clock** (about $40–50). Inside: ESP32, 8x32 RGB LED matrix, three buttons, light sensor,
  temperature/humidity sensor, battery-backed real-time clock, buzzer and battery. Overhead uses all of them except
  the battery (it just runs from USB power).
- A **USB power adapter** where the clock will live (any 5 V phone charger).
- A phone and, for the one-click installer, a Windows PC on the same Wi-Fi. Installs go over Wi-Fi; no cable needed.

No soldering, no wiring, no enclosure needed. If you'd like a custom look, print a shell or a front diffuser for the
TC001; the matrix area is 32 x 8 pixels.

## Where to put it
Anywhere with Wi-Fi. The clock gets plane positions from the internet, so it doesn't need a view of the sky.

## Pins (for developers)
From the AWTRIX 3 project's TC001 pin map: LED matrix data GPIO32 (WS2812, rows zigzag from the top left),
buttons left GPIO26 / middle GPIO27 / right GPIO14, light sensor GPIO35, buzzer GPIO15, I2C SDA 21 / SCL 22
(SHT3x sensor at 0x44, DS1307 real-time clock at 0x68).

## Installing other firmware later
Overhead's *Update firmware* button accepts any ESP32 application image, so you can switch the clock to AWTRIX 3 or
Tasmota builds made for the TC001 the same way. Ulanzi doesn't publish its stock firmware as a download.

## USB flashing
The TC001's USB auto-reset circuit doesn't put the ESP32 into flashing mode from Windows (esptool reports "No serial
data received"; the stock firmware just reboots). It's reported to work from Linux. That's why Overhead installs and
updates over Wi-Fi. The CH340 USB driver is still needed if you want to watch the serial output.
