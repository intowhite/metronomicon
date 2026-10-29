// Metronomicon for Disting NT — timing + sound engine (v1_3)
//
// Platform-independent, header-only. No allocation, no blocking, no libm.
// The Disting NT glue (metronome.cpp) feeds it bus pointers and MIDI bytes;
// the host test suite (tests/test_engine.cpp) drives it with simulated clocks.
//
// Timing model
// ------------
// Musical position is counted in ticks at 24 PPQN. Every clock event (a MIDI
// 0xF8, a CV edge worth 1/6/12/24 ticks or one bar, or an internal-tempo tick)
// anchors a "span": tick T happened at sample t, and the next clock event is
// expected S ticks later. Three layers generate events at rational positions:
//
//   * Metronome:  every integer tick (bar/beat/subdivision + base clock out)
//   * Polyrhythm: X*R evenly spaced events per Y quarter notes
//   * Pattern:    a step grid with onsets (optionally swung), cycle in ticks
//
// Events inside the current span are scheduled at t + (pos - T) * samplesPerTick.
// When the next clock event arrives, anything still pending fires immediately and
// the span is re-anchored: no drift, and prediction never outruns the clock.
//
// CPU: each block is processed in two passes. Pass 1 walks time (per sample only
// where an input must be scanned, otherwise jumping from event to event) and
// records click / trigger events with their sample offsets. Pass 2 renders only
// the voices and outputs that are actually active.

#pragma once
#include <stdint.h>

namespace metro {
// Internal linkage: every engine function stays a local symbol in the plugin .o.
namespace {

static const int32_t kTicksPerQuarter = 24;

enum ClockSource { kSourceCV = 0, kSourceMIDI = 1, kSourceInternal = 2 };
enum MidiMode { kMidiTransport = 0, kMidiClockOnly = 1 };
enum CvTransportMode { kCvRun = 0, kCvReset = 1, kCvNone = 2, kCvToggle = 3 };
enum CvResolution { kCv24ppqn = 0, kCv4ppqn, kCv2ppqn, kCv1ppqn, kCvBar, kNumCvRes };
enum Division { kDivBeats = 0, kDiv4, kDiv8, kDiv16 };
enum BaseClockRes { kBase4 = 0, kBase8, kBase16, kBase24ppqn };
enum PolyClockRes { kPoly1 = 0, kPoly4, kPoly24 };
enum ResetMode { kResetCycle = 0, kResetStart = 1 };
// Held CV levels (gates / stepped CV)
enum LevelOut { kLvlVelocity = 0, kLvlRun, kLvlCountIn, kLvlRecGate, kNumLevels };
enum PatTrigMode { kTrigAllHits = 0, kTrigNoGhosts = 1 };
// MIDI drum voices (one note number each)
enum MidiVoice {
    kVoiceDown = 0, kVoiceBeat, kVoiceSub, kVoicePoly, kVoicePattern, kVoicePatAccent, kVoicePolyAccent, kVoicePatGhost,
    kNumMidiVoices
};
enum ClickSound { kSoundClassic = 0, kSoundSoft = 1, kNumSounds };
enum Status { kStatusStopped = 0, kStatusWaiting, kStatusAcquiring, kStatusRunning, kStatusLost, kStatusCountIn };
enum RecState { kRecIdle = 0, kRecWaiting, kRecRecording, kRecEnding };
enum RecMode { kRecTrigger = 0, kRecGate = 1 };

// Click voices. 0-2 metronome, 3-4 polyrhythm, 5-6 pattern.
enum Bank {
    kClickSub = 0, kClickBeat, kClickDown, kClickPoly, kClickPolyAccent, kClickPatWeak, kClickPatStrong,
    kClickCount, kClickCountAccent, kNumBanks
};
enum LayerId { kLayerMetro = 0, kLayerPoly, kLayerPattern, kNumLayers };
enum TrigOut {
    kOutBaseClock = 0, kOutBaseReset, kOutPolyClock, kOutPolyReset, kOutPatTrig, kOutPatAccent, kOutPatReset,
    kOutPatGhost, kOutRecTrig, kNumTrigOuts
};
static inline int bankLayer(int b) {
    return b <= kClickDown ? kLayerMetro : (b <= kClickPolyAccent ? kLayerPoly : (b <= kClickPatStrong ? kLayerPattern : kLayerMetro));
}

// ---------------------------------------------------------------- meters
struct Meter {
    const char* name;
    uint8_t barTicks;
    uint8_t numBeats;
    uint8_t beat[7];  // beat start positions in ticks from the bar line
};
enum { kNumMeters = 11 };
static const Meter kMeters[kNumMeters] = {
    {"4/4", 96, 4, {0, 24, 48, 72}},
    {"3/4", 72, 3, {0, 24, 48}},
    {"6/8", 72, 2, {0, 36}},             // two dotted-quarter beats
    {"2/4", 48, 2, {0, 24}},
    {"12/8", 144, 4, {0, 36, 72, 108}},  // four dotted-quarter beats
    {"2/2", 96, 2, {0, 48}},             // two half-note beats
    {"5/4", 120, 5, {0, 24, 48, 72, 96}},
    {"7/8", 84, 3, {0, 24, 48}},         // 2+2+3
    {"9/8", 108, 3, {0, 36, 72}},        // 3+3+3
    {"5/8", 60, 2, {0, 36}},             // 3+2
    {"7/4", 168, 7, {0, 24, 48, 72, 96, 120, 144}},
};

// ---------------------------------------------------------------- polyrhythm presets
enum { kNumPolyPresets = 11 };  // index 0 = Custom
static const uint8_t kPolyPresets[kNumPolyPresets][2] = {
    {0, 0}, {3, 2}, {2, 3}, {3, 4}, {4, 3}, {6, 4}, {5, 4}, {4, 5}, {5, 3}, {7, 4}, {3, 8},
};
static inline int polyPresetFor(int x, int y) {
    for (int i = 1; i < kNumPolyPresets; ++i)
        if (kPolyPresets[i][0] == x && kPolyPresets[i][1] == y) return i;
    return 0;
}

// ---------------------------------------------------------------- rhythm patterns
struct PatternDef {
    const char* name;
    uint8_t steps;        // grid steps per cycle (<= 16)
    uint8_t stepTicks;    // 6 = 1/16, 8 = triplet 1/8, 12 = 1/8, 24 = 1/4
    uint16_t cycleTicks;  // steps * stepTicks
    uint16_t onsets;      // bit i = event on step i
    uint16_t accents;     // accented events (strong sound, accent trigger)
    uint16_t ghosts;      // quiet events
    uint8_t swing;        // responds to the Swing parameter
};
#include "patterns.h"

// ---------------------------------------------------------------- settings
struct Settings {
    // clock
    int source = kSourceCV;
    int midiMode = kMidiTransport;
    int cvResolution = kCv24ppqn;
    int cvTransport = kCvReset;
    float internalBpm = 120.0f;
    bool play = false;  // internal clock transport (start/stop requests)
    // metronome
    int meter = 0;
    int division = kDivBeats;
    bool metroAudio = true;
    float levelDown = 1.0f, levelBeat = 0.65f, levelSub = 0.25f;
    int baseClockRes = kBase16;
    int baseResetMode = kResetCycle;
    // polyrhythm
    bool polyEnable = false;
    int polyX = 3, polyY = 2;
    bool polyAudio = true;
    float polyLevel = 0.7f;
    bool polyAccent = true;
    int polyClockRes = kPoly1;
    int polyResetMode = kResetCycle;
    // pattern
    bool patEnable = false;
    int pattern = 0;  // index into kPatterns
    int swing = 50;   // 50..75 %
    bool patAudio = true;
    float patLevel = 0.7f;
    bool patAccent = true;
    int patResetMode = kResetCycle;
    // global
    float master = 0.60f;
    bool mute = false;
    int sound = kSoundClassic;
    // pattern CV
    int patTrigMode = kTrigAllHits;
    // MIDI drum output (independent of the audio switches)
    bool midiOut = false;  // master switch for all MIDI output
    int midiChannel = 10;
    int midiLayers = 7;  // bit 0 metronome, bit 1 polyrhythm, bit 2 pattern
    int midiNote[kNumMidiVoices] = {76, 77, 42, 56, 75, 75, 56, 75};
    // count-in, sync and recording
    int countInBars = 0;        // 0 = off (internal clock only)
    bool countInMidi = false;   // MIDI count out: send the count as notes
    bool countInClock = false;  // Count-in clock: false = Wait, true = Run
    bool midiClockOut = false;  // 24 PPQN clock + Start/Stop (not when following MIDI)
    bool recArm = false;
    int recLengthBars = 0;      // 0 = manual (ends on the bar line after disarm/stop)
    int recMode = kRecTrigger;
    int recNote = -1;           // MIDI note for record start/stop, -1 = off
};

struct MidiMsg { uint8_t b0, b1, b2, len; };  // len 1 = realtime byte

// Bus pointers for one block (NULL = not assigned).
struct Outputs {
    float* audio[kNumLayers] = {nullptr, nullptr, nullptr};
    bool replace[kNumLayers] = {false, false, false};
    float* trig[kNumTrigOuts] = {};
    float* level[kNumLevels] = {};  // held-level outputs (velocity CV, run / count-in / rec gates)
};

static inline int32_t cvResolutionTicks(int r, int meter) {
    switch (r) {
        case kCv24ppqn: return 1;
        case kCv4ppqn: return 6;
        case kCv2ppqn: return 12;
        case kCv1ppqn: return 24;
        default: return kMeters[meter].barTicks;
    }
}
static inline int32_t divisionTicks(int d) { return d == kDiv4 ? 24 : (d == kDiv8 ? 12 : (d == kDiv16 ? 6 : 0)); }
static inline int32_t baseClockTicks(int r) { return r == kBase4 ? 24 : (r == kBase8 ? 12 : (r == kBase16 ? 6 : 1)); }
static inline int32_t polyClockMult(int r) { return r == kPoly1 ? 1 : (r == kPoly4 ? 4 : 24); }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

namespace detail {
static const double kPi = 3.14159265358979323846;
// Init-time only (never called from the audio path).
inline double dexp(double x) {
    int k = 0;
    while ((x < -0.5 || x > 0.5) && k < 60) { x *= 0.5; ++k; }
    double s = 1.0, t = 1.0;
    for (int i = 1; i < 18; ++i) { t *= x / i; s += t; }
    while (k-- > 0) s *= s;
    return s;
}
inline double dcos(double x) {
    while (x > kPi) x -= 2.0 * kPi;
    while (x < -kPi) x += 2.0 * kPi;
    double x2 = x * x, s = 1.0, t = 1.0;
    for (int i = 1; i < 22; ++i) { t *= -x2 / ((2.0 * i - 1.0) * (2.0 * i)); s += t; }
    return s;
}
inline double dsin(double x) { return dcos(x - 0.5 * kPi); }
}  // namespace detail

// ---------------------------------------------------------------- sound
struct Resonator {
    float a1 = 0, a2 = 0, norm = 0, y1 = 0, y2 = 0;
    void setup(double freq, double tau, double sr) {
        double w = 2.0 * detail::kPi * freq / sr;
        double r = detail::dexp(-1.0 / (tau * sr));
        a1 = (float)(2.0 * r * detail::dcos(w));
        a2 = (float)(r * r);
        norm = (float)detail::dsin(w);
        y1 = y2 = 0;
    }
};

struct ClickPreset {
    float f1, tau1;          // body mode
    float f2, tau2, g2;      // upper mode
    float tauNoise, gNoise;  // contact transient
};

static const ClickPreset kPresets[kNumSounds][kNumBanks] = {
  {  // Classic: woody, resonant
    /* metro sub      */ {2600.f, 0.0055f, 6100.f, 0.0025f, 0.25f, 0.0005f, 0.10f},
    /* metro beat     */ {1250.f, 0.0100f, 3400.f, 0.0040f, 0.35f, 0.0007f, 0.18f},
    /* metro down     */ {1900.f, 0.0110f, 5000.f, 0.0045f, 0.45f, 0.0008f, 0.25f},
    /* poly           */ {820.f, 0.0300f, 1260.f, 0.0200f, 0.60f, 0.0004f, 0.05f},  // metallic, bell-like
    /* poly accent    */ {1080.f, 0.0350f, 1650.f, 0.0240f, 0.60f, 0.0004f, 0.08f},
    /* pattern weak   */ {2050.f, 0.0160f, 4700.f, 0.0060f, 0.15f, 0.0003f, 0.05f},  // clave-like
    /* pattern strong */ {2500.f, 0.0280f, 5600.f, 0.0080f, 0.20f, 0.0003f, 0.08f},
    /* count          */ {2300.f, 0.0050f, 5400.f, 0.0025f, 0.30f, 0.0004f, 0.18f},  // high, short "tick"
    /* count accent   */ {2900.f, 0.0060f, 6600.f, 0.0030f, 0.30f, 0.0004f, 0.22f},
  },
  {  // Soft: lower, short and dry - more "tock"/thump, less ring
    /* metro sub      */ {1500.f, 0.0030f, 3000.f, 0.0015f, 0.20f, 0.0004f, 0.30f},
    /* metro beat     */ {700.f, 0.0045f, 1600.f, 0.0020f, 0.25f, 0.0005f, 0.35f},
    /* metro down     */ {950.f, 0.0050f, 2100.f, 0.0025f, 0.30f, 0.0006f, 0.40f},
    /* poly           */ {520.f, 0.0070f, 1100.f, 0.0040f, 0.30f, 0.0004f, 0.20f},
    /* poly accent    */ {650.f, 0.0080f, 1400.f, 0.0045f, 0.30f, 0.0004f, 0.25f},
    /* pattern weak   */ {1200.f, 0.0050f, 2600.f, 0.0020f, 0.20f, 0.0005f, 0.30f},
    /* pattern strong */ {1500.f, 0.0055f, 3200.f, 0.0025f, 0.25f, 0.0005f, 0.35f},
    /* count          */ {1600.f, 0.0035f, 3300.f, 0.0018f, 0.25f, 0.0004f, 0.30f},
    /* count accent   */ {1900.f, 0.0040f, 3800.f, 0.0020f, 0.25f, 0.0004f, 0.35f},
  },
};

// One click voice: two resonators + a short noise transient. Re-triggering adds
// energy (superposition), so overlapping clicks never step.
struct ClickBank {
    float a1 = 0, a2 = 0, n1 = 0, b1 = 0, b2 = 0, n2 = 0;  // resonator coefficients
    float y1 = 0, y2 = 0, z1 = 0, z2 = 0;                  // resonator state
    float g2 = 0, gNoise = 0, noiseDecay = 0, noiseEnv = 0, normGain = 1;
    float chokeGain = 1.0f;
    int32_t remaining = 0, length = 0;
    bool choking = false;
    uint32_t lastTrigger = 0;
    bool everTriggered = false;

