# Technical Notes

[简体中文](TECHNICAL.zh-CN.md)

This document describes the implementation shipped in this repository. The
wire protocol is undocumented by its vendors; details below are observations
and compatibility behavior, not a stable or official API specification.

## System overview

The firmware has two main layers:

1. `src/main.cpp` owns the Core2/CoreS3 display, touch hit testing, page state,
   battery polling, and render loop. `src/main_sticks3.cpp` provides a separate
   Tasks-only entry point for the smaller StickS3 display and two buttons.
   `src/main_stopwatch.cpp` carries the full three-page UI onto the round
   StopWatch display.
2. `src/CodexMicroBle.cpp` owns BLE setup, the HID descriptor, report framing,
   JSON parsing, host request handling, and notifications sent to the host.

The Arduino loop reads an immutable snapshot of BLE-owned state. A FreeRTOS
mutex protects connection and lighting data that can be changed from BLE
callbacks.

```mermaid
sequenceDiagram
    participant User
    participant UI as Core2 UI loop
    participant BLE as BLE HID transport
    participant Host as ChatGPT Desktop

    User->>UI: Touch an Agent or Command key
    UI->>BLE: Build v.oai.hid notification
    BLE->>Host: Fragmented report notifications
    Host->>BLE: Task status or device RPC request
    BLE->>BLE: Parse JSON and update protected state
    UI->>BLE: Read state snapshot
    UI->>UI: Render complete frame to M5Canvas
```

## Build environment

The PlatformIO environment is intentionally small:

| Setting | Value |
| --- | --- |
| Platform | `espressif32@6.13.0` |
| Board | `m5stack-core2`, `m5stack-cores3`, generic `esp32-s3-devkitc-1` for StickS3, or `esp32s3box` configured for StopWatch |
| Framework | Arduino |
| Serial monitor | 115200 baud |
| Upload speed | 1,500,000 baud (Core2), 921,600 baud (ESP32-S3 targets) |
| Display and hardware library | `M5Unified ^0.2.7`; current Git versions of M5Unified/M5GFX/M5PM1/M5IOE1 for StopWatch autodetection |
| JSON library | `ArduinoJson ^6.21.5` |

The current transport relies on the Arduino-ESP32 2.x BLE HID implementation
provided by the selected PlatformIO platform. Upgrading the ESP32 platform can
change BLE field serialization, callback APIs, memory use, or pairing behavior
and must be verified on a clean host pairing.

## BLE HID identity

The device advertises as a generic BLE HID device with these compatibility
values:

| Field | Value |
| --- | --- |
| Device name | `Codex Micro` |
| Manufacturer string | `Work Louder` |
| Vendor ID | `0x303A` |
| Product ID | `0x8360` |
| PnP source | `0x02` |
| HID usage page | Vendor Defined `0xFF00` |
| Application usage | `0x01` |
| Report ID | `6` |
| Input report body | 63 bytes |
| Output report body | 63 bytes |

These names and identifiers are not assigned to this project. They are emitted
only because the host uses them for compatibility detection. Do not reuse them
for an unrelated product or imply that a device is official hardware.

Arduino-ESP32 2.x serializes the PnP fields in a byte order that differs from
the BLE PnP characteristic. The implementation pre-swaps the 16-bit VID, PID,
and release values so macOS observes the intended values.

## HID report descriptor

The descriptor declares one vendor-defined application collection containing
one 63-byte input report and one 63-byte output report under Report ID 6. Every
item uses an 8-bit report size and a logical range of 0 to 255.

On normal BLE HOGP paths, the report ID selects the characteristic and is not
part of the 63-byte characteristic value. The receive path also accepts a
64-byte variant with an explicit leading Report ID 6 for host bridges that
forward the raw report.

## Transport framing

Each report body has this layout:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 1 byte | Message type, currently `2` |
| 1 | 1 byte | Payload length, `0` to `61` |
| 2 | Up to 61 bytes | UTF-8 JSON fragment |
| Remaining | Variable | Zero padding to a 63-byte report body |

Outgoing JSON is terminated with `\n`, divided into chunks of at most 61 bytes,
and sent as fixed-size 63-byte input notifications. A 4 ms delay is inserted
between fragments.

Incoming output reports are appended to a string buffer and parsed with a
4096-byte `DynamicJsonDocument`. `IncompleteInput` retains the buffer for the
next fragment. A new fragment beginning with `{"method"` resets an incomplete
buffer, which allows the receiver to recover after a dropped request. Malformed
JSON clears the buffer and writes a parse error to the serial log.

The transport does not currently include sequence numbers, acknowledgements,
checksums, retransmission, flow-control negotiation, encryption above BLE, or a
maximum accumulated string-buffer guard.

## RPC messages

Messages follow a compact JSON-RPC-like shape. Host requests include `method`,
usually `params`, and an `id`. Responses echo `id` and include either `result`
or `error`. Device events omit `id`.

