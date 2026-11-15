# Barebones ZMK PMW3610 test: findings

This repository is a minimal, single-sensor ZMK shield for testing a PMW3610
trackball with a nice!nano (nRF52840). It has no real key matrix: the only
useful input is relative X/Y motion from the sensor.

## Current status

**Working as of commit `efe1850` (`dont use half duplex`).** The firmware
builds, the PMW3610 initializes, and pointer motion works properly. This is a
hardware-tested result, not just a successful compile.

The working configuration is:

| Signal | nice!nano / nRF52840 pin | Configuration |
| --- | --- | --- |
| SCLK | P0.29 | `SPIM_SCK` |
| SDIO | P0.31 | both `SPIM_MOSI` and `SPIM_MISO` |
| nCS | P0.02 | active low |
| MOTION | P1.15 | active low, pull-up |
| nRESET | P1.13 | active low |

The sensor runs at 2 MHz and 600 CPI. A `zmk,input-listener` forwards its
relative X/Y events into ZMK's pointing/HID path. USB is enabled for mouse
output, and the `zmk-usb-logging` snippet provides a USB CDC ACM console for
debug logs.

## What the final configuration taught us

### The PMW3610 driver is in Zephyr, not ZMK itself

At the versions inspected locally, ZMK consumes Zephyr's upstream PMW3610
input driver. The relevant files are:

- `zephyr/drivers/input/input_pmw3610.c`
- `zephyr/drivers/input/Kconfig.pmw3610`
- `zephyr/dts/bindings/input/pixart,pmw3610.yaml`

Consequently, the correct interface is:

- compatible: `pixart,pmw3610`
- Kconfig: `CONFIG_INPUT_PMW3610=y`
- GPIO properties: `motion-gpios` and optional `reset-gpios`
- resolution: `res-cpi`
- axes: `zephyr,axis-x` and `zephyr,axis-y`

The older external `badjeff/zmk-pmw3610-driver` interface used in the initial
commit is different (`pixart,pmw3610-alt`, `CONFIG_PMW3610_ALT`, `irq-gpios`,
`cpi`, `evt-type`, `x-input-code`, and `y-input-code`). The two schemas are not
interchangeable.

### One SDIO wire does not mean enabling Zephyr `SPI_HALF_DUPLEX`

The PMW3610 has one bidirectional SDIO signal. On nRF52, the working setup maps
both SPIM MOSI and MISO to the same physical pin, P0.31. It deliberately does
**not** add this devicetree property:

```dts
duplex = <SPI_HALF_DUPLEX>;
```

Zephyr's Nordic SPIM driver (`spi_nrfx_spim.c`) explicitly rejects an SPI
configuration containing `SPI_HALF_DUPLEX` with `-ENOTSUP`. The PMW3610 driver
constructs its SPI operation as master, 8-bit, MSB-first, mode 3; it does not
request that generic flag. Removing the property while retaining the shared
MOSI/MISO pin mapping is the change that produced the fully working state.

### A keyless ZMK shield still needs a valid keymap shape

The original zero-row, zero-column mock scanner and empty `bindings = <>` did
not provide a useful/valid keymap shape. The working skeleton uses a 1x1 mock
scanner with no generated events and one inert `&none` binding. It satisfies
ZMK's keymap structure without producing keyboard input.

### Logging requires both configuration and a transport

`CONFIG_INPUT_LOG_LEVEL_DBG=y` enables detailed input-driver messages, but a
host-visible logging backend is also necessary. The final build combines:

- `CONFIG_ZMK_USB=y` for USB/HID support;
- `snippet: zmk-usb-logging` in `build.yaml`, which enables
  `CONFIG_ZMK_USB_LOGGING` and creates a CDC ACM console; and
- `CONFIG_LOG_PROCESS_THREAD_STARTUP_DELAY_MS=8000`, which delays log
  processing long enough to attach a terminal after boot.

The startup delay does not initialize the sensor later; it only delays log
processing.

## Commit-by-commit record

### `b904558` — `init` — did not work

The first attempt established the shield, pin assignments, listener, build
matrix, and an external-driver manifest entry. It targeted
`badjeff/zmk-pmw3610-driver` and its `-alt` API.

