# Changelog

## 1.7
- Renamed to **Metronomicon** (plug-in ID unchanged, so presets still load); the build
  output is now `metronomicon.o` — delete the old `metronome.o` from the SD card.
- *Tap* parameter: every step to the right is a tap (keep turning; left is ignored).
  Shows "turn right = tap". A mapped gate/note still taps once per press.

## 1.6
- MIDI: *Poly accent note* (first pulse of each X:Y cycle) and *Pat ghost note*.

## 1.5
- Pattern ghost trigger output, *Pat trig mode* (All hits / No ghosts), pattern velocity CV
  output (5 V accent / 3.5 V hit / 1.5 V ghost).
- MIDI drum output: destination, channel, layer selection, per-voice notes (GM defaults),
  velocity from levels, automatic note-offs.

## 1.4
- Master volume defaults to 60 %.
- Tap tempo on the right encoder (internal clock, algorithm display).
- Changing the pattern group selects the group's first pattern.
- *Click sound*: Classic or Soft.

## 1.3
- Internal clock with Run, tap tempo (CV input or parameter).
- CV transport defaults to RESET.
- Pattern accent trigger output.
- 59 patterns in 9 groups; swing; ghost notes.
- Two-pass block processing: roughly 4–7× lower CPU.

## 1.2
- CV clock at 24 / 4 / 2 / 1 PPQN or one per bar.
- 11 time signatures with beat grouping.
- Polyrhythm generator and rhythm pattern generator with audio and CV outputs; base clock and
  reset outputs; sub-tick interpolation between clock pulses.

## 1.1
- First release: MIDI / CV clock following, RUN / RESET / NONE transport, accented clicks,
  1/4–1/16 divisions, 2/4–4/4.
