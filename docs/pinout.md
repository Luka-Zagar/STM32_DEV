# Pin allocation — EkoSonda / Nucleo-G474RE

Written before wiring anything; check against the Nucleo-G474RE user manual
(UM2570) for solder-bridge / ST-LINK / LSE conflicts before changing.

| Pin | AF | Signal | Notes |
|-----|----|--------|-------|
| PA2 | AF7 | USART2_TX | Routed to ST-LINK VCP (no external wiring needed). Debug console, 115200 8N1. |
| PA3 | AF7 | USART2_RX | Routed to ST-LINK VCP (no external wiring needed). Debug console, 115200 8N1. |
| PC4 | AF7 | USART1_TX | To GPS module (NEO-8M) RX. 9600 8N1. |
| PC5 | AF7 | USART1_RX | To GPS module (NEO-8M) TX. 9600 8N1. |
| PB10 | AF7 | USART3_TX | To ESP8266 (ESP-01S) RX. 115200 8N1 (AT firmware default; adjust if the module reports a different baud). Give the ESP-01S its own decoupling (bulk electrolytic + ceramic) - it spikes 300-400mA on TX bursts and browns out weak rails. |
| PB11 | AF7 | USART3_RX | To ESP8266 (ESP-01S) TX. 115200 8N1. |
| PB12 | -  | SD_CS | GPIO output, software-driven SPI2 chip select for the Micro SD shield. Shield labels this pin "D8/SS" on its own silkscreen (Arduino-shield heritage) - ignore that label, it's jumper-wired here, not header-stacked. |
| PB13 | AF5 | SPI2_SCK | To Micro SD shield CLK (its "D5"). Running at DIV64 (~2.66MHz, fPCLK/64) for data transfers, not the originally planned DIV8 (~21.25MHz) - see Bring-up notes below. |
| PB14 | AF5 | SPI2_MISO | To Micro SD shield MISO (its "D6"). |
| PB15 | AF5 | SPI2_MOSI | To Micro SD shield MOSI (its "D7"). |
| PC6 | -  | LED green | GPIO output, active high. One leg to GND, other via 220ohm resistor to this pin. Replaces the retired LD2/PB0 heartbeat LEDs. |
| PC7 | -  | LED white | GPIO output, active high. Same wiring as LED green. |
| PC8 | -  | LED red | GPIO output, active high. Same wiring as LED green. |
| PC9 | -  | SD eject button | GPIO input. One leg to GND, other to this pin and to 3.3V via 10kohm resistor - idle high, pressed = low. External pull-up already present, no internal pull needed; debounce in software. Wired up in app/task_sd.c: press to safely check the card out (syncs+closes the log file, unmounts) before physically removing it; press again after reinserting to remount and start a new session file. |
| PB8 | AF4 | I2C1_SCL | Shared bus - to INA3221 SCL and MPU-9250/6500 SCL. Open-drain, internal pull-up enabled alongside whatever external pull-up the modules have. 400kHz (Fast Mode). |
| PB9 | AF4 | I2C1_SDA | Shared bus - to INA3221 SDA and MPU-9250/6500 SDA. Same wiring notes as SCL. |

INA3221 (battery monitor, addr 0x40 - A0 tied to GND): TC pin tied to GND
(mode-select strap, must not float - see datasheet). PV/CRIT/WARNING alert
pins left unconnected (not used - this project polls registers over I2C
instead of using hardware alert interrupts). Only channel 3 is used (the
battery, through a 0.1 Ohm/R100 shunt); channels 1/2's IN+/IN- should be
shorted together rather than left floating.

Note: the first INA3221 module used here was damaged (see Bring-up notes
below) and replaced; the replacement module has the battery wired through
channel 3 instead of channel 1 - devices/ina3221.c reads CH3's registers
(0x05/0x06) accordingly, not CH1's (0x01/0x02).

