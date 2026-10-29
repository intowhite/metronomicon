# Changelog

## 1.10.1
The 1.10 download contained the 1.9 build by mistake. This release is 1.10 as intended.

- **Metro toggle in** (Metronome page): a CV input where each trigger turns the click
  (*Metro audio*) on or off. The parameter flips with it, so the screen and saved presets show
  the real state. Bounce and noise are ignored.
  - With the internal clock, the **count-in still plays** while the click is off: record with
    the click, tap to silence it, and the next take still counts you in.
  - Works with every clock source. When following MIDI or CV, it silences the click while
    sync, CV outputs and MIDI keep running.
  - For play/stop from a trigger, set *CV transport* to **TOGGLE** (internal clock).
- The **version** is shown on the algorithm's screen (small text above the BPM) and in its
  description, so you can see which build is loaded.
- The README now contains the full manual.

**Upgrading:** replace `metronomicon.o` on the SD card and power-cycle. Presets from 1.9 load
unchanged.

## 1.10
- **Metro toggle in** (Metronome page): each trigger on this CV input turns *Metro audio*
  on/off. The count-in still plays while it's off, so you can silence the click after a take
  and still be counted in on the next one. (Play/stop from a trigger: *CV transport* =
  TOGGLE, Internal clock.)

## 1.9
- Removed *Count-in out*.
- *Clock in count* is now **Count-in clock**: Wait (default) / Run.
- *Count-in MIDI* is now **MIDI count out**, on the Output page with the other MIDI settings.
- **MIDI out** is the master switch for all MIDI (notes, clock, count, rec note). Turning on
  *MIDI clock out* or a *Rec MIDI note* switches MIDI out on and sets *MIDI layers* to the
  new **None** option, so you get clock without drum notes.
- *Play* also appears on the Clock & Transport page when the clock source is Internal.
- *MIDI clock out* also appears on the Count-in & Rec page; while it's on, *MIDI out dest*
  appears there too.

## 1.8
- **Transport** for the internal clock: *Play* (always stopped after loading), left-encoder
  press = play/stop, CV transport *TOGGLE* mode (one trigger = play/stop); a RUN gate also
  plays/stops.
- **Count-in**: Off / 1–4 bars with its own count sound (heard even with Metro audio off);
  *Count-in MIDI* sends the count as notes; *Clock in count* runs MIDI/base clock during it.
- **MIDI clock out**: 24 PPQN clock plus Start / Stop when Metronomicon is the master
  (internal or CV clock).
- **Recording**: *Rec arm*, *Rec length* (Manual or 1–16 bars), *Rec out* (Trigger or Gate),
  *Rec MIDI note*. Takes start and end exactly on bar lines; stop while recording is
  quantized to the next bar line; arming while playing punches in on the next bar.
- **Run out** and **Count-in out** gate outputs.

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