    void setup(const ClickPreset& p, double sr) {
        Resonator r1, r2;
        r1.setup(p.f1, p.tau1, sr);
        r2.setup(p.f2, p.tau2, sr);
        a1 = r1.a1; a2 = r1.a2; n1 = r1.norm;
        b1 = r2.a1; b2 = r2.a2; n2 = r2.norm;
        g2 = p.g2;
        gNoise = p.gNoise;
        noiseDecay = (float)detail::dexp(-1.0 / (p.tauNoise * sr));
        double tmax = p.tau1 > p.tau2 ? p.tau1 : p.tau2;
        length = (int32_t)(6.5 * tmax * sr) + 1;  // ~ -56 dB at cutoff
        // Normalise so a full-level click peaks at 1.0 (noise excluded).
        normGain = 1.0f;
        float saveNoise = gNoise;
        gNoise = 0;
        trigger(1.0f);
        float peak = 0, buf[64];
        uint32_t rng = 1, prev = 0;
        for (int32_t i = 0; i < length; i += 64) {
            int m = length - i < 64 ? (int)(length - i) : 64;
            for (int k = 0; k < m; ++k) buf[k] = 0;
            render(buf, m, rng, prev);
            for (int k = 0; k < m; ++k) {
                float y = buf[k] < 0 ? -buf[k] : buf[k];
                if (y > peak) peak = y;
            }
        }
        gNoise = saveNoise;
        clear();
        everTriggered = false;
        normGain = peak > 0 ? 1.0f / peak : 1.0f;
    }
    float pending = 0;  // impulse to inject on the next rendered sample

    void clear() {
        y1 = y2 = z1 = z2 = 0;
        pending = 0;
        noiseEnv = 0;
        remaining = 0;
        choking = false;
        chokeGain = 1.0f;
    }
    inline void trigger(float amp) {
        if (choking) clear();
        pending += amp;
        noiseEnv += amp;
        remaining = length;
    }
    // Fade a just-started click out in ~0.5 ms.
    inline void choke() {
        if (remaining > 0) choking = true;
    }
    // Adds m samples of this voice into out[].
    inline void render(float* out, int m, uint32_t& rng, uint32_t& prevNoise) {
        if (remaining <= 0) return;
        if (m > remaining) m = remaining;
        float Y1 = y1, Y2 = y2, Z1 = z1, Z2 = z2, x = pending;
        pending = 0;
        const float A1 = a1, A2 = a2, B1 = b1, B2 = b2, kN1 = n1, kN2 = n2 * g2;
        float g = normGain * chokeGain;
        bool done = false;
        int k = 0;
        // slow path while the noise transient or a choke fade is running
        for (; k < m && (noiseEnv > 1e-5f || choking); ++k) {
            float y = A1 * Y1 - A2 * Y2 + x;
            float z = B1 * Z1 - B2 * Z2 + x;
            x = 0;
            Y2 = Y1; Y1 = y; Z2 = Z1; Z1 = z;
            float s = y * kN1 + z * kN2;
            if (noiseEnv > 1e-5f) {
                rng = rng * 1664525u + 1013904223u;
                uint32_t r = rng >> 9;
                s += gNoise * noiseEnv * ((float)(int32_t)(r - prevNoise)) * (1.0f / 8388608.0f);
                prevNoise = r;
                noiseEnv *= noiseDecay;
            }
            if (choking) {
                chokeGain *= 0.88f;
                g = normGain * chokeGain;
                if (chokeGain < 1e-4f) { done = true; break; }
            }
            out[k] += s * g;
        }
        // fast path: two resonators, no branches
        if (!done) {
            for (; k < m; ++k) {
                float y = A1 * Y1 - A2 * Y2 + x;
                float z = B1 * Z1 - B2 * Z2 + x;
                x = 0;
                Y2 = Y1; Y1 = y; Z2 = Z1; Z1 = z;
                out[k] += (y * kN1 + z * kN2) * g;
            }
        }
        y1 = Y1; y2 = Y2; z1 = Z1; z2 = Z2;
        remaining -= m;
        if (done || remaining <= 0) clear();
    }
};

// 5 V trigger / clock pulse generator for a CV output.
struct Trig {
    int32_t delay = 0, pendLen = 0, left = 0;
    uint32_t lastFire = 0;
    bool everFired = false;
    void fire(int32_t len, int32_t delaySamples) {
        if (delaySamples > 0) {
            delay = delaySamples + 1;  // first high sample = event + delaySamples
            pendLen = len;
        } else {
            delay = 0;
            left = len;
        }
    }
    inline bool idle() const { return delay == 0 && left == 0; }
    inline float tick() {
        if (delay > 0 && --delay == 0) left = pendLen;
        if (left > 0) { --left; return 5.0f; }
        return 0.0f;
    }
    void clear() { delay = pendLen = left = 0; }
};

// A stream of events: n per cycle of cycleTicks ticks. Event idx of the current
// cycle sits at cycleStart + idx * cycleTicks / n, odd events optionally delayed
// by swing (50 = straight, 66 = triplet swing, 75 = dotted).
// Positions are exact integers in units of 1/(n*100) tick.
struct Layer {
    bool on = false;
    int32_t cycleTicks = 1, n = 1, tag = 0, swing = 50;              // active configuration
    int32_t nextCycleTicks = 1, nextN = 1, nextTag = 0;              // applied at a cycle boundary
    int32_t cycleStart = 0, idx = 0;
    bool dueValid = false;
    uint32_t due = 0;

