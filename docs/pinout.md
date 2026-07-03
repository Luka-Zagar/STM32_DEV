# Pin allocation — EkoSonda / Nucleo-G474RE

Written before wiring anything; check against the Nucleo-G474RE user manual
(UM2570) for solder-bridge / ST-LINK / LSE conflicts before changing.

| Pin | AF | Signal | Notes |
|-----|----|--------|-------|
| PA5 | -  | LD2 (on-board user LED) | GPIO output, active high. Step 1 heartbeat. |
| PB0 | -  | External heartbeat LED | GPIO output, active high. Toggles in sync with LD2. Confirm this is free on your specific board/silkscreen before wiring - chosen to avoid I2C1 (PB6/7 or PB8/9), SPI1 (PA5/6/7 - also LD2's known solder-bridge conflict), USART3 (PB10/11 or PC10/11), and the LSE crystal (PC14/15). |
| PA2 | AF7 | USART2_TX | Routed to ST-LINK VCP (no external wiring needed). Debug console, 115200 8N1. |
| PA3 | AF7 | USART2_RX | Routed to ST-LINK VCP (no external wiring needed). Debug console, 115200 8N1. |
| PC4 | AF7 | USART1_TX | To GPS module (NEO-8M) RX. 9600 8N1. |
| PC5 | AF7 | USART1_RX | To GPS module (NEO-8M) TX. 9600 8N1. |
