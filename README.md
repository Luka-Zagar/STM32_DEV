# STM32_DEV

EkoSonda: a self-powered embedded station for urban environmental monitoring,
mounted on city buses. Logs air quality, temp/humidity/pressure, brightness,
vibration, and GNSS data, timestamped and geotagged, to an SD card.
Opportunistically uploads at the depot over WiFi (store-and-forward, not
real-time). Target: city-wide ecological mapping for Slovenia.

Register-level CMSIS on an STM32 Nucleo-G474RE, no HAL. See `CLAUDE.md` for
the full project brief, architecture, and build order.

## Current status

Spine step 1 only: boot, clock (170 MHz PLL), SysTick @ 1kHz, LED heartbeat.
Everything else (console, scheduler, sensors, SD, WiFi) is being rebuilt on
top of this per the build order in `CLAUDE.md`.

## Layout

```
core/     boot, clock, systick, main
drivers/  register-level peripheral drivers (empty so far)
devices/  sensor/chip logic built on drivers (empty so far)
app/      scheduler, tasks, ring buffer (empty so far)
docs/     pin allocation, wiring notes
Inc/      CMSIS device register header (stm32g474xx.h)
```

## Build

```
cmake --preset Debug
cmake --build --preset Debug
```