    inline int32_t den() const { return n * 100; }
    // (position of the next event - T) * den()
    inline int32_t num(int32_t T) const {
        int32_t v = (cycleStart - T) * n * 100 + idx * cycleTicks * 100;
        if (swing != 50 && (idx & 1)) v += (2 * swing - 100) * cycleTicks;
        return v;
    }
    inline void advance() {
        if (++idx >= n) {
            idx = 0;
            cycleStart += cycleTicks;
            cycleTicks = nextCycleTicks;  // rhythmic changes land on a cycle boundary
            n = nextN;
            tag = nextTag;
        }
    }
    // Position so that the next event is the first one at or after tick T.
    void seek(int32_t T, int32_t origin) {
        cycleTicks = nextCycleTicks;
        n = nextN;
        tag = nextTag;
        int32_t rel = T - origin;
        int32_t c = rel / cycleTicks, r = rel % cycleTicks;
        if (r < 0) { r += cycleTicks; --c; }
        cycleStart = origin + c * cycleTicks;
        int32_t i = (r * n + cycleTicks - 1) / cycleTicks;
        if (i >= n) { i = 0; cycleStart += cycleTicks; }
        idx = i;
        if (idx > 0 && swing > 50) {  // a swung odd event before i may still be ahead
            --idx;
            if (num(T) < 0) ++idx;
        }
        dueValid = false;
    }
};

// Rising-edge detector for a trigger/gate input that flips a setting.
// Schmitt thresholds (high > 1 V, low < 0.5 V) and a hold-off reject noise and bounce.
struct ToggleInput {
    bool high = false, primed = false;
    int32_t holdoff = 0, holdoffLen = 2400;  // 50 ms at 48 kHz
    void init(float sr) { holdoffLen = (int32_t)(0.05f * sr); prime(); }
    void prime() { primed = false; holdoff = 0; }
    // Returns the number of triggers (rising edges) in the block.
    int scan(const float* in, int n) {
        if (!in) { primed = false; return 0; }
        int count = 0;
        for (int i = 0; i < n; ++i) {
            const float v = in[i];
            if (holdoff > 0) --holdoff;
            if (!primed) { high = v > 1.0f; primed = true; continue; }  // adopt the level, no edge
            if (!high) {
                if (v > 1.0f) {
                    high = true;
                    if (holdoff == 0) { ++count; holdoff = holdoffLen; }
                }
            } else if (v < 0.5f) {
                high = false;
            }
        }
        return count;
    }
};

// ---------------------------------------------------------------- engine
class Engine {
public:
#ifdef METRO_ENGINE_TEST
    // Test hook: every click the engine decides to play (even when not audible).
    void (*onClick)(void* ctx, uint32_t sampleTime, int bank, int32_t tick, bool audible) = nullptr;
    void* onClickCtx = nullptr;
#endif

    void init(float sampleRate) {
        sr_ = sampleRate > 1000.f ? sampleRate : 48000.f;
        sound_ = kSoundClassic;
        for (int i = 0; i < kNumBanks; ++i) banks_[i].setup(kPresets[sound_][i], sr_);
        window_ = (int32_t)(0.003f * sr_);
        minInterval_ = (int32_t)(0.001f * sr_);
        resetLead_ = (int32_t)(0.001f * sr_);
        trigLen_ = (int32_t)(0.005f * sr_);
        gainCoef_ = (float)(1.0 - detail::dexp(-1.0 / (0.004 * sr_)));
        for (int i = 0; i < kNumLayers; ++i) gain_[i] = 0;
        now_ = 0;
        rng_ = 22222u;
        prevNoise_ = 0;
        fifoRead_ = fifoWrite_ = 0;
        haveSettings_ = false;
        layers_[kLayerMetro].on = true;
        tapCount_ = 0;
        tapPending_ = false;
        resetSync();
    }

    // ---- MIDI input (may be called from outside the audio callback) ----
    void midiRealtime(uint8_t byte) {
        if (byte == 0xF8 || byte == 0xFA || byte == 0xFB || byte == 0xFC) fifoPush(byte);
    }
    void midiMessage(uint8_t b0, uint8_t b1, uint8_t b2) {
        if (b0 == 0xF2) fifoPush(0xF2u | ((uint32_t)(b1 & 0x7F) << 8) | ((uint32_t)(b2 & 0x7F) << 16));
        else if (b0 == 0xF8 || b0 == 0xFA || b0 == 0xFB || b0 == 0xFC) fifoPush(b0);
    }

    // Call when a CV input bus assignment changes.
    void primeInputs() { prime_ = true; }

    // A tap (from the Tap parameter). Tap inputs on a bus are passed to process().
    void tapNow() { tap(now_); }
    // A tempo produced by tapping, for the host to store in its BPM parameter.
    // MIDI messages produced by the last process() call (send them in order).
    int midiCount() const { return nMidi_; }
    const MidiMsg* midiMessages() const { return midi_; }

    bool takeTapBpm(float& bpm) {
        if (!tapPending_) return false;
        tapPending_ = false;
        bpm = tapBpm_;
        return true;
    }

    // ---- Audio ----
    void process(const Settings& s, const float* clockIn, const float* transportIn, const Outputs& o, int n,
                 const float* tapIn = nullptr) {
        nMidi_ = 0;
        int off = 0;
        while (off < n) {
            int m = n - off < kMaxBlock ? n - off : kMaxBlock;
            Outputs oo = o;
            for (int l = 0; l < kNumLayers; ++l) if (oo.audio[l]) oo.audio[l] += off;
            for (int k = 0; k < kNumTrigOuts; ++k) if (oo.trig[k]) oo.trig[k] += off;
            for (int k = 0; k < kNumLevels; ++k) if (oo.level[k]) oo.level[k] += off;
            processChunk(s, clockIn ? clockIn + off : nullptr, transportIn ? transportIn + off : nullptr,
                         tapIn ? tapIn + off : nullptr, oo, m);
            off += m;
        }
    }

    // ---- Display state ----
    Status status() const { return (Status)status_; }
    float bpm() const { return bpmShown_; }
    int beat() const { return displayBeat_; }
    int beatsPerBar() const { return kMeters[activeMeter_].numBeats; }
    bool positionKnown() const { return posKnown_; }
    int polyPulse() const { return displayPoly_; }
    int patternStep() const { return displayStep_; }
    int activePattern() const { return layers_[kLayerPattern].tag; }
    // internal transport: playing or counting in (a pending quantized stop counts as off)
    bool transportOn() const { return tstate_ != kTStopped && !stopPending_; }
    int countBarsLeft() const { return displayCountBars_; }
    int countBeat() const { return displayCountBeat_; }
    int recState() const { return recState_; }
    int recBars() const { return recBars_; }
    // true once after a take finished: the host should switch Rec arm off
    bool takeRecDisarm() { bool d = recDisarm_; recDisarm_ = false; return d; }

private:
    static const uint32_t kFifoSize = 64;
    static const int kRing = 97;
    static const int32_t kMaxFlushTicks = 400;
    enum { kMaxBlock = 128, kMaxEvents = 96 };

    struct SoundEvent { uint16_t offset; uint8_t bank; uint8_t choke; float amp; };
    struct TrigEvent { uint16_t offset; uint8_t out; int32_t len, delay; };
    struct LevelEvent { uint16_t offset; uint8_t which; float value; };
    struct NoteOff { uint32_t due; uint8_t ch, note; };
    enum { kMaxMidi = 96, kMaxNoteOffs = 16 };

    Settings cur_;
    bool haveSettings_ = false;
    float sr_ = 48000.f;
    Outputs out_;

    // sound
    ClickBank banks_[kNumBanks];
    int sound_ = 0;
    float gain_[kNumLayers] = {0, 0, 0};
    float gainCoef_ = 0;
    uint32_t rng_ = 1, prevNoise_ = 0;
    Trig trigs_[kNumTrigOuts];
    int32_t trigLen_ = 240, resetLead_ = 48;
    SoundEvent snd_[kMaxEvents];
    TrigEvent trg_[kMaxEvents];
    LevelEvent lvl_[kMaxEvents];
    int nLvl_ = 0;
    float lvlHeld_[kNumLevels] = {0, 0, 0, 0};
    // internal transport / count-in / recording
    enum { kTStopped = 0, kTCountIn, kTPlaying };
    int tstate_ = kTStopped;
    bool stopPending_ = false, sessionActive_ = false;
    int32_t countPos_ = 0, countTicks_ = 0, countBarTicks_ = 96;
    int countMeter_ = 0;
    volatile int displayCountBars_ = 0, displayCountBeat_ = -1;
    int recState_ = kRecIdle, recBars_ = 0;
    bool recDisarm_ = false, recNoteHeld_ = false;
    uint8_t recNoteCh_ = 0, recNoteNum_ = 0;
    MidiMsg midi_[kMaxMidi];
    int nMidi_ = 0;
    NoteOff offs_[kMaxNoteOffs];
    int nOffs_ = 0;
    int nSnd_ = 0, nTrg_ = 0;
    uint32_t blockStart_ = 0;
    float layerBuf_[kNumLayers][kMaxBlock];

    // MIDI FIFO
    uint32_t fifo_[kFifoSize];
    volatile uint32_t fifoRead_ = 0, fifoWrite_ = 0;

    uint32_t now_ = 0;
    int32_t window_ = 144, minInterval_ = 48;