MPU-9250/6500 (labeled "MPU-92/65" on this module - sold as either
variant depending on batch; IMU, addr 0x68 - AD0 tied to GND). This
module has an NCS pin (chip select, for boards that support both SPI
and I2C); the bring-up initially failed intermittently (address ACKs
worked, but real register reads and even the address itself were
unstable, briefly appearing at 0x69) and momentarily holding NCS high
cleared it - but it kept working fine afterward with NCS disconnected
again, so this board has its own onboard pull-up and NCS doesn't need a
permanent connection. The instability was more likely just a loose/
flaky connection in general (this project has hit that before) that
happened to get reseated while testing NCS, not a real NCS requirement -
noted here in case another module in this family DOES need it tied
high. This project's actual module reports WHO_AM_I=0x73 (MPU9255, a
later MPU9250 successor with an identical accel register map). Project
brief already flags this part as discontinued/frequently remarked - the
probe also accepts WHO_AM_I=0x71 (MPU9250), 0x70 (MPU6500, no
magnetometer), or 0x68 (MPU6050) rather than insisting on exactly one,
since only the accelerometer is used here (vibration sensing) and a
missing/absent magnetometer doesn't
matter for that. Configured for +/-4g full scale (power-on default is
+/-2g, which risks clipping on a bus-mounted sensor).

Retired: PA5 (LD2, on-board user LED) and PB0 (old external heartbeat LED) are no longer driven by firmware and are free for reuse - superseded by the green/white/red LED trio above.

## Power

Two independent power paths, meant to coexist without any manual switching
once set up:

- **`JP5` jumper: set to `E5V`** (moved off the default `5V_STLK`/`U5V`
  position). In this position the target STM32 always runs from whatever is
  fed into the **E5V pin**, regardless of whether USB is connected - USB then
  only powers the ST-LINK debug/programming circuitry (and the VCP console),
  never the target. This is what makes USB safely hot-pluggable for
  programming without power-cycling the running board.
- **Battery: 5V → `E5V` pin, GND → any `GND` pin** (CN7/CN8/CN9/CN10 share a
  common ground plane, any of them works). Leave this connected permanently;
  it does nothing when `JP5` is on the other position, so there's no conflict
  with USB.
- **3.3V pin is an output only** (from the board's onboard LDO, meant to power
  external peripherals) - never feed a supply into it. Backfeeding it needs a
  solder-bridge (SB50) modification and isn't done on this board.
- Workflow this enables: plug USB in whenever you want to flash/debug/watch
  the console; unplug it whenever - the target keeps running off battery the
  whole time, no interruption either way.

## LED status (app/task_led.c)

White (PC7) is the heartbeat, wired independently in task_heartbeat.c and
never touched by task_led.c - it never changes meaning, on purpose, so it's
the one failure nothing else in this scheme can report.

| LED | Pattern | Meaning |
|-----|---------|---------|
| White | 1 short flash/sec, always | Heartbeat. Main loop is running. Never changes meaning. If white stops -> firmware hung. |
| Green | solid on | Logging good rows: SD mounted + GPS fix |
| Green | slow flash (1/2s) | Logging, but no GPS fix - timestamps still real (RTC-based), just not GPS-disciplined |
| Green | off | Not logging - SD not mounted / init failed |
| Red | off | No fault (normal state) |
| Red | 1 flash/sec | SD write error - DATA AT RISK (latched until acked) |
| Red | 2 flash/sec | I2C fault - INA3221 or IMU not responding (latched, not wired up yet) |
| Red | 3 flash/sec | Battery low (live state, not wired up yet - no INA3221) |
| Red | solid | Battery critical / imminent shutdown (live state, not wired up yet) |

Notes on the implementation, not just the spec:

- Green intentionally ignores battery state even though the original spec's
  "solid on" case mentions "battery OK" - red already owns battery reporting
  on its own, so folding it into green too would just show the same
  information twice under two different failure semantics. Green here is
  purely SD-mounted + GPS-fix.
- Red faults are worst-first priority when more than one is true at once:
  battery critical (solid) > SD write error (1Hz) > I2C fault (2Hz) >
  battery low (3Hz) > off.
- "Latched" (SD write error, I2C fault) means it stays on once triggered even
  if the condition clears itself, until acknowledged - press `'A'` inside the
  console's `L` menu to clear it. Acking does not reset the underlying error
  counters used for diagnostics (`Task_Logger_Write_Errors()` etc.), only the
  LED's own latch.
- Battery-low/critical and the I2C fault have no sensor to drive them yet
  (no INA3221/IMU on the bus). `Task_LED_Set_Battery_State()` and
  `Task_LED_Report_I2C_Fault()` are ready for those tasks to call into with
  zero changes needed here once they exist.

