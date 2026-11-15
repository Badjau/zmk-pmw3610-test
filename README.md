# zmk-pmw3610-test

Barebones ZMK configuration for a PMW3610 trackball on the right-hand
nice!nano. This test connects directly to the host over USB, with Bluetooth
and split transport disabled. There are no physical keys; an inert mock
keymap satisfies ZMK's keymap requirements.

The baseline sensor configuration was hardware-tested (see FINDINGS.md).
The new diagnostic configuration still needs a build and hardware validation.

Wiring and orientation match the `right-pmw3610-trackball` branch at
[de4c1a4](https://github.com/Badjau/zmk-mk3-handwire/blob/de4c1a498455e9e96e9c900132bb889c6f21e411/boards/shields/mk3/mk3_trackball_right.dtsi)
(`Correct trackball vertical cursor direction`), fetched on 2026-11-15:

| PMW3610 signal | nRF52840 GPIO |
| --- | --- |
| SCLK | P0.29 |
| SDIO (shared MOSI/MISO) | P0.02 |
| nCS | P0.31 |
| MOTION | P0.20, active low with pull-up |
| nRESET | P0.22, active low |

Connect ground and the supply appropriate for your sensor breakout. These
sensor pins are free of the reference right-hand matrix and wake wiring.
Sensor X maps to host Y with inversion; sensor Y maps to host X, matching
the reference mounting. No left controller or dongle is needed. The 2 MHz
SPI limit and 600 CPI also match the reference. This test uses the upstream
driver already available in its ZMK dependency; it does not copy the branch's
Zephyr 3.5 driver backport or split forwarding code.

Build the existing `pmw3610_test` target in GitHub Actions and flash its UF2
to the right controller. The `zmk-usb-logging` snippet supplies the USB serial
console and USB mouse output remains enabled. Open the new COM port in a
serial terminal (115200 baud) after flashing. Logs begin processing after
eight seconds; reset with the terminal attached if you miss startup output.

Diagnostic output includes:

- `PMW3610 ready`: the upstream driver's initialization passed its product ID
  (`0x3e`), observation-register and motion-interrupt checks.
- `PMW3610 NOT READY`: inspect adjacent `input_pmw3610` and SPI errors.
  `Invalid product id` indicates the expected ID was not read; check power,
  ground, CS, SDIO, SCLK and reset wiring. SPI transfer completion alone does
  not prove a sensor is connected.
- Every input event logs its type, axis code, delta and synchronization flag.
  Rolling the ball should increase the motion-report count and move the mouse.
- A heartbeat every five seconds reports readiness, the raw P0.20 MOTION
  level (0=asserted), observed motion interrupts, motion reports and observer
  registration status (0=attached). The observer adds a GPIO callback without
  changing the driver's interrupt setup. Counts begin when diagnostics starts,
  after driver initialization. If IRQs increase but reports stay zero, the
  fault is downstream of the GPIO interrupt: driver work, SPI burst reads or
  motion decoding. If MOTION stays low with no IRQs, the edge-triggered driver
  may have missed an already-asserted line. If it stays high with no IRQs while
  rolling, investigate the IRQ signal and sensor tracking state. Occasional
  level samples can miss short pulses; use the IRQ counter alongside them.
  Readiness is the boot initialization result, not a fresh SPI connectivity
  probe. A stationary ball normally produces no events. If readiness passes
  but rolling produces none, check MOTION, optics and ball placement.

SPI and input debug logs are enabled, with a 16 KiB log buffer. Fast motion
can still overwhelm serial logging; dropped messages are possible. Diagnostic
code observes driver readiness and input events without issuing competing raw
SPI transactions.

See [FINDINGS.md](FINDINGS.md) for the wiring, final configuration, complete
commit-by-commit investigation, failed approaches, source-level explanation,
and reproducibility notes.