    // transport / position
    bool running_ = false, armed_ = false, lost_ = false, posKnown_ = false, pendingReset_ = false;
    bool sessionStartPending_ = false;
    int32_t origin_ = 0, barStart_ = 0;
    int activeMeter_ = 0, pendingMeter_ = 0;
    volatile int displayBeat_ = -1, displayPoly_ = -1, displayStep_ = -1;

    // layers & span
    Layer layers_[kNumLayers];
    int32_t spanT_ = 0, spanS_ = 1;
    uint32_t spanTime_ = 0;
    float tickPeriod_ = 0;
    bool tickPeriodValid_ = false;
    bool anyDue_ = false;
    uint32_t minDue_ = 0;
    bool relabeling_ = false;
    uint32_t relabelEdgeTime_ = 0;
    bool suppressMetroAudio_ = false;
    int32_t lastMetroClickTick_ = 0;
    int lastMetroClickBank_ = -1;

    // CV / internal pulses
    bool prime_ = true, clkHigh_ = false, trHigh_ = false, tapHigh_ = false;
    bool haveEdge_ = false;
    uint32_t lastEdgeTime_ = 0;
    int32_t lastEdgeTick_ = 0, cvTick_ = 0, lastSpan_ = 1;
    int32_t cvTimeout_ = 0;
    // internal clock
    bool intActive_ = false;
    uint32_t intNext_ = 0;
    double intFrac_ = 0, intPeriod_ = 1000.0;  // double: no drift over hours
    float tapOverride_ = 0;
    // tap tempo
    uint32_t tapTimes_[5];
    int tapCount_ = 0;
    bool tapPending_ = false;
    float tapBpm_ = 0;

    // MIDI
    bool midiHaveClock_ = false;
    uint32_t midiLastClock_ = 0;
    int32_t midiTick_ = 0, midiTimeout_ = 0;
    uint32_t ring_[kRing];
    int ringPos_ = 0, ringCount_ = 0;

    float bpmRaw_ = 0;
    volatile float bpmShown_ = 0;
    volatile int status_ = kStatusWaiting;

    // ------------------------------------------------------------------
    static inline int32_t since(uint32_t now, uint32_t then) { return (int32_t)(now - then); }

    void fifoPush(uint32_t e) {
        uint32_t w = fifoWrite_;
        if (w - fifoRead_ >= kFifoSize) return;
        fifo_[w & (kFifoSize - 1)] = e;
        fifoWrite_ = w + 1;
    }

    bool clicksEnabled() const { return running_ && !armed_; }
    bool pulseSource() const { return cur_.source != kSourceMIDI; }  // CV or internal: same transport logic

    // ---------------- block processing ----------------
    void processChunk(const Settings& s, const float* clockIn, const float* transportIn, const float* tapIn,
                      const Outputs& o, int n) {
        const uint32_t t0 = now_;
        blockStart_ = t0;
        nSnd_ = nTrg_ = nLvl_ = 0;
        out_ = o;
        applySettings(s);

        while (fifoRead_ != fifoWrite_) {
            uint32_t e = fifo_[fifoRead_ & (kFifoSize - 1)];
            fifoRead_ = fifoRead_ + 1;
            if (cur_.source == kSourceMIDI) handleMidi(e, t0);
        }
        if (cur_.source == kSourceMIDI) checkMidiTimeout(t0);

        // ---- pass 1: timeline ----
        const bool cv = cur_.source == kSourceCV;
        const bool internal = cur_.source == kSourceInternal;
        const bool scanTransport = pulseSource() && cur_.cvTransport != kCvNone && transportIn;
        const bool scanTap = internal && tapIn;
        if (cv || scanTransport || scanTap) {
            for (int i = 0; i < n; ++i) {
                const uint32_t t = t0 + (uint32_t)i;
                if (prime_) {
                    primeLevels(clockIn ? clockIn[i] : 0.0f, transportIn ? transportIn[i] : 0.0f,
                                tapIn ? tapIn[i] : 0.0f);
                } else {
                if (scanTransport) {
                    const float r = transportIn[i];
                    if (!trHigh_) { if (r > 1.0f) { trHigh_ = true; onTransportRise(t); } }
                    else if (r < 0.5f) { trHigh_ = false; onTransportFall(t); }
                }
                if (scanTap) {
                    const float r = tapIn[i];
                    if (!tapHigh_) { if (r > 1.0f) { tapHigh_ = true; tap(t); } }
                    else if (r < 0.5f) tapHigh_ = false;
                }
                if (cv && clockIn) {
                    const float c = clockIn[i];
                    if (!clkHigh_) { if (c > 1.0f) { clkHigh_ = true; onCvEdge(t); } }
                    else if (c < 0.5f) clkHigh_ = false;
                }
                }
                if (internal && intActive_ && since(t, intNext_) >= 0) internalTick(t);
                if (anyDue_ && since(t, minDue_) >= 0) runScheduled(t);
            }
        } else {
            if (prime_) primeLevels(0.0f, 0.0f, 0.0f);
            // event-to-event jumps (MIDI, or internal clock without inputs to scan)
            const uint32_t end = t0 + (uint32_t)n;
            for (int guard = 0; guard < 4 * kMaxBlock; ++guard) {
                uint32_t t = end;
                if (internal && intActive_ && since(intNext_, t) < 0) t = intNext_;
                if (anyDue_ && since(minDue_, t) < 0) t = minDue_;
                if (since(t, t0) < 0) t = t0;
                if (t == end) break;
                if (internal && intActive_ && since(t, intNext_) >= 0) internalTick(t);
                if (anyDue_ && since(t, minDue_) >= 0) runScheduled(t);
            }
        }
        if (cv && haveEdge_ && since(t0 + (uint32_t)n, lastEdgeTime_) > cvTimeout_) onCvLost(t0 + (uint32_t)n - 1);

        flushNoteOffs(t0 + (uint32_t)n, false);

        // ---- pass 2: render ----
        render(o, n);
        now_ = t0 + (uint32_t)n;
        updateStatus();
    }

    void primeLevels(float c, float r, float tp) {
        prime_ = false;
        clkHigh_ = c > 1.0f;
        trHigh_ = r > 1.0f;
        tapHigh_ = tp > 1.0f;
        if (cur_.source == kSourceCV && cur_.cvTransport == kCvRun && trHigh_ && !running_) {
            // Gate already high: playing, but the bar position is unknowable.
            running_ = true;
            armed_ = true;
            posKnown_ = false;
        }
    }

    void setLevel(int which, uint32_t t, float v) {
        if (nLvl_ < kMaxEvents) lvl_[nLvl_++] = {(uint16_t)(t - blockStart_), (uint8_t)which, v};
        else lvlHeld_[which] = v;
    }
    void renderLevels(const Outputs& o, int n) {
        for (int k = 0; k < kNumLevels; ++k) {
            float* dst = o.level[k];
            int pos = 0;
            for (int e = 0; e <= nLvl_; ++e) {
                if (e < nLvl_ && lvl_[e].which != k) continue;
                int at = e < nLvl_ ? lvl_[e].offset : n;
                if (dst) for (; pos < at; ++pos) dst[pos] = lvlHeld_[k];
                if (e < nLvl_) lvlHeld_[k] = lvl_[e].value;
            }
        }
    }

    void render(const Outputs& o, int n) {
        renderLevels(o, n);
        // voices
        bool layerActive[kNumLayers] = {false, false, false};
        for (int b = 0; b < kNumBanks; ++b) {
            ClickBank& bank = banks_[b];
            bool hasEvents = false;
            for (int e = 0; e < nSnd_; ++e) if (snd_[e].bank == b) { hasEvents = true; break; }
            if (!hasEvents && bank.remaining <= 0) continue;
            const int l = bankLayer(b);
            float* buf = layerBuf_[l];
            if (!layerActive[l]) {
                for (int i = 0; i < n; ++i) buf[i] = 0;
                layerActive[l] = true;
            }
            int pos = 0;
            for (int e = 0; e < nSnd_; ++e) {
                if (snd_[e].bank != b) continue;
                int at = snd_[e].offset;
                if (at > pos) { bank.render(buf + pos, at - pos, rng_, prevNoise_); pos = at; }
                if (snd_[e].choke) bank.choke();
                else bank.trigger(snd_[e].amp);
            }
            if (pos < n) bank.render(buf + pos, n - pos, rng_, prevNoise_);
        }

        // gains + outputs (layers sharing a bus are summed before writing)
        float target[kNumLayers];
        target[kLayerMetro] = (cur_.mute || (!cur_.metroAudio && tstate_ != kTCountIn)) ? 0.0f : cur_.master;
        target[kLayerPoly] = (cur_.mute || !cur_.polyEnable || !cur_.polyAudio) ? 0.0f : cur_.master;
        target[kLayerPattern] = (cur_.mute || !cur_.patEnable || !cur_.patAudio) ? 0.0f : cur_.master;
        const float kVolts = 5.0f;
        bool contributes[kNumLayers];
        for (int l = 0; l < kNumLayers; ++l) {
            float g = gain_[l], tg = target[l];
            float d = tg - g;
            bool settled = d < 1e-5f && d > -1e-5f;
            contributes[l] = layerActive[l] && (tg > 0.0f || !settled);
            if (!contributes[l]) {
                if (settled) gain_[l] = tg;
                else for (int i = 0; i < n; ++i) g += gainCoef_ * (tg - g), gain_[l] = g;
                continue;
            }
            float* buf = layerBuf_[l];
            if (settled) {
                gain_[l] = tg;
                const float k = tg * kVolts;
                for (int i = 0; i < n; ++i) buf[i] *= k;
            } else {
                for (int i = 0; i < n; ++i) {
                    g += gainCoef_ * (tg - g);
                    buf[i] *= g * kVolts;
                }
                gain_[l] = g;
            }
        }
        for (int l = 0; l < kNumLayers; ++l) {
            float* dst = o.audio[l];
            if (!dst) continue;
            bool primary = true;
            for (int j = 0; j < l; ++j) if (o.audio[j] == dst) { primary = false; break; }
            if (!primary) continue;
            bool any = false;
            for (int k = l; k < kNumLayers; ++k) if (o.audio[k] == dst && contributes[k]) any = true;
            if (!any) {
                if (o.replace[l]) for (int i = 0; i < n; ++i) dst[i] = 0.0f;
                continue;
            }
            bool first = o.replace[l];
            for (int k = l; k < kNumLayers; ++k) {
                if (o.audio[k] != dst || !contributes[k]) continue;
                const float* src = layerBuf_[k];
                if (first) { for (int i = 0; i < n; ++i) dst[i] = src[i]; first = false; }
                else for (int i = 0; i < n; ++i) dst[i] += src[i];
            }
        }

        // CV outputs (outputs sharing a bus are OR-ed)
        for (int k = 0; k < kNumTrigOuts; ++k) {
            float* dst = o.trig[k];
            if (!dst) { trigs_[k].clear(); continue; }
            bool primary = true;
            for (int j = 0; j < k; ++j) if (o.trig[j] == dst) { primary = false; break; }
            int pos = 0;
            Trig& tr = trigs_[k];
            for (int e = 0; e <= nTrg_; ++e) {
                int at = e < nTrg_ ? trg_[e].offset : n;
                if (e < nTrg_ && trg_[e].out != k) continue;
                if (tr.idle()) {
                    if (primary) for (; pos < at; ++pos) dst[pos] = 0.0f;
                    else pos = at;
                } else {
                    for (; pos < at; ++pos) {
                        float v = tr.tick();
                        if (primary || v > dst[pos]) dst[pos] = v;
                    }
                }
                if (e < nTrg_) tr.fire(trg_[e].len, trg_[e].delay);
            }
        }
    }