### Device to host

| Method | Parameters | Purpose |
| --- | --- | --- |
| `v.oai.hid` | `k`: key ID, `act`: action, optional `ag`: Agent index | Key press or release |
| `v.oai.rad` | `a`: normalized angle, `d`: distance | Analog-direction press or release |

Key actions used by the firmware:

- `act = 0`: release
- `act = 1`: press
- `act = 2`: one encoder step

Agent IDs are `AG00` through `AG05`. Default command IDs are `ACT06`, `ACT07`,
`ACT08`, `ACT09`, `ACT10`, and `ACT12`. Encoder IDs are `ENC_CC`, `ENC_CW`, and
`ENC`.

Directional angles are normalized turns rather than radians:

| Direction | Angle | Press distance | Release distance |
| --- | ---: | ---: | ---: |
| Right | `0.00` | `1.0` | `0.0` |
| Down | `0.25` | `1.0` | `0.0` |
| Left | `0.50` | `1.0` | `0.0` |
| Up | `0.75` | `1.0` | `0.0` |

### Host to device

| Method | Behavior |
| --- | --- |
| `sys.version` | Returns `0.1.0-core2`, `0.1.0-cores3`, `0.1.0-sticks3`, or `0.1.0-stopwatch` |
| `device.status` | Returns version, profile, layer, battery, and charging state |
| `v.oai.thstatus` | Updates one or more of the six Agent status lights |
| `v.oai.rgbcfg` | Stores host ambient and key lighting configuration |
| `lights.preview` | Acknowledged; no physical lighting preview is implemented |
| `host.focused_app` | Acknowledged; no local app-specific behavior is implemented |

An unknown method receives error code `-32601` and message `Method not found`.

### Task lighting state

Each `v.oai.thstatus` array element may contain:

| Key | Type | Meaning |
| --- | --- | --- |
| `id` | Integer 0 to 5 | Agent slot |
| `c` | 24-bit integer | RGB color |
| `b` | Float | Brightness multiplier |
| `e` | String | Effect name, including `off` or `breath` |
| `s` | Float | Effect speed supplied by host |

The UI uses the color, brightness, and `breath` effect. Effect speed is stored
but the current breathing animation uses a fixed local timing function.
Ambient and key lighting configuration is stored for protocol compatibility but
is not rendered because Core2 has no equivalent per-key lighting hardware.

## Input mapping

The display uses three pages. Page selection is available from the bottom tabs
and from Core2's A, B, and C touch buttons. CoreS3's digitizer covers only the
320 x 240 panel, so M5Unified never reports those buttons there and the tab bar
is the only page control.

StopWatch uses a dedicated 468 x 468 design coordinate system on its round
CO5300 AMOLED. The six Agent and Command controls use a two-column by three-row
grid inside the circular safe area; Navigate uses a radial direction layout.
The yellow/blue physical buttons (`M5.BtnA`/`M5.BtnB`, GPIO 2/1) move between
pages, while the CST820 touch controller drives all actions.

Valid touch actions, on-screen tab changes, and physical page buttons trigger
a 35 ms vibration pulse at level 128/255. `M5.Power.setVibration()` drives the
StopWatch motor through M5IOE1 PWM1. Pulse shutdown is handled from `loop()`
using wrap-safe elapsed-time arithmetic, so haptics never delay BLE processing
or HID release reports. Touches outside active controls do not vibrate.

The StickS3 entry point has no touch pages. It renders all six Agent states as
a vertical list. Its default mappings advance the highlight with a single
`BtnB` click, move it back with a double-click within 350 ms, activate the
highlighted Agent after a 500 ms hold, emit Enter for a single `BtnA` click,
and emit Right Alt for a double-click. Each gesture can be remapped at runtime.

The standalone configurator communicates over USB CDC at 115200 baud. Commands
and responses are newline-delimited and prefixed with `BUTTONS`:

- `BUTTONS GET` returns the five current action tokens.
- `BUTTONS SET <a-single> <a-double> <b-single> <b-double> <b-hold>` validates,
   saves, and then applies all mappings.
- `BUTTONS RESET` restores the default mappings.

Supported tokens are `none`, `enter`, `right_alt`, `ctrl_shift_d`, `next`,
`previous`, `activate`, `key:<modifier>:<usage>`, and `codex:<ACT key>`.
Mappings are stored in the `stick-buttons` NVS namespace and loaded before the
BLE service starts.

Power timing uses a parallel protocol:

- `POWER GET` returns `POWER CONFIG <dim-seconds> <power-off-seconds>`.
- `POWER SET <dim-seconds> <power-off-seconds>` saves and applies both values.
- `POWER RESET` restores 60-second dimming and 1800-second power-off.

Each timeout accepts `0` (disabled) through 604800 seconds (seven days).
Settings are stored in the `stick-power` NVS namespace. Applying new values
restarts the inactivity timer and restores the display if it was dimmed.

