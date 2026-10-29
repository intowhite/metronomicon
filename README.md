# Metronomicon for Disting NT

![Metronomicon — metronome, polyrhythms and rhythm patterns for the Disting NT](docs/banner.png)

Metronomicon is a sample-accurate metronome plug-in for the
[Expert Sleepers Disting NT](https://www.expert-sleepers.co.uk/distingNT.html). It follows
**MIDI clock, CV clock or its own tempo**, counts you in, arms your sampler or looper on the
bar line, and adds a **polyrhythm layer**, **59 rhythm patterns** and **MIDI drum output**,
with CV outputs for everything.

Built for playing and recording against a sequencer (e.g. Squarp Hermod+) without relying on
quantisation: patch the clock in, press play, hear clicks that sit exactly on the grid.

**Contents:** [Features](#features) · [Install](#install) · [Quick start](#quick-start) ·
[Manual](#manual) · [Build](#build) · [Project layout](#project-layout) · [License](#license)

---

## Features

**Clock & transport**
- Follows MIDI clock (24 PPQN) with Start / Stop / Continue, or clock-only
- Follows CV clock at 24, 4, 2 or 1 PPQN, or one pulse per bar
- Or runs on its own tempo (20–300 BPM) with tap tempo and play/stop on the encoder, a
  mappable parameter, a gate or a trigger
- Clicks land on the exact sample of a CV edge; subdivisions between clock pulses are
  interpolated and re-anchored on every pulse, so there's no drift and nothing keeps running
  on a stale tempo
- Clear status: stopped / waiting / acquiring / running / clock lost / count-in

**Metronome**
- 11 time signatures with correct beat grouping: 4/4, 3/4, 6/8, 2/4, 12/8, 2/2, 5/4, 7/8,
  9/8, 5/8, 7/4
- Clicks on beats, 1/4, 1/8 or 1/16, with distinct downbeat / beat / subdivision sounds and levels
- Two click sounds: Classic (woody) and Soft (lower, drier)
- Toggle the click on/off from a trigger input

**Count-in & recording** (when Metronomicon is the master clock)
- Count-in of 1–4 bars on its own sound
- Arms samplers and loopers: a record trigger or gate (plus an optional MIDI note) exactly on
  bar 1; open-ended takes end on the next bar line, or fixed 1–16-bar takes end by themselves
- MIDI clock + Start/Stop out and a Run gate, so other gear follows

**Polyrhythm**
- Any X:Y from 1–16 against Y quarter notes, plus presets (3:2, 2:3, 3:4, 4:3, 6:4, 5:4, 4:5,
  5:3, 7:4, 3:8)
- Accent on the cycle start; its own clock and reset outputs

**Rhythm patterns** — 59 patterns in 9 groups
- Afro-Cuban/African, Odd meter, Swing/shuffle, Jazz/practice, Indian tala, Middle Eastern,
  Brazilian, Caribbean, Funk/electronic
- Accents, normal hits and ghost notes; trigger, accent, ghost, velocity-CV and reset outputs

**Outputs**
- Each layer has its own audio bus, level and on/off; audio mutes never stop CV
- MIDI drum output: every click as a note, per-voice note numbers, velocity from levels
- Every layer keeps its own cycle — a 3:2 polyrhythm or a 6-quarter bell pattern runs against
  3/4 without being reset by the bar — and all of them realign on start or reset

---

## Install

1. Download `metronomicon.o` from [Releases](../../releases).
2. Copy it to `/programs/plug-ins/` on the Disting NT's SD card. If you had an older
   `metronome.o`, delete it, or both will load.
3. Add the **Metronomicon** algorithm (Utility category). Its screen shows the version (e.g. `v1.10`) above the BPM.

Requires Disting NT firmware **1.18** or newer.

---

## Quick start

**Follow a CV sequencer (e.g. Hermod+)**
1. Sequencer CLOCK OUT (24 PPQN) → NT input 1; RESET OUT → NT input 2.
2. *Clock source* = CV, *CV clock res* = 24 PPQN, *CV transport* = RESET (or RUN if your
   sequencer sends a run gate). These are the defaults.
3. Press play on the sequencer.

**Follow MIDI clock**
1. Sequencer or DAW MIDI out → NT MIDI in, sending clock and transport.
2. *Clock source* = MIDI.
3. Press play on the sequencer.

**Be the master clock**
1. *Clock source* = Internal, set *Internal BPM* (or tap the right encoder).
2. Press the **left encoder** to play/stop.
3. Optional: *MIDI clock out* = On and/or patch *Run out* + *Base clock out* to slave other gear.

**Record a loop with a count-in into a sampler or looper**
1. Be the master clock (above). Set *Count-in* = 1 bar.
2. Patch *Rec out* to the device's record input; pick *Rec out mode* = Trigger (press-to-start,
   press-to-stop devices) or Gate (records while high).
3. Turn on *Rec arm* and press play: *1 2 3 4*, recording starts on bar 1.
4. Press stop (or turn *Rec arm* off to keep playing): the take ends on the next bar line.

---

## Manual

### The display

| Area | Shows |
|---|---|
| Top left | Clock source: CV / MIDI / INTERNAL (and "clock only" for MIDI clock-only) |
| Top right | Status (below) |
| Large text | Tempo in BPM; during a count-in, `IN 2`, `IN 1` = bars left |
| Boxes | Beats of the bar; the current beat is filled, the downbeat box is brighter; `?` = bar position unknown |
| Small line | Polyrhythm ratio and current pulse; current pattern (dimmed when that layer's audio is off) |
| Bottom | Time signature, click division, and `REC 2/4`, `REC END`, `ARMED`, `MUTED` or `metro off` |
| Hint | `L:play R:tap` (Internal clock) |
| Version | Small version number (e.g. `v1.10`) above the BPM — the build that's loaded |

**Status:** STOPPED (transport stopped) · WAITING (playing, waiting for the first clock) ·
ACQUIRING (measuring tempo / finding the bar) · RUNNING · CLOCK LOST (clock stopped arriving
while playing) · COUNT-IN.

**Encoders** (Internal clock only, on the Metronomicon display): **left press** = play/stop,
**right press** = tap tempo. With MIDI or CV clock they keep their normal function.

Some parameters appear on two pages; they are the same parameter, so changing one changes
both. Parameters that don't apply to the current setup are greyed out.

### Clock & Transport page

| Parameter | Values (default) | What it does |
|---|---|---|
| Clock source | CV / MIDI / Internal (CV) | Where tempo and position come from. |
| MIDI sync | Transport / Clock only (Transport) | *Transport*: follows Start, Stop, Continue and Song Position. *Clock only*: starts on the first clock (bar 1), stops when clock stops — for sources that send no transport messages. |
| CV clock input | input (Input 1) | The clock input. Rising edge above 1 V, released below 0.5 V. |
| CV clock res | 24 PPQN / 4 PPQN (1/16) / 2 PPQN (1/8) / 1 PPQN (1/4) / 1 per bar (24 PPQN) | How much musical time each clock pulse is worth. With *1 per bar*, the bar length follows the time signature. |
| CV transport in | input (Input 2) | The transport input. |
| CV transport | RUN / RESET / NONE / TOGGLE (RESET) | **CV clock:** RUN = playing while the gate is high, bar 1 on the first clock after it rises; RESET = a trigger makes the next clock bar 1 (a reset up to 3 ms *after* a clock edge counts for that edge), stopping is detected from clock loss; NONE = the first clock pulse is bar 1, clock loss stops; TOGGLE = each trigger starts/stops. **Internal clock:** RUN = gate high plays, low stops; TOGGLE = each trigger plays/stops; RESET = restart at bar 1 while playing; NONE = ignore the input. |
| Internal BPM | 20.0–300.0 (120.0) | Tempo of the internal clock. Tapping writes here. |
| Play | Stop / Play (Stop) | Internal clock transport. Shown here only when the source is Internal (also on the Count-in & Rec page). Always Stop after loading a preset. |
| Tap input | input (None) | Each trigger is a tap (Internal clock). |
| Tap | — | Turn **right** to tap — every step is a tap, so keep turning; left does nothing. Map a button, gate or MIDI note to it for one tap per press. |

**Tap tempo** averages up to the last five taps; a gap longer than 2.5 s starts over. The new
tempo applies immediately and is written to *Internal BPM*, so it's saved with the preset.

### Count-in & Rec page

*Play*, *Count-in* and *Count-in clock* apply to the **Internal clock** (Metronomicon as
master). Recording and *Run out* work with any clock source — with MIDI or CV clock, an armed
take starts on the next bar line, and stopping the sequencer ends it. *MIDI clock out* works
with the internal or CV clock.

| Parameter | Values (default) | What it does |
|---|---|---|
| Play | Stop / Play (Stop) | Start/stop. Starting always begins at bar 1 (after the count-in, if on). |
| Count-in | Off / 1 / 2 / 3 / 4 bars (Off) | Bars of count before bar 1, in the current time signature, on a distinct high "tick" (first beat of each bar accented). Heard even when *Metro audio* is off; only *Mute all* silences it. During the count nothing else runs: no polyrhythm, pattern, CV outputs or MIDI notes. Stopping during the count cancels it. |
| Count-in clock | Wait / Run (Wait) | *Wait*: MIDI clock and the base CV clock start on bar 1. *Run*: they run through the count so receivers lock to the tempo first; MIDI Start is still sent exactly on bar 1. |
| MIDI clock out | Off / On (Off) | Sends 24 PPQN MIDI clock, Start on bar 1 and Stop on stop (a reset while playing sends Stop then Start). Works with the internal clock and re-transmits a CV clock; never sent when following MIDI. Turning it on also turns *MIDI out* on (and sets *MIDI layers* to None if MIDI out was off, so you get clock without drum notes). Also on the Output page. |
| MIDI out dest | (see Output page) | Shown here while *MIDI clock out* is on. |
| Rec arm | Off / On (Off) | Arms the next take. Armed while stopped: recording starts on bar 1. Armed while playing: starts on the next bar line (punch-in). Switch it off while recording to end the take on the next bar line and keep playing. Switches itself off when a take ends. |
| Rec length | Manual / 1 / 2 / 4 / 8 / 16 bars (Manual) | *Manual*: the take runs until you disarm or press stop, and ends on the next bar line — stop is quantized only while recording. *Fixed*: the take ends by itself after exactly that many bars; playback continues so you hear the loop come round. |
| Rec out | output (None) | CV output to the sampler/looper's record input. |
| Rec out mode | Trigger / Gate (Trigger) | *Trigger*: a 5 ms pulse at the start and another at the end — for press-to-start, press-to-stop devices. *Gate*: high for the whole take. |
| Rec MIDI note | Off / 0–127 (Off) | A note sent at the start and end of a take (Trigger) or held for the take (Gate), on *MIDI out channel*. Setting a note turns *MIDI out* on, like *MIDI clock out*. |
| Run out | output (None) | Gate high from bar 1 until stop, with any clock source — a RUN signal for downstream sequencers. |

Typical setups:
- **Device with no tempo** (sampler, looper, recorder): *Count-in* 1 bar, *Rec out* to its
  record input, *Rec length* Manual or the loop length. Arm, press play.
- **Device with a tempo** (drum machine, sequencer, DAW): add *MIDI clock out* and/or *Run out*
  + *Base clock out*; set *Count-in clock* to Run if it needs to lock before bar 1.

### Metronome page

| Parameter | Values (default) | What it does |
|---|---|---|
| Time signature | 4/4, 3/4, 6/8, 2/4, 12/8, 2/2, 5/4, 7/8, 9/8, 5/8, 7/4 (4/4) | Changes take effect at the next bar line. Beat grouping below. |
| Click division | Beats / 1/4 / 1/8 / 1/16 (Beats) | *Beats*: clicks only on the meter's beats. Otherwise beats plus subdivision ticks on that grid. For compound meters (6/8, 9/8, 12/8) use 1/8. |
| Metro audio | Off / On (On) | The metronome's click. Off doesn't affect timing, CV, MIDI or the count-in. |
| Metro toggle in | input (None) | Each trigger flips *Metro audio*. Works with every clock source. Bounce and noise are ignored. |
| Downbeat vol | 0–100 % (100) | Level of beat 1. |
| Beat vol | 0–100 % (65) | Level of the other beats. |
| Subdiv vol | 0–100 % (25) | Level of the subdivision ticks. |
| Metro out | output (Output 1) | Audio output for the click. |
| Metro out mode | Add / Replace (Add) | Mix into the bus, or replace it. Layers sharing a bus are summed. |
| Base clock out | output (None) | Clock pulses from bar 1. |
| Base clock res | 1/4 / 1/8 / 1/16 / 24 PPQN (1/16) | Pulse rate of *Base clock out*. |
| Base reset out | output (None) | Reset trigger. When assigned, the clock outputs trail it by 1 ms so downstream modules see the reset first. |
| Base reset mode | Bar / Start only (Bar) | Reset on every bar line, or only when playback starts. |

**Beat grouping**

| Meter | Beats per bar | Grouping |
|---|---|---|
| 4/4, 3/4, 2/4, 5/4, 7/4 | 4, 3, 2, 5, 7 | quarter notes |
| 2/2 | 2 | half notes |
| 6/8, 9/8, 12/8 | 2, 3, 4 | dotted quarters (3+3…) |
| 7/8 | 3 | 2+2+3 eighths |
| 5/8 | 2 | 3+2 eighths |

### Polyrhythm page

X evenly spaced pulses over Y quarter notes, e.g. 3:4 = three pulses across four beats. The
cycle realigns with the beat every Y quarters, independent of the bar.

| Parameter | Values (default) | What it does |
|---|---|---|
| Poly enable | Off / On (Off) | Turns the layer on. Joins in phase if switched on while playing. |
| Ratio preset | Custom, 3:2, 2:3, 3:4, 4:3, 6:4, 5:4, 4:5, 5:3, 7:4, 3:8 (3:2) | A shortcut that writes X and Y. Editing X or Y shows the matching preset, or Custom. 3:2 and 6:4 stay distinct (different cycle length and accents). |
| Pulses (X) | 1–16 (3) | Pulses per cycle. |
| Host beats (Y) | 1–16 (2) | Cycle length in quarter notes. |
| Poly audio | Off / On (On) | The polyrhythm's sound (a metallic bell-like click). |
| Poly vol | 0–100 % (70) | Level. |
| Poly accent | Off / On (On) | Stronger sound on the first pulse of each cycle. |
| Poly out / mode | output (Output 1), Add / Replace | Audio output. |
| Poly clock out | output (None) | A pulse for every polyrhythm pulse. |
| Poly clock res | 1 / 4 / 24 per pulse (1) | Clock multiplication — 24 per pulse turns the polyrhythm into a 24 PPQN clock for another sequencer. |
| Poly reset out | output (None) | Reset trigger. |
| Poly reset mode | Cycle / Start only (Cycle) | Reset at every cycle start, or only on start. |

Ratio and clock-resolution changes take effect at the next cycle boundary, so there are no
uneven transition pulses.

### Pattern page

| Parameter | Values (default) | What it does |
|---|---|---|
| Pattern enable | Off / On (Off) | Turns the layer on. |
| Pattern group | 9 groups (Afro-Cuban/African) | Choosing a group jumps to its first pattern. |
| Pattern | the group's patterns | Changes take effect at the end of the current cycle. |
| Swing | 50–75 % (50) | For the swing patterns only (greyed otherwise): 50 = straight, 66 = triplet swing, 75 = dotted. |
| Pattern audio | Off / On (On) | The pattern's sound (clave-like). |
| Pattern vol | 0–100 % (70) | Level. Ghost notes play at 40 % of it. |
| Pattern accent | Off / On (On) | Use the strong sound for accented hits. |
| Pattern out / mode | output (Output 1), Add / Replace | Audio output. |
| Pattern trig out | output (None) | A trigger for each hit. |
| Pat trig mode | All hits / No ghosts (All hits) | Whether ghost notes fire *Pattern trig out*. |
| Pat accent out | output (None) | A trigger on accented hits only. |
| Pat ghost out | output (None) | A trigger on ghost notes only. |
| Pat velocity out | output (None) | Stepped CV set at each hit and held: **5 V** accent, **3.5 V** hit, **1.5 V** ghost. It changes before the hit's trigger, so it can feed a VCA, a drum's level input or a sample & hold. |
| Pattern reset out | output (None) | Reset trigger. |
| Pat reset mode | Cycle / Start only (Cycle) | Reset at every cycle start, or only on start. |

**Patterns** — `X` accent, `x` hit, `g` ghost, `.` rest. Cycle length in quarter notes.

| Group | Pattern | Steps | Grid | Quarters |
|---|---|---|---|---|
| Afro-Cuban/African | Tresillo | `X..x..x.` | 1/8 | 4 |
| Afro-Cuban/African | Son clave 3-2 | `X..x..x...x.x...` | 1/8 | 8 |
| Afro-Cuban/African | Son clave 2-3 | `..x.x...X..x..x.` | 1/8 | 8 |
| Afro-Cuban/African | Rumba clave 3-2 | `X..x...x..x.x...` | 1/8 | 8 |
| Afro-Cuban/African | Rumba clave 2-3 | `..x.x...X..x...x` | 1/8 | 8 |
| Afro-Cuban/African | Cinquillo | `X.xx.xx.` | 1/8 | 4 |
| Afro-Cuban/African | Habanera | `X..xx.x.` | 1/16 | 2 |
| Afro-Cuban/African | Cascara | `X.x.xx.xX.xx.x.x` | 1/8 | 8 |
| Afro-Cuban/African | Std bell (bembe) | `X.x.xx.x.x.x` | 1/8 | 6 |
| Afro-Cuban/African | Bell 5-stroke | `X.x.x..x.x..` | 1/8 | 6 |
| Afro-Cuban/African | 6/8 short bell | `X.x.xx` | 1/8 | 3 |
| Odd meter | 5/8 2+3 | `Xgxgg` | 1/8 | 2.5 |
| Odd meter | 5/8 3+2 | `Xggxg` | 1/8 | 2.5 |
| Odd meter | 7/8 2+2+3 | `Xgxgxgg` | 1/8 | 3.5 |
| Odd meter | 7/8 2+3+2 | `Xgxggxg` | 1/8 | 3.5 |
| Odd meter | 7/8 3+2+2 | `Xggxgxg` | 1/8 | 3.5 |
| Odd meter | 9/8 3+3+3 | `Xggxggxgg` | 1/8 | 4.5 |
| Odd meter | 9/8 2+2+2+3 | `Xgxgxgxgg` | 1/8 | 4.5 |
| Odd meter | 9/8 2+2+3+2 | `Xgxgxggxg` | 1/8 | 4.5 |
| Odd meter | 9/8 2+3+2+2 | `Xgxggxgxg` | 1/8 | 4.5 |
| Odd meter | 11/8 3+3+3+2 | `Xggxggxggxg` | 1/8 | 5.5 |
| Odd meter | 11/8 2+2+3+2+2 | `Xgxgxggxgxg` | 1/8 | 5.5 |
| Odd meter | 11/8 3+2+3+3 | `Xggxgxggxgg` | 1/8 | 5.5 |
| Swing/shuffle | 8th swing | `Xgxgxgxg` | 1/8 | 4 · swing |
| Swing/shuffle | 16th swing | `Xgxgxgxgxgxgxgxg` | 1/16 | 4 · swing |
| Swing/shuffle | Triplet shuffle | `X.gx.gx.gx.g` | 1/8 triplet | 4 |
| Swing/shuffle | Half-time shuffle | `x.gx.gX.gx.g` | 1/8 triplet | 4 |
| Jazz/practice | 2 and 4 | `.X.X` | 1/4 | 4 |
| Jazz/practice | 8th offbeats | `.x.x.x.x` | 1/8 | 4 |
| Jazz/practice | 16th offbeats (e) | `.x...x...x...x..` | 1/16 | 4 |
| Jazz/practice | Displaced (a) | `...x...x...x...x` | 1/16 | 4 |
| Jazz/practice | Charleston | `X..x....` | 1/8 | 4 |
| Jazz/practice | Jazz ride | `X..x.xx..x.x` | 1/8 triplet | 4 |
| Jazz/practice | Jazz ride + 2/4 | `x..X.xx..X.x` | 1/8 triplet | 4 |
| Jazz/practice | Half-time backbeat | `..X.` | 1/4 | 4 |
| Indian tala | Teentaal 16 | `Xgggxggg.gggxggg` | 1/4 | 16 |
| Indian tala | Jhaptal 10 | `Xgxgg.gxgg` | 1/4 | 10 |
| Indian tala | Rupak 7 | `Xggxgxg` | 1/4 | 7 |
| Indian tala | Dadra 6 | `Xgg.gg` | 1/4 | 6 |
| Indian tala | Keherwa 8 | `Xggg.ggg` | 1/4 | 8 |
| Middle Eastern | Maqsum | `Xx.xX.x.` | 1/8 | 4 |
| Middle Eastern | Baladi | `XX.xX.x.` | 1/8 | 4 |
| Middle Eastern | Saidi | `Xx.XX.x.` | 1/8 | 4 |
| Middle Eastern | Malfuf | `X..x..x.` | 1/16 | 2 |
| Middle Eastern | Samai thaqil 10/8 | `X..x.XXx..` | 1/8 | 5 |
| Middle Eastern | Aksak 9/8 | `X.x.x.X..` | 1/8 | 4.5 |
| Brazilian | Bossa nova | `X..x..x...x..x..` | 1/8 | 8 |
| Brazilian | Samba | `xggxXggx` | 1/16 | 2 |
| Brazilian | Partido alto | `.X..x.x.x..x..x.` | 1/16 | 4 |
| Brazilian | Baiao | `X..Xx...` | 1/16 | 2 |
| Brazilian | Maracatu | `X..x..x.X..x..x.` | 1/16 | 4 |
| Caribbean | Dembow | `X..xX.x.X..xX.x.` | 1/16 | 4 |
| Caribbean | One drop | `.x.xXx.x` | 1/8 | 4 |
| Caribbean | Steppers | `X.x.X.x.X.x.X.x.` | 1/16 | 4 |
| Funk/electronic | Syncopated 16ths | `X..x...x..x.x...` | 1/16 | 4 |
| Funk/electronic | Funk backbeat | `x...X..g.gx.X..g` | 1/16 | 4 |
| Funk/electronic | Half-time groove | `x.....g.X..g..g.` | 1/16 | 4 |
| Funk/electronic | Four on the floor | `xgXgxgXg` | 1/8 | 4 |
| Funk/electronic | Accent cycle | `XggXggXgggXggXgg` | 1/16 | 4 |

Notes: odd-meter patterns accent each group start, strongest at the cycle start. Tala: sam =
accent, claps = hits, waves (khali) = rests, other counts = ghosts. Middle Eastern: D = accent,
T = hit. Several Brazilian and Caribbean entries are reduced guide patterns rather than full
arrangements.

### Output page

| Parameter | Values (default) | What it does |
|---|---|---|
| Master volume | 0–100 % (60) | Overall level of all layers. Full scale is 5 V peak. |
| Mute all | Off / On (Off) | Silences all audio including the count-in. CV and MIDI keep running. |
| Click sound | Classic / Soft (Classic) | *Classic*: woody, ringing. *Soft*: lower, shorter, drier. Applies to all layers; timing is identical. |
| MIDI out | Off / On (Off) | **Master switch** for all MIDI output: notes, clock, count and rec note. |
| MIDI out dest | USB / Breakout / Select Bus / Internal / USB + Breakout / All (USB) | Where MIDI goes. |
| MIDI clock out | Off / On (Off) | See the Count-in & Rec page. |
| MIDI out channel | 1–16 (10) | Channel for notes (10 = GM drums). |
| MIDI layers | All / Metronome / Polyrhythm / Pattern / Metro + Poly / Metro + Pattern / Poly + Pattern / None (All) | Which layers send notes. *None* = clock/rec note only. MIDI ignores the audio switches and *Mute all*, so you can silence the NT and drive only an external kit. |
| MIDI count out | Off / On (Off) | Also send the count-in as notes (downbeat / beat notes). |
| Downbeat note | 0–127 (76, hi wood block) | |
| Beat note | 0–127 (77, low wood block) | |
| Subdiv note | 0–127 (42, closed hi-hat) | |
| Poly note | 0–127 (56, cowbell) | |
| Poly accent note | 0–127 (56) | First pulse of each cycle (when *Poly accent* is on) — e.g. ONE on one sound, two–five on another. |
| Pattern note | 0–127 (75, claves) | |
| Pat accent note | 0–127 (75) | |
| Pat ghost note | 0–127 (75) | Send ghosts to a quieter kit piece. |

Note velocity follows the voice levels (at defaults: downbeat 127, beat 83, subdivision 32;
accents boosted to 127; ghosts ≈ 36). Note-offs follow 15 ms later. Notes are sent with the
audio block containing the click.

### How timing works

Position is counted at 24 PPQN. Every clock event (MIDI clock, CV edge or internal tick)
anchors the timeline; subdivisions, polyrhythm pulses at fractional positions and (swung)
pattern steps before the next clock event are scheduled to the sample from the measured
clock period. When the next clock arrives, anything pending fires and the timeline
re-anchors — no drift, and nothing runs on after the clock stops.

- CV clicks start on the exact sample of the clock edge. MIDI events take effect at the next
  audio block, because the plug-in API doesn't timestamp MIDI.
- With coarse CV clocks (1 PPQN, 1 per bar) the tempo is unknown until the second pulse, so
  events in the very first interval fire together on that pulse.
- Clock loss is detected after about 2.5 clock periods (CV) or 4 clock periods (MIDI).
- If the bar position can't be known (e.g. a RUN gate already high when the plug-in loads),
  the display shows `?` and no downbeat accent is played until the next reset or start.
- The internal clock uses a double-precision accumulator: no drift over hours.

---

## Build

Needs the [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
(`arm-none-eabi`) and the Disting NT API at the tag matching your firmware
(firmware 1.18 → [`v1.18.0`](https://github.com/expertsleepersltd/distingNT_API/archive/refs/tags/v1.18.0.zip)).
A plug-in built against a newer API than the module's firmware shows **failed** on the
plug-ins page.

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

Pushing a tag `v*` runs the GitHub Actions workflow, which runs the tests, builds
`metronomicon.o` and attaches it to a release.

Patterns are written as strings in `tools/gen_patterns.py`; run it to regenerate
`src/patterns.h`.

---

## Project layout

```
src/metronome_engine.h   timing + sound engine (platform-independent, header-only, no allocation)
src/patterns.h           pattern table (generated)
src/metronome.cpp        Disting NT glue: parameters, pages, MIDI, display, custom UI
tests/test_engine.cpp    host test suite with simulated CV/MIDI clocks
tools/gen_patterns.py    readable pattern list -> src/patterns.h
docs/banner.svg / .png   README banner
CHANGELOG.md             version history
```

---

## License

MIT — see [LICENSE](LICENSE).