    // ---------------- settings ----------------
    void configurePoly(bool immediate) {
        Layer& L = layers_[kLayerPoly];
        int x = clampi(cur_.polyX, 1, 16), y = clampi(cur_.polyY, 1, 16);
        L.nextCycleTicks = y * kTicksPerQuarter;
        L.nextTag = polyClockMult(cur_.polyClockRes);
        L.nextN = x * L.nextTag;
        if (immediate) { L.cycleTicks = L.nextCycleTicks; L.n = L.nextN; L.tag = L.nextTag; }
    }
    void configurePattern(bool immediate) {
        Layer& L = layers_[kLayerPattern];
        int idx = clampi(cur_.pattern, 0, kNumPatterns - 1);
        L.nextCycleTicks = kPatterns[idx].cycleTicks;
        L.nextN = kPatterns[idx].steps;
        L.nextTag = idx;
        if (immediate) { L.cycleTicks = L.nextCycleTicks; L.n = L.nextN; L.tag = L.nextTag; }
    }
    void updatePatternSwing() {
        Layer& L = layers_[kLayerPattern];
        int sw = kPatterns[clampi(L.tag, 0, kNumPatterns - 1)].swing ? clampi(cur_.swing, 50, 75) : 50;
        if (sw != L.swing) {
            L.swing = sw;
            if (L.dueValid) { computeDue(L); updateMinDue(); }
        }
    }

    void applySettings(const Settings& s) {
        const bool first = !haveSettings_;
        const bool resync = first || s.source != cur_.source || s.midiMode != cur_.midiMode ||
                            s.cvTransport != cur_.cvTransport;
        const bool polyOn = s.polyEnable && !cur_.polyEnable;
        const bool patOn = s.patEnable && !cur_.patEnable;
        const bool polyCfg = s.polyX != cur_.polyX || s.polyY != cur_.polyY || s.polyClockRes != cur_.polyClockRes;
        const bool patCfg = s.pattern != cur_.pattern;
        const bool playEdge = first ? s.play : (s.play != cur_.play);
        const bool armOn = s.recArm && (first || !cur_.recArm);
        const bool armOff = !first && !s.recArm && cur_.recArm;
        if (nOffs_ && (!s.midiOut || s.midiChannel != cur_.midiChannel)) flushNoteOffs(now_, true);
        cur_ = s;
        haveSettings_ = true;
        const int snd = clampi(s.sound, 0, kNumSounds - 1);
        if (snd != sound_) {  // switch voice set (cuts any ringing tail)
            sound_ = snd;
            for (int i = 0; i < kNumBanks; ++i) banks_[i].setup(kPresets[sound_][i], sr_);
        }
        pendingMeter_ = clampi(s.meter, 0, kNumMeters - 1);
        if (!clicksEnabled()) activeMeter_ = pendingMeter_;

        Layer& P = layers_[kLayerPoly];
        Layer& Q = layers_[kLayerPattern];
        if (first || polyCfg || polyOn) configurePoly(first || !clicksEnabled());
        if (first || patCfg || patOn) configurePattern(first || !clicksEnabled());
        P.on = s.polyEnable;
        Q.on = s.patEnable;
        if (!P.on) { P.dueValid = false; displayPoly_ = -1; }
        if (!Q.on) { Q.dueValid = false; displayStep_ = -1; }
        if (clicksEnabled()) {
            if (polyOn) seekLayer(kLayerPoly, spanT_ + 1);
            if (patOn) seekLayer(kLayerPattern, spanT_ + 1);
        }
        updatePatternSwing();
        updateMinDue();

        // internal tempo (a fresh tap wins until the parameter catches up)
        float bpm = s.internalBpm;
        if (tapOverride_ > 0) {
            float d = bpm - tapOverride_;
            if (d < 0.06f && d > -0.06f) tapOverride_ = 0;
            else bpm = tapOverride_;
        }
        if (bpm < 10.0f) bpm = 10.0f;
        intPeriod_ = 60.0 * (double)sr_ / ((double)bpm * 24.0);

        if (resync) resetSync();
        if (cur_.source == kSourceInternal && playEdge) {
            if (s.play) requestStart(now_);
            else requestStop(now_);
        }
        if (armOn && recState_ == kRecIdle) recState_ = kRecWaiting;  // starts on the next bar line
        if (armOff) {
            if (recState_ == kRecWaiting) recState_ = kRecIdle;
            else if (recState_ == kRecRecording) recState_ = kRecEnding;  // ends on the next bar line
        }
    }

    void resetSync() {
        if (sessionActive_ || recState_ == kRecRecording || recState_ == kRecEnding) transportStopped(now_);
        if (tstate_ == kTCountIn) setLevel(kLvlCountIn, now_, 0.0f);
        tstate_ = kTStopped;
        stopPending_ = false;
        displayCountBars_ = 0;
        running_ = armed_ = lost_ = posKnown_ = pendingReset_ = sessionStartPending_ = false;
        origin_ = barStart_ = 0;
        activeMeter_ = pendingMeter_;
        displayBeat_ = displayPoly_ = displayStep_ = -1;
        lastMetroClickBank_ = -1;
        haveEdge_ = tickPeriodValid_ = false;
        tickPeriod_ = 0;
        cvTick_ = 0;
        lastSpan_ = 1;
        spanT_ = 0;
        spanS_ = 1;
        clearDues();
        cvTimeout_ = initialCvTimeout();
        midiHaveClock_ = false;
        midiTick_ = 0;
        ringCount_ = ringPos_ = 0;
        midiTimeout_ = (int32_t)(0.75f * sr_);
        bpmRaw_ = 0;
        bpmShown_ = 0;
        prime_ = true;
        intActive_ = false;
        for (int k = 0; k < kNumTrigOuts; ++k) trigs_[k].clear();
        fifoRead_ = fifoWrite_;
    }

    // ---------------- internal clock & tap ----------------
    // ---- internal transport: Stopped -> (Count-in) -> Playing -> Stopped ----
    void requestStart(uint32_t t) {
        if (cur_.source != kSourceInternal) return;
        if (tstate_ != kTStopped) { stopPending_ = false; return; }  // play again cancels a pending stop
        intActive_ = true;
        intNext_ = t;
        intFrac_ = 0;
        tickPeriod_ = (float)intPeriod_;
        tickPeriodValid_ = true;
        updateBpm(60.0f * sr_ / (24.0f * tickPeriod_), true);
        stopPending_ = false;
        haveEdge_ = false;
        const int bars = clampi(cur_.countInBars, 0, 4);
        if (bars > 0) {
            tstate_ = kTCountIn;
            countMeter_ = pendingMeter_;
            countBarTicks_ = kMeters[countMeter_].barTicks;
            countTicks_ = bars * countBarTicks_;
            countPos_ = 0;
            displayCountBars_ = bars;
            displayCountBeat_ = -1;
            setLevel(kLvlCountIn, t, 5.0f);
        } else {
            beginPlaying();
        }
    }
    void beginPlaying() {
        tstate_ = kTPlaying;
        running_ = armed_ = posKnown_ = true;  // the next tick is bar 1
        pendingReset_ = false;
    }
    void requestStop(uint32_t t) {
        if (cur_.source != kSourceInternal || tstate_ == kTStopped) return;
        if (tstate_ == kTPlaying && (recState_ == kRecRecording || recState_ == kRecEnding)) {
            stopPending_ = true;  // finish the take on the next bar line
            return;
        }
        stopNow(t);
    }
    void stopNow(uint32_t t) {
        if (tstate_ == kTCountIn) setLevel(kLvlCountIn, t, 0.0f);
        tstate_ = kTStopped;
        stopPending_ = false;
        intActive_ = false;
        running_ = armed_ = lost_ = false;
        haveEdge_ = false;
        clearDues();
        displayBeat_ = displayPoly_ = displayStep_ = -1;
        displayCountBars_ = 0;
        transportStopped(t);
    }
    void internalTick(uint32_t t) {
        // advance the tick clock with a fractional accumulator: no drift
        double adv = intPeriod_ + intFrac_;
        uint32_t whole = (uint32_t)adv;
        intFrac_ = adv - (double)whole;
        intNext_ = t + whole;
        if (tstate_ == kTCountIn) countTick(t);
        else onPulse(t, (float)intPeriod_);
    }
    void countTick(uint32_t t) {
        if (countPos_ >= countTicks_) {  // count finished: this tick is bar 1
            setLevel(kLvlCountIn, t, 0.0f);
            displayCountBars_ = 0;
            beginPlaying();
            onPulse(t, (float)intPeriod_);
            return;
        }
        const Meter& m = kMeters[countMeter_];
        const int32_t rel = countPos_ % countBarTicks_;
        for (int i = 0; i < m.numBeats; ++i) {
            if (m.beat[i] == rel) {
                countClick(t, i == 0);
                displayCountBeat_ = i;
                displayCountBars_ = (countTicks_ - countPos_ + countBarTicks_ - 1) / countBarTicks_;
                break;
            }
        }
        if (cur_.countInClock) {
            if (sendsMidiClock()) pushRealtime(0xF8);
            if (out_.trig[kOutBaseClock]) {
                int32_t bt = baseClockTicks(cur_.baseClockRes);
                if (countPos_ % bt == 0) fireTrig(kOutBaseClock, t, clockLen((float)bt), 0);
            }
        }
        ++countPos_;
    }
    void countClick(uint32_t t, bool accent) {
        const int bank = accent ? kClickCountAccent : kClickCount;
        const float lev = accent ? 0.9f : 0.7f;
#ifdef METRO_ENGINE_TEST
        if (onClick) onClick(onClickCtx, t, bank, countPos_, true);
#endif
        if (cur_.countInMidi && cur_.midiOut) midiNote(bank, t, lev, false);
        if (cur_.mute) return;
        banks_[bank].lastTrigger = t;
        banks_[bank].everTriggered = true;
        if (nSnd_ < kMaxEvents) snd_[nSnd_++] = {(uint16_t)(t - blockStart_), (uint8_t)bank, 0, lev};
    }

