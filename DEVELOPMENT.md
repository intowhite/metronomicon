# Metronomicon — Development notes

## Building

Needs the [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
(`arm-none-eabi`) and the Disting NT API **at the tag that matches the target firmware**
(firmware 1.18 = API v13 = tag
[`v1.18.0`](https://github.com/expertsleepersltd/distingNT_API/archive/refs/tags/v1.18.0.zip)).
A plug-in built against a newer API than the module's firmware shows **failed** on the
plug-ins page, with the API version it was built for.

```sh
# macOS / Linux
git clone --branch v1.18.0 https://github.com/expertsleepersltd/distingNT_API.git
make NT_API=distingNT_API     # -> plugins/metronomicon.o
make test                     # host test suite, any C++11 compiler
```

```powershell
# Windows: unzip the API next to this folder (any nesting), then
.\build.ps1                   # or: powershell -ExecutionPolicy Bypass -File .\build.ps1
```

Pushing a tag `v*` runs `.github/workflows/build.yml`: host tests, plug-in build, and a
release with `metronomicon.o` attached.

## Layout

```
src/metronome_engine.h   timing + sound engine: platform-independent, header-only, no allocation
src/patterns.h           pattern table (generated — don't edit)
src/metronome.cpp        Disting NT glue: parameters, pages, buses, MIDI, display, custom UI
tests/test_engine.cpp    host test suite with simulated CV / MIDI clocks
tools/gen_patterns.py    readable pattern list -> src/patterns.h
docs/                    README banner (SVG source + PNG)
```

## Adding or editing patterns

Patterns are one line each in `tools/gen_patterns.py`:

```python
("Son clave 3-2", 12, "X..x..x...x.x...", 0),
#  name           step  X accent, x hit,      swing
#                 ticks g ghost, . rest       (0/1)
```

Step ticks are at 24 PPQN: 6 = 1/16, 8 = triplet 1/8, 12 = 1/8, 24 = 1/4. Up to 16 steps.
Run `python3 tools/gen_patterns.py` to regenerate `src/patterns.h`; `make test` checks the
table's consistency.

## Presets and parameter order

The NT stores parameters by index. **Only ever append new parameters at the end** of the
enum and `kParameterTemplate` in `metronome.cpp`, or existing presets load with shifted
values. Removing the *last* parameter is safe. The plug-in GUID `Metr` must not change.

## Engine design

- **Timeline:** 24 PPQN ticks. Each clock event (MIDI 0xF8, CV edge, internal tick) anchors a
  span; events of the three layers inside the span are scheduled from the measured tick
  period and fired at their exact sample. The next real clock event flushes anything pending
  and re-anchors, so there's no drift and nothing outruns the clock.
- **Layers** keep independent cycles from the session origin; configuration changes are
  queued and applied at their cycle boundary.
- **Two-pass blocks:** pass 1 walks time (per sample only where a CV input must be scanned,
  otherwise jumping event to event) and records sound / trigger / level events with sample
  offsets; pass 2 renders only active voices and writes outputs as runs between events.
- **Sound:** each voice is two damped resonators plus a short noise transient, normalised
  at init. No samples, no allocation.
- **Transport (Internal):** Stopped → Count-in → Playing; quantized stop while recording;
  recording starts/ends on bar lines from the metronome event stream.
- **MIDI out** is collected per block and sent by the glue (`NT_sendMidiByte` /
  `NT_sendMidi3ByteMessage`).

Imports from the firmware: `NT_globals`, drawing, parameter and MIDI-send functions, and
`NT_updateParameterPages` / `NT_updateParameterDefinition` (API v13). Footprint is roughly
6 KB of SRAM per instance and ~25 KB of code.

## Tests

`make test` runs the host suite at 48 kHz. It covers click intervals and accents for all
clock resolutions and meters, long-run drift (CV, MIDI with jitter, internal clock), tempo
ramps, RUN/RESET/transport coincidence, clock loss, MIDI Start/Stop/Continue/SPP,
polyrhythm and pattern placement and cycle-boundary changes, swing, all CV outputs, MIDI
notes / clock / transport, count-in, recording (manual, fixed, punch-in, quantized stop),
the toggle input, the sound sets and odd block sizes.

What the host tests can't cover — encoders, parameter pages, the display, real MIDI
hardware — needs checking on the module.
