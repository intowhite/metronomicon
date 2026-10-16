# Metronomicon for Disting NT

![Metronomicon — metronome, polyrhythms and rhythm patterns for the Disting NT](docs/banner.png)

Metronomicon is a sample-accurate metronome plug-in for the [Expert Sleepers Disting NT](https://www.expert-sleepers.co.uk/distingNT.html)
that follows **MIDI clock, CV clock or its own tempo**, with a **polyrhythm generator**,
**59 rhythm patterns**, **11 time signatures**, CV outputs for every layer and a
**MIDI drum output**.

Built for playing and recording against a sequencer (e.g. Squarp Hermod+) without relying
on quantisation: patch the clock in, press play, hear clicks that sit exactly on the grid.

## Features

**Clock & transport**
- MIDI clock (24 PPQN) with Start / Stop / Continue / Song Position Pointer, or clock-only
- CV clock at 24, 4, 2 or 1 PPQN, or one pulse per bar
- CV transport input: RESET (default), RUN gate, or none
- Internal tempo (20–300 BPM) with tap tempo: right-encoder press, turning the Tap parameter, a CV input, or a mapped gate/MIDI note
- As master: play/stop on the left encoder, a mappable Play parameter, a RUN gate or a toggle trigger;
  MIDI clock + Start/Stop out and a Run gate output
- Clicks land on the exact sample of a CV edge; subdivisions between clock pulses are interpolated and
  re-anchored on every pulse, so there's no drift and nothing keeps running on a stale tempo
- Clock-loss detection and clear status: stopped / waiting / acquiring / running / clock lost

**Metronome**
- 4/4, 3/4, 6/8, 2/4, 12/8, 2/2, 5/4, 7/8, 9/8, 5/8, 7/4 with correct beat grouping
  (6/8 = two dotted quarters, 7/8 = 2+2+3…)
- Click on beats, 1/4, 1/8 or 1/16 with distinct downbeat / beat / subdivision sounds and levels
- Time signature changes land on the next bar line

**Count-in & recording**
- Count-in of 1–4 bars with its own count sound; optionally sent as MIDI notes, with clock waiting for bar 1 or running through the count
- Record arming for samplers and loopers: *Rec out* trigger or gate plus a MIDI note, exactly on bar lines
- Manual (open-ended) takes end on the bar line after you stop or disarm; or fixed 1/2/4/8/16-bar takes;
  arm while playing to punch in on the next bar

**Polyrhythm**
- Any X:Y from 1–16 against Y quarter notes, plus presets (3:2, 2:3, 3:4, 4:3, 6:4, 5:4, 4:5, 5:3, 7:4, 3:8)
- Accent on the cycle start; its own clock output (1, 4 or 24 per pulse) and reset output

**Rhythm patterns** — 59 patterns in 9 groups
- Afro-Cuban/African (tresillo, son & rumba clave 3-2 / 2-3, cinquillo, habanera, cascara, bells),
  Odd meter (5/8, 7/8, 9/8, 11/8 groupings), Swing/shuffle (adjustable 50–75 % swing),
  Jazz/practice, Indian tala, Middle Eastern, Brazilian, Caribbean, Funk/electronic
- Accents, normal hits and ghost notes; trigger, accent, ghost, velocity-CV and reset outputs

**Outputs**
- Each layer: own audio bus (add/replace), own level, own on/off; audio mutes never stop CV
- CV: base clock + reset, poly clock + reset, pattern trig / accent / ghost / velocity / reset
- Reset outputs lead their clocks by 1 ms for modules that need reset-before-clock
- MIDI drum output to USB / Breakout / Select Bus / Internal: per-voice notes (GM defaults),
  velocity from levels, choose which layers send
- Two click sounds: Classic (woody) and Soft (lower, drier)

Every layer keeps its own cycle — a 3:2 polyrhythm or a 6-quarter bell pattern runs against 3/4
without being reset by the bar — and all of them realign on MIDI Start or a reset.

## Install

1. Download `metronomicon.o` from [Releases](../../releases) (or build it, below).
2. Copy it to `/programs/plug-ins/` on the Disting NT's SD card.
3. Add the **Metronomicon** algorithm (Utility).

Requires firmware **1.18** or newer (plug-in API v13).

## Quick start

| Source | Patch | Set |
|---|---|---|
| CV (Hermod+) | CLOCK OUT (24 PPQN) → input 1, RESET OUT → input 2 | Clock source CV, CV clock res 24 PPQN, CV transport RESET (or RUN if your sequencer sends a run gate) |
| MIDI | sequencer MIDI out → NT MIDI in (clock + transport) | Clock source MIDI |
| Internal | — | Clock source Internal, set BPM or tap the right encoder, press the left encoder to play/stop |

Full parameter reference, timing details and pattern notation: **[docs/MANUAL.md](docs/MANUAL.md)**.

## Build

Needs the [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
(`arm-none-eabi`) and the Disting NT API at the tag matching your firmware
(firmware 1.18 → [`v1.18.0`](https://github.com/expertsleepersltd/distingNT_API/archive/refs/tags/v1.18.0.zip)).

```sh
# macOS / Linux
git clone --branch v1.18.0 https://github.com/expertsleepersltd/distingNT_API.git
make NT_API=distingNT_API          # -> plugins/metronomicon.o
make test                          # host test suite (any C++11 compiler)
```

```powershell
# Windows (no make needed): unzip the API next to this folder, then
.\build.ps1
```

> A plug-in built against a newer API than the module's firmware shows **failed** on the
> plug-ins page. Build against the API tag that matches your firmware.

Pushing a tag `v*` runs the GitHub Actions workflow, which builds `metronomicon.o`, runs the
tests and attaches the object to a release.

## Project layout

```
src/metronome_engine.h   timing + sound engine (platform-independent, header-only, no allocation)
src/patterns.h           pattern table (generated)
src/metronome.cpp        Disting NT glue: parameters, buses, MIDI, display, custom UI
tests/test_engine.cpp    200 host-side checks with simulated CV/MIDI clocks
tools/gen_patterns.py    readable pattern list -> src/patterns.h
docs/MANUAL.md           full manual
docs/banner.svg / .png   README banner (SVG source, PNG for display)
```

Patterns are written as strings (`X` accent, `x` hit, `g` ghost, `.` rest) in
`tools/gen_patterns.py`; run it to regenerate `src/patterns.h`.

## License

MIT — see [LICENSE](LICENSE).