    // Any source: the transport has stopped (Stop, RUN low, clock loss, source change).
    void transportStopped(uint32_t t) {
        if (sessionActive_) {
            if (sendsMidiClock()) pushRealtime(0xFC);
            setLevel(kLvlRun, t, 0.0f);
        }
        sessionActive_ = false;
        endRec(t);
    }

    // ---- recording: start / end on bar lines ----
    void recNoteOn(uint32_t t, bool hold) {
        if (cur_.recNote < 0 || !cur_.midiOut) return;
        const uint8_t ch = (uint8_t)(clampi(cur_.midiChannel, 1, 16) - 1);
        const uint8_t note = (uint8_t)clampi(cur_.recNote, 0, 127);
        pushMidi((uint8_t)(0x90 | ch), note, 127);
        if (hold) {
            recNoteHeld_ = true;
            recNoteCh_ = ch;
            recNoteNum_ = note;
        } else if (nOffs_ < kMaxNoteOffs) {
            offs_[nOffs_++] = {t + (uint32_t)(0.015f * sr_), ch, note};
        } else {
            pushMidi((uint8_t)(0x80 | ch), note, 0);
        }
    }
    void startRec(uint32_t t) {
        recState_ = kRecRecording;
        recBars_ = 0;
        if (cur_.recMode == kRecGate) setLevel(kLvlRecGate, t, 5.0f);
        else fireTrig(kOutRecTrig, t, trigLen_, 0);
        recNoteOn(t, cur_.recMode == kRecGate);
    }
    void endRec(uint32_t t) {
        if (recState_ != kRecRecording && recState_ != kRecEnding) return;
        recState_ = kRecIdle;
        if (cur_.recMode == kRecGate) setLevel(kLvlRecGate, t, 0.0f);
        else fireTrig(kOutRecTrig, t, trigLen_, 0);
        if (recNoteHeld_) {
            pushMidi((uint8_t)(0x80 | recNoteCh_), recNoteNum_, 0);
            recNoteHeld_ = false;
        } else if (cur_.recMode == kRecTrigger) {
            recNoteOn(t, false);
        }
        recDisarm_ = true;
    }
    void recBarLine(uint32_t t) {
        if (recState_ == kRecRecording) {
            ++recBars_;
            if (cur_.recLengthBars > 0 && recBars_ >= cur_.recLengthBars) endRec(t);
        } else if (recState_ == kRecEnding) {
            endRec(t);
        }
        if (recState_ == kRecWaiting) startRec(t);
    }

    void tap(uint32_t t) {
        if (tapCount_ > 0) {
            int32_t gap = since(t, tapTimes_[tapCount_ - 1]);
            if (gap < (int32_t)(0.1f * sr_)) return;                 // bounce
            if (gap > (int32_t)(2.5f * sr_)) tapCount_ = 0;          // start over
        }
        if (tapCount_ == 5) {
            for (int i = 0; i < 4; ++i) tapTimes_[i] = tapTimes_[i + 1];
            tapCount_ = 4;
        }
        tapTimes_[tapCount_++] = t;
        if (tapCount_ >= 2) {
            float avg = (float)since(t, tapTimes_[0]) / (float)(tapCount_ - 1);
            float bpm = 60.0f * sr_ / avg;
            if (bpm < 20.0f) bpm = 20.0f;
            if (bpm > 300.0f) bpm = 300.0f;
            bpm = (float)(int32_t)(bpm * 10.0f + 0.5f) * 0.1f;
            tapBpm_ = bpm;
            tapPending_ = true;
            tapOverride_ = bpm;
            intPeriod_ = 60.0 * (double)sr_ / ((double)bpm * 24.0);
        }
    }

    // ---------------- session / position ----------------
    void seekLayer(int l, int32_t tick) {
        Layer& L = layers_[l];
        if (l == kLayerPoly) configurePoly(true);
        if (l == kLayerPattern) { configurePattern(true); updatePatternSwing(); }
        L.seek(tick, origin_);
    }

    void startSessionAt(int32_t tick) {
        origin_ = tick;
        barStart_ = tick;
        activeMeter_ = pendingMeter_;
        sessionStartPending_ = true;
        for (int l = 0; l < kNumLayers; ++l) seekLayer(l, tick);
    }

    void advanceBar(int32_t tick) {
        int32_t len = kMeters[activeMeter_].barTicks;
        int32_t rel = tick - barStart_;
        if (rel < 0 || rel >= 16 * len) {
            int32_t m = rel % len;
            if (m < 0) m += len;
            barStart_ = tick - m;
            return;
        }
        while (rel >= len) {
            barStart_ += len;
            activeMeter_ = pendingMeter_;
            len = kMeters[activeMeter_].barTicks;
            rel = tick - barStart_;
        }
    }

    int32_t clockLen(float ticks) const {
        int32_t len = trigLen_;
        if (tickPeriodValid_) {
            int32_t half = (int32_t)(tickPeriod_ * ticks * 0.5f);
            if (half < len) len = half;
        }
        return len < 1 ? 1 : len;
    }

    void fireTrig(int k, uint32_t t, int32_t len, int32_t delay) {
        if (!out_.trig[k]) return;
        if (relabeling_ && trigs_[k].everFired && trigs_[k].lastFire == relabelEdgeTime_) return;
        trigs_[k].lastFire = t;
        trigs_[k].everFired = true;
        if (nTrg_ < kMaxEvents) trg_[nTrg_++] = {(uint16_t)(t - blockStart_), (uint8_t)k, len, delay};
    }

    // onTime: not a late (reconciled) event. audioOn: the layer's audio switch.
    // MIDI follows onTime and the MIDI layer selection, not the audio switches.
    void playClick(int bank, uint32_t t, int32_t tick, float level, bool onTime, bool audioOn, bool ghost = false) {
        const bool audible = onTime && audioOn;
#ifdef METRO_ENGINE_TEST
        if (onClick) onClick(onClickCtx, t, bank, tick, audible);
#endif
        if (onTime && cur_.midiOut && level > 0.0f && (cur_.midiLayers & (1 << bankLayer(bank)))) midiNote(bank, t, level, ghost);
        if (!audible || cur_.mute || level <= 0.0f) return;
        banks_[bank].lastTrigger = t;
        banks_[bank].everTriggered = true;
        if (nSnd_ < kMaxEvents) snd_[nSnd_++] = {(uint16_t)(t - blockStart_), (uint8_t)bank, 0, level};
    }
    void pushMidi(uint8_t b0, uint8_t b1, uint8_t b2) {
        if (nMidi_ < kMaxMidi) midi_[nMidi_++] = {b0, b1, b2, 3};
    }
    void pushRealtime(uint8_t b) {
        if (nMidi_ < kMaxMidi) midi_[nMidi_++] = {b, 0, 0, 1};
    }
    bool sendsMidiClock() const { return cur_.midiOut && cur_.midiClockOut && cur_.source != kSourceMIDI; }
    void midiNote(int bank, uint32_t t, float level, bool ghost) {
        static const uint8_t voiceOf[kNumBanks] = {kVoiceSub,        kVoiceBeat,    kVoiceDown,     kVoicePoly,
                                                   kVoicePolyAccent, kVoicePattern, kVoicePatAccent,
                                                   kVoiceBeat,       kVoiceDown};
        const bool accent = bank == kClickPolyAccent || bank == kClickPatStrong || bank == kClickCountAccent;
        const uint8_t ch = (uint8_t)(clampi(cur_.midiChannel, 1, 16) - 1);
        const int voice = (ghost && bank == kClickPatWeak) ? (int)kVoicePatGhost : (int)voiceOf[bank];
        const uint8_t note = (uint8_t)clampi(cur_.midiNote[voice], 0, 127);
        int vel = (int)(level * (accent ? 1.43f : 1.0f) * 127.0f + 0.5f);
        vel = clampi(vel, 1, 127);
        for (int i = 0; i < nOffs_; ++i) {  // retrigger: end the previous note first
            if (offs_[i].ch == ch && offs_[i].note == note) {
                pushMidi((uint8_t)(0x80 | ch), note, 0);
                offs_[i] = offs_[--nOffs_];
                break;
            }
        }
        pushMidi((uint8_t)(0x90 | ch), note, (uint8_t)vel);
        if (nOffs_ < kMaxNoteOffs) offs_[nOffs_++] = {t + (uint32_t)(0.015f * sr_), ch, note};
        else pushMidi((uint8_t)(0x80 | ch), note, 0);
    }
    void flushNoteOffs(uint32_t until, bool all) {
        for (int i = 0; i < nOffs_;) {
            if (all || since(until, offs_[i].due) >= 0) {
                pushMidi((uint8_t)(0x80 | offs_[i].ch), offs_[i].note, 0);
                offs_[i] = offs_[--nOffs_];
            } else {
                ++i;
            }
        }
    }