Problems found:

- `.github/workflows/build.yml` had `on:` and `jobs:` indented beneath the
  document root, so it was not a valid GitHub Actions workflow structure.
- There was no `zephyr/module.yml`, so the repository did not advertise its
  custom `boards/` directory as a Zephyr board root.
- The zero-position mock scanner was paired with an empty keymap.
- Debug logging was enabled, but no USB logging transport was selected.

The reset line was implemented as an always-high GPIO hog rather than through
the driver. That matched the experimental external-driver setup, but it is not
the reset interface used by Zephyr's driver.

### `a77431a` — `forgot module.yml` — build discovery fix, not yet working

This commit fixed the workflow indentation and added:

```yaml
build:
  settings:
    board_root: .
```

That makes the shield under `boards/shields/pmw3610_test` discoverable when the
repository is loaded as a Zephyr module. It fixed repository/build plumbing;
it did not address the driver mismatch or empty keymap.

### `4808830` — `misconfigs` — correct upstream driver, still not working

This was the major driver migration:

- removed the Badjeff remote and module from `west.yml`;
- changed `pixart,pmw3610-alt` to `pixart,pmw3610`;
- changed `CONFIG_PMW3610_ALT` to `CONFIG_INPUT_PMW3610`;
- replaced the alternate driver's devicetree properties with the properties
  from Zephyr's binding; and
- moved reset control into the PMW3610 node as an active-low `reset-gpios`.

Those changes agree with the inspected Zephyr binding and driver. However,
this commit also added `duplex = <SPI_HALF_DUPLEX>`, which the nRF SPIM driver
does not support, and it retained the invalid/empty keymap arrangement.

### `ddd58d7` — `bindings typo` — keymap fixed, sensor still not working

The mock scanner became 1x1 and the keymap gained a matching `&none` binding,
with `<behaviors.dtsi>` included so that behavior is defined. This repaired the
keymap side without creating any real key events.

The unsupported `SPI_HALF_DUPLEX` flag remained, so the PMW3610 SPI path was
still not operational.

### `9474e08` — `enable logging and USB` — diagnostics/output improved

This commit enabled USB and made input debugging explicit. It also set an
eight-second log-processing startup delay. That supplied USB mouse output and
prepared useful driver diagnostics, but detailed logs still needed a console
transport and the SPI half-duplex problem remained.

### `1f3edc0` — `forgot logging snippet` — USB log console added

Adding `snippet: zmk-usb-logging` to the build enabled ZMK USB logging and
provided the CDC ACM console. This made the debug configuration observable on
the host. The sensor still could not work while the Nordic SPIM driver was
being asked for unsupported generic half-duplex operation.

### `efe1850` — `dont use half duplex` — **working**

This commit removed the SPI binding include and the `duplex` property. It kept
MOSI and MISO assigned to the PMW3610's single P0.31 SDIO wire. With that
combination the PMW3610 works properly.

## Build

The GitHub Actions build matrix is intentionally minimal:

```yaml
include:
  - board: nice_nano//zmk
    shield: pmw3610_test
    snippet: zmk-usb-logging
```

Pushing the repository triggers ZMK's reusable user-config workflow. Locally,
it is the equivalent of building the `nice_nano//zmk` board with the
`pmw3610_test` shield and `zmk-usb-logging` snippet.

## Version and reproducibility notes

The investigation compared this repository with the local checkout at
`C:\Users\User\OneDrive\Documents\zmk`:

- ZMK: `9ebbeff0a8b69a42f14aec022cdf16c7a107b9e0`
- Zephyr: `10ba6d0cb38bc3d258775d27982f707599320085`

The manifest currently tracks ZMK `main`, and the reusable GitHub workflow is
also referenced at `@main`. Therefore a future build may consume different
ZMK/Zephyr code than the working build documented here. Pin both references to
known revisions if exact reproducibility matters.

The local west workspace was also incomplete at investigation time: `west
list` reported that Zephyr's `manifest-rev` import data was unavailable. Source
comparison was possible, but a clean local rebuild was not. The definitive
runtime result in this record is the hardware-confirmed working latest commit.