Core2/CoreS3 touch hit areas are fixed for the 320 x 240 landscape orientation;
StopWatch hit areas scale from its 468 x 468 design space. Press and release
events are sent for Agent Keys, Command Keys, directional controls, and the dial
press. Dial rotation controls send one encoder-step action immediately.

The Core2/CoreS3 touch UI does not implement double-click timing or the 500 ms
settings hold locally; ChatGPT Desktop interprets those press/release sequences.
The StickS3 entry point handles its physical-button gestures locally.
`StickS3ButtonController` contains timing, configured actions, and selection
state without Arduino dependencies, so gesture boundaries, remapping,
wraparound, token validation, and `millis()` rollover behavior are covered by
native Unity tests.

## Display pipeline

The UI is rendered in landscape orientation into a 320 x 240, 16-bit
`M5Canvas`. The sprite is allocated after `M5.begin()` so display dimensions and
PSRAM are initialized. Each update performs:

1. Clear the off-screen canvas.
2. Draw the header, active page, status animation, and tabs.
3. Push the complete sprite to the physical display once.

This full-frame double buffering avoids the visible erase-and-redraw flicker of
direct display rendering. If allocation fails, the firmware stops and displays
`Canvas allocation failed` directly on the LCD.

Task status with a `breath` effect is redrawn approximately every 80 ms. Other
screens redraw on input, connection changes, or host state changes.
StopWatch uses the same full-frame pipeline with a 468 x 468 PSRAM-backed
sprite. M5Unified auto-detects `board_M5StopWatch` via CST820, M5PM1, and M5IOE1
on GPIO 47/48, then configures the QSPI CO5300 panel and power rails.
The StickS3 build uses the same double-buffered pipeline at its native 135 x 240
portrait resolution.

## Battery and power

The Core2 battery percentage and charging flag are sampled at startup and every
30 seconds. The percentage is clamped to 0 through 100. If the power API returns
an invalid negative level, the firmware reports 100 percent as a compatibility
fallback.

The standard BLE HID battery characteristic is updated while connected, and
`device.status` returns both the cached percentage and charging state.

StickS3 treats a physical button press or an actual change to any Agent's
lighting state as activity. Its display brightness drops from 120 to 20 after
one inactive minute. Activity immediately restores brightness and restarts both
timeouts. After 30 inactive minutes, the firmware calls M5Unified's hardware
power-off path; the physical power button starts the device again.

## Pairing and security

The BLE stack requests bonding with `ESP_IO_CAP_NONE`, resulting in a "Just
Works" pairing flow without passkey verification. This is convenient for a
keyboard-class accessory but does not authenticate the user through a displayed
or entered code. Pair the device in a trusted physical and radio environment.

The firmware does not contain Wi-Fi credentials, an OpenAI API client, analytics,
or an update service. It sends control events and device status to the connected
BLE host. Host behavior, microphone access, and task data remain responsibilities
of ChatGPT Desktop and the operating system.

## Validation baseline

The current implementation has been checked with:

- A physical M5Stack Core2 connected over Bluetooth
- macOS HID enumeration as VID `0x303A`, PID `0x8360`, usage page `0xFF00`
- ChatGPT Desktop detection and BLE reconnection
- `host.focused_app` and `device.status` request handling
- Touch Agent, Command, navigation, and dial event generation
- Full-screen canvas allocation and display rendering
- PlatformIO release build and device flashing

Validation was last performed on July 16, 2026. A specific ChatGPT Desktop build
number was not recorded, so protocol changes should be tested against the current
app before publishing a firmware release.

## Known limitations

- BLE only; no USB HID transport
- One connected host at a time
- No user-selectable Bluetooth slots
- No conventional keyboard keys or text input
- Digital four-direction input instead of a continuous analog stick
- No physical encoder; rotation is exposed as touch buttons
- No ambient or per-key LEDs
- No extra Work Louder layers or Work Louder configuration support
- No over-the-air updater or rollback mechanism
- No automatic UI label update after host-side command remapping
- Compatibility depends on private host behavior and can regress without notice

## Extending the implementation

When changing the protocol layer:

1. Keep observed wire values separate from inferred semantics in documentation.
2. Preserve the HID descriptor and identity unless a clean macOS re-pair test is
   planned.
3. Bound any new receive buffers and validate fragment lengths before parsing.
4. Avoid blocking BLE callbacks with display work; update protected state and
   render from the main loop.
5. Verify press and release pairs, disconnect recovery, battery reporting, and
   unknown RPC responses on physical hardware.
6. Forget the device on the host after descriptor changes so cached metadata is
   refreshed.

## External reference

The official product behavior is documented by OpenAI at
[Codex Micro](https://learn.chatgpt.com/docs/features/codex-micro). That page
describes the official OpenAI and Work Louder hardware. It is a behavioral
reference only and does not document or authorize this compatibility protocol.