    void chokeBank(int bank, uint32_t t) {
        if (nSnd_ < kMaxEvents) snd_[nSnd_++] = {(uint16_t)(t - blockStart_), (uint8_t)bank, 1, 0.0f};
    }

    void fireStartResets(uint32_t t) {
        if (!sessionStartPending_) return;
        sessionStartPending_ = false;
        if (sendsMidiClock()) {
            if (sessionActive_) pushRealtime(0xFC);  // restart (reset while playing): Stop, then Start
            pushRealtime(0xFA);
        }
        sessionActive_ = true;
        setLevel(kLvlRun, t, 5.0f);
        if (cur_.baseResetMode == kResetStart) fireTrig(kOutBaseReset, t, trigLen_, 0);
        if (cur_.polyEnable && cur_.polyResetMode == kResetStart) fireTrig(kOutPolyReset, t, trigLen_, 0);
        if (cur_.patEnable && cur_.patResetMode == kResetStart) fireTrig(kOutPatReset, t, trigLen_, 0);
    }

    void metroEvent(int32_t tick, uint32_t t, bool audible) {
        if (sendsMidiClock()) pushRealtime(0xF8);
        advanceBar(tick);
        const Meter& m = kMeters[activeMeter_];
        const int32_t rel = tick - barStart_;
        if (rel == 0 && posKnown_ && !relabeling_) recBarLine(t);
        int beatIdx = -1;
        for (int i = 0; i < m.numBeats; ++i)
            if (m.beat[i] == rel) { beatIdx = i; break; }
        int bank = -1;
        if (beatIdx >= 0) {
            bank = (beatIdx == 0 && posKnown_) ? kClickDown : kClickBeat;
            displayBeat_ = posKnown_ ? beatIdx : -1;
        } else {
            int32_t d = divisionTicks(cur_.division);
            if (d > 0 && rel % d == 0) bank = kClickSub;
        }
        if (bank >= 0) {
            float lev = bank == kClickDown ? cur_.levelDown : (bank == kClickBeat ? cur_.levelBeat : cur_.levelSub);
            playClick(bank, t, tick, lev, audible && !suppressMetroAudio_, cur_.metroAudio);
            if (!relabeling_) {
                lastMetroClickTick_ = tick;
                lastMetroClickBank_ = bank;
            }
        }
        if (out_.trig[kOutBaseClock]) {
            int32_t bt = baseClockTicks(cur_.baseClockRes);
            int32_t r = (tick - origin_) % bt;
            if (r < 0) r += bt;
            if (r == 0) fireTrig(kOutBaseClock, t, clockLen((float)bt), out_.trig[kOutBaseReset] ? resetLead_ : 0);
        }
        if (cur_.baseResetMode == kResetCycle && rel == 0 && posKnown_) fireTrig(kOutBaseReset, t, trigLen_, 0);
    }

    void polyEvent(const Layer& L, uint32_t t, bool audible) {
        const int32_t mult = L.tag > 0 ? L.tag : 1;
        const bool cycleStart = L.idx == 0;
        if (L.idx % mult == 0) {
            displayPoly_ = L.idx / mult;
            bool acc = cycleStart && cur_.polyAccent;
            playClick(acc ? kClickPolyAccent : kClickPoly, t, L.cycleStart, cur_.polyLevel, audible, cur_.polyAudio);
        }
        fireTrig(kOutPolyClock, t, clockLen((float)L.cycleTicks / (float)L.n),
                 out_.trig[kOutPolyReset] ? resetLead_ : 0);
        if (cycleStart && cur_.polyResetMode == kResetCycle) fireTrig(kOutPolyReset, t, trigLen_, 0);
    }

    void patternEvent(const Layer& L, uint32_t t, bool audible) {
        const PatternDef& p = kPatterns[clampi(L.tag, 0, kNumPatterns - 1)];
        const int step = L.idx;
        displayStep_ = step;
        const int32_t lead = out_.trig[kOutPatReset] ? resetLead_ : 0;
        if (step == 0 && cur_.patResetMode == kResetCycle) fireTrig(kOutPatReset, t, trigLen_, 0);
        const uint32_t bit = 1u << step;
        if (step < 16 && (p.onsets & bit)) {
            const bool accent = (p.accents & bit) != 0;
            const bool ghost = (p.ghosts & bit) != 0;
            const bool strong = accent && cur_.patAccent;
            float lev = cur_.patLevel * (ghost ? 0.4f : 1.0f);
            playClick(strong ? kClickPatStrong : kClickPatWeak, t, L.cycleStart, lev, audible, cur_.patAudio, ghost);
            // velocity CV steps at the hit (before any delayed trigger): accent / hit / ghost
            if (out_.level[kLvlVelocity]) setLevel(kLvlVelocity, t, accent ? 5.0f : (ghost ? 1.5f : 3.5f));
            if (!(ghost && cur_.patTrigMode == kTrigNoGhosts)) fireTrig(kOutPatTrig, t, trigLen_, lead);
            if (accent) fireTrig(kOutPatAccent, t, trigLen_, lead);
            if (ghost) fireTrig(kOutPatGhost, t, trigLen_, lead);
        }
    }

    void fireLayer(int l, uint32_t t, bool audible) {
        const Layer& L = layers_[l];
        if (l == kLayerMetro) metroEvent(L.cycleStart + L.idx, t, audible);
        else if (l == kLayerPoly) polyEvent(L, t, audible);
        else patternEvent(L, t, audible);
    }

    void clearDues() {
        for (int l = 0; l < kNumLayers; ++l) layers_[l].dueValid = false;
        anyDue_ = false;
    }
    void updateMinDue() {
        anyDue_ = false;
        for (int l = 0; l < kNumLayers; ++l) {
            const Layer& L = layers_[l];
            if (!L.on || !L.dueValid) continue;
            if (!anyDue_ || since(L.due, minDue_) < 0) minDue_ = L.due;
            anyDue_ = true;
        }
    }
    void computeDue(Layer& L) {
        L.dueValid = false;
        if (!L.on || !tickPeriodValid_) return;
        int32_t nm = L.num(spanT_);
        int32_t dn = L.den();
        if (nm < 0 || nm >= spanS_ * dn) return;
        L.due = spanTime_ + (uint32_t)((float)nm / (float)dn * tickPeriod_ + 0.5f);
        L.dueValid = true;
    }

    // A real clock event: tick T happened at sample tFire. Interpolated events
    // until the next one (S ticks later) are timed from spanTime.
    void clockEvent(int32_t T, uint32_t tFire, uint32_t spanTime, int32_t S) {
        if (!clicksEnabled()) return;
        if (stopPending_) {
            advanceBar(T);
            if (T == barStart_) { stopNow(tFire); return; }  // quantized stop: end exactly on the bar line
        }
        fireStartResets(tFire);
        for (int l = 0; l < kNumLayers; ++l) {
            Layer& L = layers_[l];
            if (!L.on) continue;
            if (L.num(T) < -kMaxFlushTicks * L.den()) seekLayer(l, T);
            int guard = 0;
            while (L.num(T) < 0 && guard++ < 4096) {
                fireLayer(l, tFire, L.num(T) > -L.den());  // late by < 1 tick: still audible
                L.advance();
            }
            while (L.num(T) == 0 && guard++ < 4096) {
                fireLayer(l, tFire, true);
                L.advance();
            }
        }
        spanT_ = T;
        spanTime_ = spanTime;
        spanS_ = S;
        for (int l = 0; l < kNumLayers; ++l) computeDue(layers_[l]);
        updateMinDue();
    }

    void runScheduled(uint32_t t) {
        if (!clicksEnabled()) { anyDue_ = false; return; }
        for (int l = 0; l < kNumLayers; ++l) {
            Layer& L = layers_[l];
            int guard = 0;
            while (L.on && L.dueValid && since(t, L.due) >= 0 && guard++ < 16) {
                fireLayer(l, t, true);
                L.advance();
                computeDue(L);
            }
        }
        updateMinDue();
    }

    void relabelLastEdge(uint32_t t) {
        const bool metroWasDown = lastMetroClickBank_ == kClickDown && lastMetroClickTick_ == lastEdgeTick_;
        for (int b = 0; b < kNumBanks; ++b) {
            if (metroWasDown && b <= kClickDown) continue;
            if (banks_[b].everTriggered && banks_[b].lastTrigger == lastEdgeTime_) chokeBank(b, t);
        }
        posKnown_ = true;
        startSessionAt(lastEdgeTick_);
        relabeling_ = true;
        relabelEdgeTime_ = lastEdgeTime_;
        suppressMetroAudio_ = metroWasDown;
        clockEvent(lastEdgeTick_, t, lastEdgeTime_, lastSpan_);
        relabeling_ = false;
        suppressMetroAudio_ = false;
        displayBeat_ = 0;
    }

    int32_t window() const {
        int32_t w = window_;
        if (tickPeriodValid_) {
            int32_t p = (int32_t)(tickPeriod_ * (float)lastSpan_) / 3;
            if (p < w) w = p;
        }
        return w;
    }

    // ---------------- CV / internal pulses ----------------
    int32_t initialCvTimeout() const {
        static const float sec[kNumCvRes] = {1.0f, 2.0f, 3.0f, 4.0f, 20.0f};
        return (int32_t)(sec[clampi(cur_.cvResolution, 0, kNumCvRes - 1)] * sr_);
    }