## Logging: session files and safe removal

- **A new file is started every session** - every boot, and every time the
  card is remounted after an eject (see the PC9 button above) - rather than
  one file appended to forever. Named `YYYYMMDD_HHMMSS.CSV` if GPS has
  disciplined the RTC by the time the first row is written; otherwise a
  sequential `BOOTnnnn.CSV` (found by scanning the card for the highest
  existing number and using the next one, so it's collision-safe across
  power cycles without needing a persistent counter). The name is decided
  once, at file-creation time - it does not get renamed mid-session if a fix
  shows up later. Needed enabling long filenames (`FF_USE_LFN=1` in
  ffconf.h, plus vendoring `ffunicode.c`) since the 8.3 short-name limit
  can't fit a readable timestamp.
- **Safe card removal via the PC9 button**: pressing it while mounted syncs
  and closes the currently-open file *before* unmounting - the point is to
  guarantee nothing is mid-write the instant the card is physically pulled.
  Never yank the card without doing this first; FAT is not crash-safe (see
  the SPI/FAT-corruption note below for what that looks like in practice).
  Pressing it again after reinserting a card re-runs the mount + self-test
  ritual and starts a fresh session file.

## Bring-up notes

- **SD card must be formatted FAT32, not exFAT.** `FF_FS_EXFAT` is compiled
  out of this project's FatFs build (keeps flash/RAM down - CSV logs never
  need exFAT's feature set), so an exFAT-formatted card mounts with
  `FR_NO_FILESYSTEM` even though the card itself is working fine. First
  reformat attempt on this project's card silently produced exFAT (macOS
  Disk Utility defaults to it for cards in this size class) - the fix is
  picking "MS-DOS (FAT)" explicitly, not "ExFAT", in the format dialog
  (Windows: Explorer's formatter defaults to FAT32 fine for cards <=32GB).
- **SPI2 fast-transfer clock had to drop from DIV8 (~21.25MHz) to DIV64
  (~2.66MHz).** Symptom was the same `FR_NO_FILESYSTEM` even on a correctly
  FAT32-formatted card: `disk_initialize()` succeeded at the slow
  (DIV256, ~664kHz) init-sequence clock, but the very first fast-clock
  transfer (FatFs reading the boot sector) came back as data that didn't
  parse as a valid FAT header. SD SPI mode has CRC checking disabled by
  default, so bit errors from a marginal signal at 21MHz over jumper wires/
  breadboard pass through silently instead of raising a read error - there's
  no driver-level symptom other than "the data doesn't make sense one layer
  up." Dropping to DIV64 fixed it immediately (mount + self-test write/read
  both passed). Revisit going faster once this is a real PCB trace instead
  of jumper wires - 21MHz is well within spec for a card, just not for this
  wiring.
- **First INA3221 module was damaged by a 9V mis-power event** (briefly
  powered from 9V instead of 3.3V before this was caught and corrected).
  Symptom: `i2c_probe()`/a full 0x08-0x77 bus scan found nothing at all,
  even after confirming (separately) that SCL/PB8 and SDA/PB9 both
  physically toggle correctly as plain GPIO, and that the I2C peripheral
  itself was issuing real START/address/STOP sequences and getting a real
  NACK back (traced via raw `ISR` reads: `TXE|BUSY` right after START, then
  `TXE|NACKF|STOPF` ~1ms later) - i.e. the driver and STM32-side wiring were
  provably fine, the chip just wasn't answering on the bus. The module's
  `VS`/`PV` status LEDs staying lit didn't rule this out - those reflect the
  analog side (a simple power-present indicator and the chip's own internal
  power-good comparator output), which can keep working even if the digital
  I2C pins were damaged by briefly seeing ~9V (if the module's own pull-ups
  reference VCC, that's well past the STM32 GPIO's ~3.6V absolute max too,
  though no STM32-side damage was observed here). Swapping in a second,
  never-over-powered INA3221 module fixed it immediately - `ina3221_probe()`
  passed on the first try. Lesson: a `VS`/power LED being lit is not proof
  an I2C device's digital interface survived an overvoltage event.
