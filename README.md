# ESPHome component for the DYP A0221AT ultrasonic distance sensor

[![ESPHome external component](https://img.shields.io/badge/ESPHome-external--component-blue)](https://esphome.io)
[![UART](https://img.shields.io/badge/UART-9600_8N1-informational)](#how-it-works)
[![License: MIT](https://img.shields.io/badge/license-MIT-lightgrey)](#acknowledgements--history)
[![Original project](https://img.shields.io/badge/fork%20of-flamedodger%2Fa0221at-orange)](https://github.com/flamedodger/a0221at)

External [ESPHome](https://esphome.io) component for the **DYP A0221AT** ultrasonic distance sensor — the **UART-controlled** variant of the A02 family (sold as DYP-A02YYT(W)).

> [!IMPORTANT]
> **Which variant do you have?**
>
> | Variant | Behaviour | Use |
> |---|---|---|
> | **A02YYUW** (auto output) | streams a measurement continuously on its own | the **built-in** [`a02yyuw` platform](https://github.com/esphome/esphome/tree/dev/esphome/components/a02yyuw) — you don't need this repository |
> | **A0221AT** (controlled output) | stays silent until you send a trigger byte on its `RX` pin, then answers with exactly one frame | **this component** |
>
> Both variants share the same 4-byte frame format, so a module of the other kind wired by mistake will still be parsed — but for auto-output hardware the built-in platform is the right tool.

## Acknowledgements & history

This project is a **continuation of the original `a0221at` ESPHome integration by [@flamedodger](https://github.com/flamedodger/a0221at)** (v1.0.1, September 2025, MIT licensed).

Flamedodger's component was the first to make the A0221AT usable with ESPHome at all: at a time when ESPHome only supported the auto-output variant (`a02yyuw`), the original project worked out the manual-UART trigger approach, got real sensor frames parsed and published to Home Assistant, and documented the whole tank-level use case — including hardware details like the sensor running reliably at 3.3 V on ESP32 despite its 5 V rating. It was published under MIT, which is what makes this rewrite possible.

This repository keeps that architecture (trigger on `update()`, parse on `loop()`, millimetre frames) and hardens it against the manufacturer's official interface specification. Nothing here would exist without the original.

**What v1 got right (and v2 keeps):**

- the trigger-based polling model for the manual-UART variant, when no built-in platform fitted
- the tank-level / fill-percentage use case, with Home Assistant telemetry
- the practical hardware notes: 3.3 V operation on ESP32, "avoid the PWM version, avoid the auto-UART version"
- MIT licensing

**What v2 changes:**

| | v1.x — original by [@flamedodger](https://github.com/flamedodger/a0221at) | v2.0 — this rewrite |
|---|---|---|
| Trigger | string `"R\r\n"` (3 bytes → up to 3 triggers per poll) | single `0xFF` byte, per the datasheet |
| `update()` | `delay(50)` — blocked the whole ESP main loop every cycle | non-blocking: trigger in `update()`, parsing in `loop()` |
| Frame parsing | 3 of 4 bytes, no checksum | 4-byte frame, header resync + checksum validation |
| UART access | hand-rolled `set_uart_parent()` | proper `uart::UARTDevice` inheritance |
| Config schema | hand-written allow-list of 6 keys — `filters:` and `internal:` were rejected with `Extra keys not allowed` | built on `sensor.sensor_schema()`, the same base as built-ins → the full standard option set, wired entirely by the framework (`sensor.new_sensor()`) |
| UART validation | none (a wrong baud rate failed silently) | `uart.final_validate_device_schema()` |
| Output unit | cm | mm (protocol-native) |
| `dump_config()` | missing | `LOG_SENSOR` + boot-time interval check |
| Code layout | `const.py` + `uart_handler.py` modules | consolidated into one component pair (`.h` / `.cpp`) |

## Features

- **Non-blocking by design** — the trigger is sent in `update()`, the frame is received and parsed in `loop()`; no `delay()`, the main loop is never stalled
- **Spec-compliant trigger** — a single `0xFF` byte (the datasheet accepts *a falling-edge pulse or any one serial byte*), instead of the old 3-byte string that fired up to three triggers per poll
- **Robust frame parsing** — resynchronisation on the `0xFF` header, full 4-byte frames, **checksum validation** before anything is published
- **Protocol-native output** — distance in **millimetres**, with `device_class: distance` and `state_class: measurement` (long-term statistics work in Home Assistant out of the box)
- **Full standard `sensor:` schema** — `filters`, `internal`, `icon`, `entity_category`, `on_value`, `on_value_range`, `expire_after`, … everything a built-in platform supports
- **Compile-time UART validation** — 9600 8N1 enforced and `tx_pin` required (this variant cannot work without a trigger line), with clear error messages instead of silent failure
- **Boot-time sanity checks** — warns if `update_interval` is shorter than the datasheet requires, and warns if the module stops answering triggers

## Installation

```yaml
external_components:
  - source: github://LePouet/a0221at@main
    components: [a0221at]
    # refresh: 0s   # uncomment while hacking on the component
```

For a local copy of this repository, use `source: components` (path relative to your YAML file).

## Configuration

### Minimal

```yaml
uart:
  tx_pin: GPIO17        # → sensor RX  (trigger input, required)
  rx_pin: GPIO16        # ← sensor TX  (data output)
  baud_rate: 9600

sensor:
  - platform: a0221at
    name: "Distance"
```

The sensor publishes **millimetres** and polls every second by default (`update_interval: 1s`). The interval must be **greater than 70 ms**, the datasheet minimum between two triggers.

### Centimetres (the unit v1 published)

```yaml
sensor:
  - platform: a0221at
    name: "Distance"
    unit_of_measurement: "cm"
    accuracy_decimals: 1
    filters:
      - multiply: 0.1        # mm → cm
```

### Tank level — the original project's use case, now with on-device maths

`internal` and `filters` (and every other standard option) now work:

```yaml
sensor:
  # Hidden, heavily filtered raw distance in mm
  - platform: a0221at
    id: distance_raw
    internal: true                   # not exposed to Home Assistant
    update_interval: 250ms
    filters:
      - median: 5
      - sliding_window_moving_average:
          window_size: 10
          send_every: 2
    on_value:
      then:
        - sensor.template.publish:
            id: tank_level
            # 2 m deep tank, empty when the sensor reads 2 m
            state: !lambda "return clamp<float>((2000.0f - x) / 2000.0f * 100.0f, 0.0f, 100.0f);"

  # Exposed to Home Assistant, recomputed on every new filtered reading
  - platform: template
    id: tank_level
    name: "Tank level"
    unit_of_measurement: "%"
```

<details>
<summary><strong>Migration from v1.x — breaking changes</strong></summary>

The differences are summarised in the table under [Acknowledgements & history](#acknowledgements--history). In practice, migrating means:

1. The published unit changed from **cm to mm**. To keep the old behaviour add:
   ```yaml
   unit_of_measurement: "cm"
   accuracy_decimals: 1
   filters:
     - multiply: 0.1
   ```
2. The `uart:` bus **must** define a `tx_pin` (validation error otherwise) — the controlled variant cannot work without it.
3. Your existing YAML keys (`name`, `update_interval`, `accuracy_decimals`, `unit_of_measurement`, `force_update`, `disabled_by_default`) keep working unchanged.

</details>

## How it works

Protocol summary (source: the [manufacturer's interface specification](https://www.dypcn.com/uploads/A02-Output-Interfaces.pdf)):

- UART **9600 baud, 8 data bits, no parity, 1 stop bit**, 3.3 V TTL logic
- A measurement starts when the module's `RX` sees **a falling-edge pulse or any single serial byte**; this component sends one `0xFF` byte
- **More than 70 ms** must pass between two triggers
- After each trigger the module replies with exactly one 4-byte frame:

```
ESP ── trigger: 0xFF ──────────────────────►  module RX
ESP ◄── [0xFF][DATA_H][DATA_L][SUM] ────────  module TX

distance (mm) = DATA_H * 256 + DATA_L
SUM           = (0xFF + DATA_H + DATA_L) & 0xFF
```

| Byte | Field | Meaning |
|---|---|---|
| 0 | `0xFF` | frame header |
| 1 | `DATA_H` | distance, high byte |
| 2 | `DATA_L` | distance, low byte |
| 3 | `SUM` | checksum: `(0xFF + DATA_H + DATA_L) & 0xFF` |

- Distances of **≤ 30 mm** (blind zone, ≈ 3 cm) are meaningless echoes; the component logs `Invalid data read from sensor` instead of publishing them

What the component does with this:

1. `update()` — warns if the previous trigger was never answered, drops stale bytes, sends the `0xFF` trigger
2. `loop()` — collects bytes, resynchronises on the `0xFF` header, validates the checksum of every complete frame
3. On a valid frame — publishes the distance in mm and clears the pending-response flag

## Wiring

![A0221AT sensor module](https://www.espboards.dev/img/GZGsogluph-1000.avif)
*Photo from the original project's documentation.*

| Module pin | Connects to | Note |
|---|---|---|
| `5V` (VCC) | 5 V | supply pin — the original project confirmed the module **also runs reliably at 3.3 V on ESP32** (short range up to ~350 cm) |
| `GND` | `GND` | common ground with the ESP |
| `RX` | any TX-capable GPIO | **required** — this is the trigger line |
| `TX` | any RX-capable GPIO | data line |
| other pins (`RTS`/`ADC`) | — | leave unconnected for UART use |

The UART lines are 3.3 V TTL and connect directly to ESP32/ESP8266/RP2040 GPIOs; no level shifter is needed on native UARTs. Watch out for the other A02-family variants: the **PWM version** outputs pulse-width, not serial, and the **auto-UART version** transmits continuously — neither works with this component (a note inherited from the original project).

## Troubleshooting

| Log message | Cause | Fix |
|---|---|---|
| `Sensor did not respond to the last trigger, check wiring and baud rate` | no valid frame since the last trigger | `TX`/`RX` swapped? baud 9600? module powered? — the warning repeats until the first valid frame arrives |
| `Checksum failed: XX != XX` | garbled frame (noise, loose wire, wrong baud) | prefer hardware UART pins, re-check wiring and ground |
| `Invalid data read from sensor: X mm` | object inside the ≈3 cm blind zone, or no reflection at all | nothing to fix — the sensor cannot see that close |
| `update_interval ... too short, the sensor needs more than 70 ms between triggers` | polling faster than the datasheet allows | raise `update_interval` (≥ 100 ms recommended) |
| Compile error about a missing `tx_pin` | the `uart:` bus has no `tx_pin` | this variant must be triggered; add `tx_pin:` to the `uart:` bus |

<details>
<summary><strong>Technical notes</strong></summary>

- Written against ESPHome 2026.10.x; it also builds on older releases thanks to the officially documented [`ESPHOME_LOG_TAG` fallback](https://developers.esphome.io/architecture/logging/best_practices/).
- The `dev`-branch built-in `a02yyuw` component uses the new `uart::UARTDevice::read_available()` helper, which ships with ESPHome 2026.11. This external component uses the classic `available()` / `read_byte()` loop so it compiles on today's stable release — a comment in `a0221at.cpp` shows the 2026.11+ form.
- The A02 frame format is shared across the family, so this parser also tolerates an auto-output module wired by mistake — but for that hardware, use the built-in `a02yyuw` platform.
- Works on any board supported by the ESPHome `uart` core (ESP32, ESP8266, RP2040, …). Prefer hardware UART pins; software UART at 9600 baud is generally fine.
- **License:** MIT, continuing the original project's license — see `LICENSE`.

Repository layout:

```
components/a0221at/
├── __init__.py      # domain + code owners
├── sensor.py        # config schema, UART validation, codegen
├── a0221at.h        # component class
└── a0221at.cpp      # trigger + frame parser implementation
```

</details>

## References

- [flamedodger/a0221at](https://github.com/flamedodger/a0221at) — **the original component** (v1.0.1, MIT), first to bring the A0221AT to ESPHome; this project builds on it
- [DYP — A02 output interface specification (PDF)](https://www.dypcn.com/uploads/A02-Output-Interfaces.pdf) — trigger and frame format
- [`esphome/components/a02yyuw`](https://github.com/esphome/esphome/tree/dev/esphome/components/a02yyuw) — the built-in component for the auto-output variant, used here as the quality reference
- [RobTillaart/A02YYUW](https://github.com/RobTillaart/A02YYUW) — independent Arduino implementation, used to cross-check the protocol