    void onTransportRise(uint32_t t) {
        const int mode = cur_.cvTransport;
        if (cur_.source == kSourceInternal) {
            if (mode == kCvRun) requestStart(t);
            else if (mode == kCvToggle) {
                if (tstate_ == kTStopped || stopPending_) requestStart(t);
                else requestStop(t);
            } else if (mode == kCvReset) onResetTrigger(t);
            return;
        }
        if (mode == kCvRun) onRunRise(t);
        else if (mode == kCvToggle) { if (running_) onRunFall(t); else onRunRise(t); }
        else if (mode == kCvReset) onResetTrigger(t);
    }
    void onTransportFall(uint32_t t) {
        if (cur_.cvTransport != kCvRun) return;
        if (cur_.source == kSourceInternal) requestStop(t);
        else onRunFall(t);
    }

    void onRunRise(uint32_t t) {
        running_ = true;
        armed_ = true;
        lost_ = false;
        posKnown_ = true;
        pendingReset_ = false;
        if (haveEdge_ && since(t, lastEdgeTime_) <= window()) {
            armed_ = false;
            relabelLastEdge(t);
        }
    }

    void onRunFall(uint32_t t) {
        running_ = armed_ = lost_ = false;
        displayBeat_ = displayPoly_ = displayStep_ = -1;
        clearDues();
        transportStopped(t);
    }

    void onResetTrigger(uint32_t t) {
        if (running_ && haveEdge_ && since(t, lastEdgeTime_) <= window()) {
            pendingReset_ = false;
            relabelLastEdge(t);
        } else {
            pendingReset_ = true;
        }
    }

    void onCvEdge(uint32_t t) {
        if (haveEdge_) {
            int32_t iv = since(t, lastEdgeTime_);
            if (iv < minInterval_) return;
            float tp = (float)iv / (float)lastSpan_;
            int32_t to = iv * 2 + iv / 2;
            int32_t lo = (int32_t)(0.02f * sr_), hi = (int32_t)(30.0f * sr_);
            cvTimeout_ = to < lo ? lo : (to > hi ? hi : to);
            onPulse(t, tp);
        } else {
            onPulse(t, 0.0f);
        }
    }

    // One clock pulse from CV or the internal clock. tickPeriod <= 0: unknown yet.
    void onPulse(uint32_t t, float tickPeriod) {
        if (tickPeriod > 0) {
            tickPeriod_ = tickPeriod;
            tickPeriodValid_ = true;
            updateBpm(60.0f * sr_ / (24.0f * tickPeriod_), cur_.source == kSourceInternal);
        }
        haveEdge_ = true;
        lost_ = false;
        lastEdgeTime_ = t;

        const int32_t tick = cvTick_;
        lastEdgeTick_ = tick;
        bool startNow = false;
        if (cur_.source == kSourceInternal) {
            if (running_ && armed_) { armed_ = false; startNow = true; }
            else if (running_ && pendingReset_) { pendingReset_ = false; startNow = true; }
        } else if (cur_.cvTransport == kCvRun || cur_.cvTransport == kCvToggle) {
            if (running_ && armed_) {
                armed_ = false;
                startNow = true;
            }
        } else if (!running_ || pendingReset_) {
            running_ = true;
            posKnown_ = true;
            pendingReset_ = false;
            startNow = true;
        }
        if (startNow) startSessionAt(tick);

        int32_t S;
        if (cur_.source == kSourceInternal) {
            S = 1;
        } else if (cur_.cvResolution == kCvBar) {
            if (clicksEnabled()) advanceBar(tick);
            S = kMeters[clicksEnabled() ? activeMeter_ : pendingMeter_].barTicks;
            if (clicksEnabled() && tick != barStart_) S = barStart_ + S - tick;
        } else {
            S = cvResolutionTicks(cur_.cvResolution, activeMeter_);
        }
        clockEvent(tick, t, t, S);
        lastSpan_ = S;
        cvTick_ = tick + S;
    }

    void onCvLost(uint32_t t) {
        haveEdge_ = tickPeriodValid_ = false;
        clearDues();
        cvTimeout_ = initialCvTimeout();
        bpmRaw_ = 0;
        bpmShown_ = 0;
        if (running_) lost_ = true;
        if (cur_.cvTransport != kCvRun && cur_.cvTransport != kCvToggle) {
            if (running_) transportStopped(t);
            running_ = false;
            displayBeat_ = displayPoly_ = displayStep_ = -1;
        }
    }

    // ---------------- MIDI ----------------
    void handleMidi(uint32_t e, uint32_t t) {
        const bool transport = cur_.midiMode == kMidiTransport;
        switch (e & 0xFF) {
            case 0xF8: onMidiClock(t); break;
            case 0xFA:
                if (!transport) break;
                running_ = armed_ = posKnown_ = true;
                lost_ = false;
                midiTick_ = 0;
                startSessionAt(0);
                break;
            case 0xFB:
                if (!transport) break;
                running_ = true;
                armed_ = lost_ = false;
                break;
            case 0xFC:
                if (!transport) break;
                running_ = armed_ = lost_ = false;
                displayBeat_ = displayPoly_ = displayStep_ = -1;
                clearDues();
                transportStopped(t);
                break;
            case 0xF2: {
                int32_t spp = (int32_t)(((e >> 8) & 0x7F) | (((e >> 16) & 0x7F) << 7));
                midiTick_ = spp * 6;
                origin_ = 0;
                activeMeter_ = pendingMeter_;
                int32_t len = kMeters[activeMeter_].barTicks;
                barStart_ = midiTick_ - (midiTick_ % len);
                posKnown_ = true;
                for (int l = 0; l < kNumLayers; ++l) seekLayer(l, midiTick_);
                break;
            }
        }
    }

    void onMidiClock(uint32_t t) {
        ring_[ringPos_] = t;
        ringPos_ = (ringPos_ + 1) % kRing;
        if (ringCount_ < kRing) ++ringCount_;
        if (ringCount_ >= 3) {
            int nInt = ringCount_ - 1;
            uint32_t oldest = ring_[(ringPos_ + kRing - ringCount_) % kRing];
            int32_t dt = since(t, oldest);
            if (dt > 0) {
                int nShort = nInt < 6 ? nInt : 6;
                uint32_t recent = ring_[(ringPos_ + kRing - 1 - nShort) % kRing];
                int32_t dts = since(t, recent);
                tickPeriod_ = dts > 0 ? (float)dts / (float)nShort : (float)dt / (float)nInt;
                tickPeriodValid_ = true;
                updateBpm(60.0f * sr_ * (float)nInt / (24.0f * (float)dt), false);
                int32_t to = (int32_t)(tickPeriod_ * 4.0f);
                int32_t lo = (int32_t)(0.05f * sr_), hi = (int32_t)(3.0f * sr_);
                midiTimeout_ = to < lo ? lo : (to > hi ? hi : to);
            }
        }
        midiHaveClock_ = true;
        midiLastClock_ = t;
        lost_ = false;

        if (cur_.midiMode == kMidiTransport) {
            if (!running_) return;
            armed_ = false;
        } else if (!running_) {
            running_ = posKnown_ = true;
            midiTick_ = 0;
            startSessionAt(0);
        }
        const int32_t tick = midiTick_++;
        clockEvent(tick, t, t, 1);
    }

    void checkMidiTimeout(uint32_t t) {
        if (!midiHaveClock_ || since(t, midiLastClock_) <= midiTimeout_) return;
        midiHaveClock_ = tickPeriodValid_ = false;
        clearDues();
        ringCount_ = ringPos_ = 0;
        midiTimeout_ = (int32_t)(0.75f * sr_);
        bpmRaw_ = 0;
        bpmShown_ = 0;
        if (running_) lost_ = true;
        if (cur_.midiMode == kMidiClockOnly) {
            if (running_) transportStopped(t);
            running_ = false;
            displayBeat_ = displayPoly_ = displayStep_ = -1;
        }
    }

    // ---------------- tempo / status ----------------
    void updateBpm(float inst, bool snap) {
        if (inst <= 0) return;
        if (snap || bpmRaw_ <= 0 || inst > bpmRaw_ * 1.04f || inst < bpmRaw_ * 0.96f) bpmRaw_ = inst;
        else bpmRaw_ += 0.1f * (inst - bpmRaw_);
        float shown = bpmShown_;
        float d = bpmRaw_ - shown;
        if (snap || shown <= 0 || d > 0.15f || d < -0.15f)
            bpmShown_ = (float)(int32_t)(bpmRaw_ * 10.0f + 0.5f) * 0.1f;
    }

    void updateStatus() {
        int s;
        if (cur_.source == kSourceInternal) {
            s = tstate_ == kTCountIn ? kStatusCountIn : (tstate_ == kTPlaying ? kStatusRunning : kStatusStopped);
        } else if (pulseSource()) {
            if (cur_.cvTransport == kCvRun || cur_.cvTransport == kCvToggle) {
                if (cur_.cvTransport == kCvRun ? !trHigh_ : !running_) s = kStatusStopped;
                else if (lost_) s = kStatusLost;
                else if (armed_ || !haveEdge_) s = kStatusWaiting;
                else if (!tickPeriodValid_ || !posKnown_) s = kStatusAcquiring;
                else s = kStatusRunning;
            } else {
                if (lost_ && !haveEdge_) s = kStatusLost;
                else if (!running_) s = kStatusWaiting;
                else if (!tickPeriodValid_) s = kStatusAcquiring;
                else s = kStatusRunning;
            }
        } else if (cur_.midiMode == kMidiTransport) {
            if (!running_) s = kStatusStopped;
            else if (lost_) s = kStatusLost;
            else if (armed_ || !midiHaveClock_) s = kStatusWaiting;
            else if (!posKnown_ || ringCount_ < 3) s = kStatusAcquiring;
            else s = kStatusRunning;
        } else {
            if (lost_ && !running_) s = kStatusLost;
            else if (!running_) s = kStatusWaiting;
            else if (ringCount_ < 3) s = kStatusAcquiring;
            else s = kStatusRunning;
        }
        status_ = s;
    }
};

}  // namespace
}  // namespace metro
