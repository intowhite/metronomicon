// Host-side test suite for the metronome engine (v1_2).
// Simulates CV clock/transport signals and MIDI byte streams sample-by-sample and
// checks the PRD + polyrhythm/pattern acceptance criteria that can be verified
// without hardware.
//
//   make test

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "metronome_engine.h"

using namespace metro;

static const double SR = 48000.0;
static int g_fail = 0, g_pass = 0;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        if (cond) { ++g_pass; }                                              \
        else { ++g_fail; std::printf("    FAIL %s:%d: ", __FILE__, __LINE__); \
               std::printf(__VA_ARGS__); std::printf("\n"); }                \
    } while (0)

struct Click {
    uint32_t t;
    int type;  // bank
    int32_t tick;
    bool audible;
};
struct Interval { uint64_t start, end; };
struct MidiEv { double t; uint8_t b0, b1, b2; bool msg; };
struct MidiOutEv { uint64_t t; uint8_t b0, b1, b2, len; };

struct Sim {
    Engine e;
    Settings s;
    int block = 24;
    uint64_t now = 0;
    std::vector<Click> all;
    std::vector<Interval> clock, gate, taps;
    std::vector<MidiEv> midi;
    size_t ci = 0, gi = 0, mi = 0, ti = 0;
    double peak = 0;
    bool logOut = false;
    std::vector<float> outLog;                       // audio bus A (metro default)
    std::vector<MidiOutEv> midiOut;
    bool logVel = false;
    std::vector<float> velLog;
    bool logLevel[kNumLevels] = {};
    std::vector<float> levelLog[kNumLevels];
    bool trigEnabled[kNumTrigOuts] = {};
    std::vector<float> trigLog[kNumTrigOuts];
    int audioBus[kNumLayers] = {0, 0, 0};            // all layers -> bus A by default
    bool replace[kNumLayers] = {true, false, false};

    explicit Sim(int blk = 24) : block(blk) {
        e.init((float)SR);
        e.onClick = [](void* ctx, uint32_t t, int type, int32_t tick, bool audible) {
            ((Sim*)ctx)->all.push_back({t, type, tick, audible});
        };
        e.onClickCtx = this;
        s.cvResolution = kCv4ppqn;
        s.cvTransport = kCvRun;  // most tests drive a RUN gate
    }
    void addPulses(double start, double period, int count) {
        for (int i = 0; i < count; ++i) {
            double t = start + i * period;
            uint64_t a = (uint64_t)std::llround(t);
            uint64_t w = (uint64_t)std::min(240.0, period / 2);
            clock.push_back({a, a + w});
        }
    }
    void addMidi(double t, uint8_t b) { midi.push_back({t, b, 0, 0, false}); }
    void addSpp(double t, int spp) { midi.push_back({t, 0xF2, (uint8_t)(spp & 0x7F), (uint8_t)(spp >> 7), true}); }
    void addMidiClocks(double start, double bpm, int count) {
        double p = SR * 60.0 / (bpm * 24.0);
        for (int i = 0; i < count; ++i) addMidi(start + i * p, 0xF8);
    }
    static float level(std::vector<Interval>& v, size_t& idx, uint64_t t) {
        while (idx < v.size() && v[idx].end <= t) ++idx;
        return (idx < v.size() && v[idx].start <= t) ? 5.0f : 0.0f;
    }
    void runUntil(uint64_t until) {
        std::vector<float> c(block), g(block), tp(block), vb(block), bus[2], tb[kNumTrigOuts];
        bus[0].resize(block);
        bus[1].resize(block);
        for (int k = 0; k < kNumTrigOuts; ++k) tb[k].resize(block);
        if (mi == 0)
            std::stable_sort(midi.begin(), midi.end(), [](const MidiEv& a, const MidiEv& b) { return a.t < b.t; });
        while (now < until) {
            while (mi < midi.size() && midi[mi].t <= (double)now) {
                const MidiEv& m = midi[mi++];
                if (m.msg) e.midiMessage(m.b0, m.b1, m.b2);
                else e.midiRealtime(m.b0);
            }
            for (int i = 0; i < block; ++i) {
                c[i] = level(clock, ci, now + i);
                g[i] = level(gate, gi, now + i);
                tp[i] = level(taps, ti, now + i);
                bus[0][i] = bus[1][i] = 0;
            }
            Outputs o;
            for (int l = 0; l < kNumLayers; ++l) {
                o.audio[l] = bus[audioBus[l]].data();
                o.replace[l] = replace[l];
            }
            for (int k = 0; k < kNumTrigOuts; ++k) o.trig[k] = trigEnabled[k] ? tb[k].data() : nullptr;
            o.level[kLvlVelocity] = logVel ? vb.data() : nullptr;
            static std::vector<float> lb[kNumLevels];
            for (int k = 1; k < kNumLevels; ++k) { lb[k].resize(block); o.level[k] = logLevel[k] ? lb[k].data() : nullptr; }
            e.process(s, c.data(), g.data(), o, block, taps.empty() ? nullptr : tp.data());
            for (int k = 0; k < e.midiCount(); ++k)
                midiOut.push_back({now, e.midiMessages()[k].b0, e.midiMessages()[k].b1, e.midiMessages()[k].b2,
                                   e.midiMessages()[k].len});
            if (logVel) velLog.insert(velLog.end(), vb.begin(), vb.end());
            for (int k = 1; k < kNumLevels; ++k)
                if (logLevel[k]) levelLog[k].insert(levelLog[k].end(), o.level[k], o.level[k] + block);
            for (int i = 0; i < block; ++i) {
                double a = std::fabs(bus[0][i]);
                if (!(bus[0][i] == bus[0][i])) peak = 1e9;
                if (a > peak) peak = a;
                if (logOut) outLog.push_back(bus[0][i]);
                for (int k = 0; k < kNumTrigOuts; ++k)
                    if (trigEnabled[k]) trigLog[k].push_back(tb[k][i]);
            }
            now += block;
        }
    }
    // Audible clicks of one layer
    std::vector<Click> layer(int l) const {
        std::vector<Click> r;
        for (auto& c : all) {
            int cl = c.type <= kClickDown ? kLayerMetro : (c.type <= kClickPolyAccent ? kLayerPoly : kLayerPattern);
            if (cl == l && c.audible) r.push_back(c);
        }
        return r;
    }
    std::vector<Click> metro() const { return layer(kLayerMetro); }
    // rising edges of a trigger output
    std::vector<uint64_t> edges(int k) const {
        std::vector<uint64_t> r;
        const std::vector<float>& v = trigLog[k];
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i] > 2.5f && (i == 0 || v[i - 1] < 2.5f)) r.push_back(i);
        return r;
    }
};

static int patternIndex(const char* name);

static const char* typeName(int t) {
    static const char* n[] = {"s", "B", "D", "p", "P", "w", "W"};
    return (t >= 0 && t < kNumBanks) ? n[t] : "?";
}
static void dumpClicks(const std::vector<Click>& c, size_t n = 16) {
    std::printf("      ");
    for (size_t i = 0; i < c.size() && i < n; ++i) std::printf("%s@%u ", typeName(c[i].type), c[i].t);
    std::printf("\n");
}

// ================================================================== CV (v1 behaviour)

static void testCvRunBasic(int division, int expectInterval, const char* label) {
    std::printf("  CV RUN, 4 PPQN clock, 120 BPM, %s clicks\n", label);
    Sim sim;
    sim.s.division = division;
    sim.gate.push_back({10000, 10000 + (uint64_t)(SR * 60)});
    sim.addPulses(12000, 6000, 4 * 4 * 25);
    sim.runUntil(12000 + 6000 * 4 * 4 * 25);
    auto c = sim.metro();
    CHECK(!c.empty() && c[0].t == 12000 && c[0].type == kClickDown, "first click must be downbeat at first clock edge");
    bool ok = true;
    for (size_t i = 1; i < c.size(); ++i)
        if ((int)(c[i].t - c[i - 1].t) != expectInterval) { ok = false; break; }
    CHECK(ok, "every interval must be exactly %d samples (%.1f ms)", expectInterval, expectInterval / 48.0);
    int perBeat = 24000 / expectInterval, perBar = 4 * perBeat;
    bool acc = true;
    for (size_t i = 0; i < c.size(); ++i) {
        int pos = (int)(i % perBar);
        int want = pos == 0 ? kClickDown : (pos % perBeat == 0 ? kClickBeat : kClickSub);
        if (c[i].type != want) { acc = false; break; }
    }
    CHECK(acc, "accent pattern ONE (e & a) two ... must be correct");
    CHECK(sim.e.status() == kStatusRunning, "status RUNNING (got %d)", sim.e.status());
    CHECK(std::fabs(sim.e.bpm() - 120.0f) < 0.05f, "BPM display 120.0 (got %.2f)", sim.e.bpm());
}

static void testCvInterpolationDrift(double bpm, int res, int division, double minutes) {
    std::printf("  CV %s clock -> %s clicks at %.2f BPM for %.0f min: interpolation & drift\n",
                res == kCv1ppqn ? "1 PPQN" : "2 PPQN", division == kDiv16 ? "1/16" : "1/8", bpm, minutes);
    Sim sim(32);
    sim.s.cvResolution = res;
    sim.s.division = division;
    sim.s.cvTransport = kCvNone;
    double quarter = SR * 60.0 / bpm;
    double pulse = quarter * cvResolutionTicks(res, 0) / 24.0;
    int n = (int)(minutes * 60.0 * SR / pulse);
    double start = 5000.3;
    sim.addPulses(start, pulse, n);
    sim.runUntil((uint64_t)(start + pulse * n));
    double clickStep = quarter * divisionTicks(division) / 24.0;
    double maxErr = 0, firstErr = 0, lastErr = 0;
    size_t count = 0;
    for (auto& c : sim.metro()) {
        double k = std::floor((c.t - start) / clickStep + 0.5);
        double err = c.t - (start + k * clickStep);
        if (count == 0) firstErr = err;
        lastErr = err;
        maxErr = std::max(maxErr, std::fabs(err));
        ++count;
    }
    int perPulse = cvResolutionTicks(res, 0) / divisionTicks(division);
    size_t expected = (size_t)(n - 1) * perPulse + 1;
    CHECK(count >= expected - perPulse && count <= (size_t)n * perPulse, "click count %zu, expected ~%zu", count, expected);
    CHECK(maxErr <= 1.6, "max click error %.2f samples", maxErr);
    CHECK(std::fabs(lastErr - firstErr) <= 2.0, "no drift: first err %.2f, last err %.2f samples", firstErr, lastErr);
}

static void testTempoRamp(double from, double to) {
    std::printf("  CV 1 PPQN clock, 1/16 clicks, tempo ramp %.0f -> %.0f BPM over 40 s\n", from, to);
    Sim sim;
    sim.s.cvResolution = kCv1ppqn;
    sim.s.division = kDiv16;
    sim.s.cvTransport = kCvNone;
    double t = 1000;
    int pulses = 0;
    while (t < SR * 40) {
        double bpm = from + (to - from) * (t / (SR * 40));
        double p = SR * 60.0 / bpm;
        sim.addPulses(t, p, 1);
        t += p;
        ++pulses;
    }
    sim.runUntil((uint64_t)t);
    auto c = sim.metro();
    double nomMin = SR * 60.0 / std::max(from, to) / 4, nomMax = SR * 60.0 / std::min(from, to) / 4;
    bool ok = true;
    double minI = 1e9, maxI = 0;
    for (size_t i = 2; i < c.size(); ++i) {
        double d = c[i].t - c[i - 1].t;
        minI = std::min(minI, d);
        maxI = std::max(maxI, d);
        if (d < nomMin * 0.8 || d > nomMax * 1.2) ok = false;
    }
    CHECK(ok, "16th intervals within tempo range (min %.0f, max %.0f)", minI, maxI);
    CHECK((int)c.size() == (pulses - 1) * 4 + 1, "click count %zu == %d", c.size(), (pulses - 1) * 4 + 1);
    bool ticksOk = true;
    for (size_t i = 2; i < c.size(); ++i) if (c[i].tick != c[i - 1].tick + 6) ticksOk = false;
    CHECK(ticksOk, "every 1/16 grid position sounds exactly once");
}

static void testRunCoincidence(int gateOffset, const char* label) {
    std::printf("  RUN gate %s clock edge\n", label);
    Sim sim;
    uint64_t edge = 20000;
    sim.addPulses(2000, 6000, 3 + 64);
    sim.gate.push_back({(uint64_t)((int64_t)edge + gateOffset), edge + 6000 * 40});
    sim.runUntil(edge + 6000 * 32 + 100);
    auto c = sim.metro();
    int downs = 0;
    for (auto& k : c) if (k.type == kClickDown && k.t < edge + 1000) ++downs;
    CHECK(downs == 1, "exactly one downbeat around play (got %d)", downs);
    bool anyEarly = false;
    for (auto& k : c) if (k.t < edge - 300) anyEarly = true;
    CHECK(!anyEarly, "no clicks before play");
    CHECK(!c.empty() && c[0].type == kClickDown && c[0].t >= edge && c[0].t <= edge + 200, "downbeat within 4 ms of play");
    bool grid = c.size() >= 3 && c[1].t == edge + 24000 && c[2].t == edge + 48000;
    CHECK(grid, "following beats locked to the clock grid");
    if (!grid) dumpClicks(c);
}

static void testRunStopRestart() {
    std::printf("  RUN stop is immediate; restart begins at beat 1\n");
    Sim sim;
    sim.addPulses(0, 6000, 200);
    sim.gate.push_back({1, 6000 * 22 + 3000});
    sim.gate.push_back({6000 * 60 - 10, 6000 * 199});
    sim.runUntil(6000 * 100);
    bool none = true;
    for (auto& k : sim.metro()) if (k.t > 6000 * 22 + 3000 && k.t < 6000 * 60 - 10) none = false;
    CHECK(none, "no clicks while RUN is low");
    Click first{};
    for (auto& k : sim.metro()) if (k.t >= 6000 * 60 - 10) { first = k; break; }
    CHECK(first.type == kClickDown && first.t == 6000 * 60, "restart downbeat on first clock after gate");
}

static void testResetMode() {
    std::printf("  RESET mode: coincident, slightly late and mid-interval resets\n");
    {
        Sim sim;
        sim.s.cvTransport = kCvReset;
        sim.addPulses(0, 6000, 200);
        uint64_t r = 6000 * 8;
        sim.gate.push_back({r, r + 240});
        sim.runUntil(6000 * 40);
        int downsAt = 0, others = 0;
        for (auto& k : sim.metro())
            if (k.t >= r && k.t < r + 1000) { if (k.type == kClickDown) ++downsAt; else ++others; }
        CHECK(downsAt == 1 && others == 0, "coincident reset: one downbeat, nothing else (%d/%d)", downsAt, others);
        bool next = false;
        for (auto& k : sim.metro()) if (k.t == r + 24000 * 4) next = k.type == kClickDown;
        CHECK(next, "bar counting restarts from the reset");
    }
    {
        Sim sim;
        sim.s.cvTransport = kCvReset;
        sim.addPulses(0, 6000, 200);
        uint64_t edge = 6000 * 8, r = edge + 48;
        sim.gate.push_back({r, r + 240});
        sim.runUntil(6000 * 40);
        int downs = 0;
        for (auto& k : sim.metro()) if (k.t >= edge && k.t < edge + 1000 && k.type == kClickDown) ++downs;
        CHECK(downs == 1, "late reset: exactly one downbeat (%d)", downs);
        bool next = false;
        for (auto& k : sim.metro()) if (k.t == edge + 24000 * 4) next = k.type == kClickDown;
        CHECK(next, "late reset: bar anchored on the clock edge");
    }
    {
        Sim sim;
        sim.s.cvTransport = kCvReset;
        sim.addPulses(0, 6000, 200);
        uint64_t r = 6000 * 9 + 2500;
        sim.gate.push_back({r, r + 240});
        sim.runUntil(6000 * 40);
        Click nxt{};
        for (auto& k : sim.metro()) if (k.t > r) { nxt = k; break; }
        CHECK(nxt.type == kClickDown && nxt.t == 6000 * 10, "next clock after reset is the downbeat");
    }
}

static void testNoneModeAndClockLoss() {
    std::printf("  NONE mode: first pulse = beat 1, clock loss stops, resume restarts\n");
    Sim sim;
    sim.s.cvTransport = kCvNone;
    sim.addPulses(1000, 6000, 48);
    sim.addPulses(1000 + 6000 * 48 + SR * 2, 6000, 32);
    sim.runUntil((uint64_t)(1000 + 6000 * 48 + SR * 1));
    auto c = sim.metro();
    CHECK(!c.empty() && c[0].type == kClickDown && c[0].t == 1000, "first pulse is beat 1");
    CHECK(sim.e.status() == kStatusLost, "status CLOCK LOST after gap (got %d)", sim.e.status());
    size_t before = c.size();
    CHECK(before == 12, "12 beats then silence (got %zu)", before);
    sim.runUntil((uint64_t)(1000 + 6000 * 48 + SR * 2 + 6000 * 32));
    c = sim.metro();
    CHECK(c.size() > before && c[before].type == kClickDown, "resume starts at beat 1");
}

// ================================================================== CV clock resolutions

static void test24ppqn() {
    std::printf("  CV 24 PPQN clock (Hermod default), quarter clicks\n");
    Sim sim;
    sim.s.cvResolution = kCv24ppqn;
    sim.gate.push_back({500, (uint64_t)(SR * 30)});
    sim.addPulses(1000, 1000, 24 * 40);  // 120 BPM
    sim.runUntil(1000 + 1000 * 24 * 40);
    auto c = sim.metro();
    bool ok = c.size() == 40;
    for (size_t i = 1; ok && i < c.size(); ++i) ok = c[i].t - c[i - 1].t == 24000;
    CHECK(ok, "40 beats exactly 500 ms apart (got %zu clicks)", c.size());
    CHECK(c.size() > 4 && c[0].type == kClickDown && c[4].type == kClickDown && c[1].type == kClickBeat, "accents");
    CHECK(std::fabs(sim.e.bpm() - 120.0f) < 0.05f, "BPM 120.0 (got %.2f)", sim.e.bpm());
}

static void testBarClock(int meter, double bpm) {
    const Meter& m = kMeters[meter];
    std::printf("  CV 1-per-bar clock in %s at %.0f BPM, 1/16 clicks interpolated\n", m.name, bpm);
    Sim sim;
    sim.s.cvResolution = kCvBar;
    sim.s.meter = meter;
    sim.s.division = kDiv16;
    sim.s.cvTransport = kCvNone;
    double tickP = SR * 60.0 / (bpm * 24.0);
    double bar = tickP * m.barTicks;
    sim.addPulses(1000, bar, 12);
    sim.runUntil((uint64_t)(1000 + bar * 12));
    auto c = sim.metro();
    // after the first bar (tempo unknown), every 1/16 must land on the grid
    double maxErr = 0;
    int count = 0, downs = 0;
    for (auto& k : c) {
        if (k.t < 1000 + bar) continue;
        double pos = (k.t - 1000) / tickP;
        double err = (pos - std::floor(pos / 6 + 0.5) * 6) * tickP;
        maxErr = std::max(maxErr, std::fabs(err));
        ++count;
        if (k.type == kClickDown) ++downs;
    }
    int expect = 11 * m.barTicks / 6;
    CHECK(count == expect, "%d sixteenths after the first bar (got %d)", expect, count);
    CHECK(maxErr <= 1.6, "interpolated 1/16s on the grid (max err %.2f samples)", maxErr);
    CHECK(downs == 11, "one downbeat per bar pulse (got %d)", downs);
}

static void testMeters() {
    std::printf("  All 11 time signatures: beat positions and accents\n");
    for (int mi = 0; mi < kNumMeters; ++mi) {
        const Meter& m = kMeters[mi];
        Sim sim;
        sim.s.cvResolution = kCv24ppqn;
        sim.s.meter = mi;
        sim.s.division = kDiv8;
        sim.gate.push_back({500, (uint64_t)(SR * 60)});
        int bars = 4;
        sim.addPulses(1000, 1000, m.barTicks * bars);
        sim.runUntil(1000 + 1000 * m.barTicks * bars);
        auto c = sim.metro();
        bool ok = true;
        int beats = 0, downs = 0, subs = 0;
        for (auto& k : c) {
            int rel = (int)((k.t - 1000) / 1000) % m.barTicks;
            bool isBeat = false;
            for (int b = 0; b < m.numBeats; ++b) if (m.beat[b] == rel) isBeat = true;
            if (k.type == kClickDown) { ++downs; if (rel != 0) ok = false; }
            else if (k.type == kClickBeat) { ++beats; if (!isBeat || rel == 0) ok = false; }
            else { ++subs; if (isBeat || rel % 12) ok = false; }
        }
        CHECK(ok && downs == bars && beats == bars * (m.numBeats - 1) && subs == bars * (m.barTicks / 12 - m.numBeats),
              "%s: %d downbeats, %d beats, %d eighths, positions correct", m.name, downs, beats, subs);
    }
}

// ================================================================== MIDI

static void testMidiBasic() {
    std::printf("  MIDI Start/clock 120 BPM, clocks while stopped, Stop, Continue\n");
    Sim sim;
    sim.s.source = kSourceMIDI;
    double p = 1000;
    sim.addMidiClocks(1000, 120, 24 * 40);
    double start = 1000 + p * 24 * 4 - 300;
    sim.addMidi(start, 0xFA);
    double stop = 1000 + p * (24 * 4 + 24 * 6 + 12);
    sim.addMidi(stop - 200, 0xFC);
    double cont = 1000 + p * (24 * 20) - 300;
    sim.addMidi(cont, 0xFB);
    sim.runUntil((uint64_t)(1000 + p * 24 * 30));
    auto c = sim.metro();
    bool noneBefore = true;
    for (auto& k : c) if (k.t < start) noneBefore = false;
    CHECK(noneBefore, "no clicks from clocks received before Start");
    double first = 1000 + p * 24 * 4;
    CHECK(!c.empty() && c[0].type == kClickDown && c[0].t >= first && c[0].t < first + sim.block,
          "Start -> downbeat within one block of its clock");
    bool ok = true;
    for (size_t i = 1; i < 7 && i < c.size(); ++i) if (c[i].t - c[i - 1].t != 24000) ok = false;
    CHECK(ok, "quarter clicks every 500 ms");
    bool silent = true;
    for (auto& k : c) if (k.t > stop && k.t < cont) silent = false;
    CHECK(silent, "Stop silences immediately");
    Click after{};
    for (auto& k : c) if (k.t > cont) { after = k; break; }
    CHECK(after.type == kClickBeat && after.tick == 168, "Continue resumes position (tick %d)", after.tick);
    int down2 = -1;
    for (auto& k : c) if (k.t > cont && k.type == kClickDown) { down2 = k.tick; break; }
    CHECK(down2 == 192, "next downbeat at bar 3 (tick %d)", down2);
}

static void testMidiSpp() {
    std::printf("  MIDI Song Position Pointer + Continue\n");
    Sim sim;
    sim.s.source = kSourceMIDI;
    sim.addMidiClocks(1000, 120, 24 * 20);
    sim.addSpp(1500, 4 * 4 * 2 + 4);
    sim.addMidi(1600, 0xFB);
    sim.runUntil(1000 + 1000 * 24 * 10);
    auto c = sim.metro();
    CHECK(!c.empty() && c[0].type == kClickBeat && c[0].tick == 9 * 24, "first click is beat 2 of bar 3");
    CHECK(c.size() > 3 && c[3].type == kClickDown, "downbeat 3 beats later");
}

static void testMidiDrift() {
    std::printf("  MIDI 123.7 BPM jittered for 20 min: no accumulated drift\n");
    Sim sim(24);
    sim.s.source = kSourceMIDI;
    sim.s.division = kDiv16;
    double bpm = 123.7, p = SR * 60.0 / (bpm * 24.0);
    int n = (int)(20 * 60 * SR / p);
    sim.addMidi(500, 0xFA);
    uint32_t r = 12345;
    for (int i = 0; i < n; ++i) {
        r = r * 1103515245u + 12345u;
        double j = ((double)((r >> 8) & 0xFFFF) / 65535.0 - 0.5) * 0.6e-3 * SR;
        sim.addMidi(1000 + i * p + j, 0xF8);
    }
    sim.runUntil((uint64_t)(1000 + n * p));
    double maxLate = 0, minLate = 1e9;
    auto c = sim.metro();
    for (auto& k : c) {
        double late = k.t - (1000 + k.tick * p);
        maxLate = std::max(maxLate, late);
        minLate = std::min(minLate, late);
    }
    double sixteenths = std::floor((n - 1) / 6.0) + 1;
    CHECK(c.size() == (size_t)sixteenths, "every 1/16 sounded (%zu / %.0f)", c.size(), sixteenths);
    CHECK(maxLate < sim.block + 0.3e-3 * SR + 1 && minLate > -0.3e-3 * SR - 1, "error bounded (%.1f..%.1f samples)",
          minLate, maxLate);
    CHECK(std::fabs(sim.e.bpm() - bpm) < 0.3, "BPM display %.1f ~ %.1f", sim.e.bpm(), bpm);
}

static void testMidiStartClockSameBlock() {
    std::printf("  MIDI Start and first clock in the same block: single downbeat\n");
    Sim sim;
    sim.s.source = kSourceMIDI;
    sim.addMidi(1000, 0xFA);
    sim.addMidiClocks(1001, 120, 24 * 8);
    sim.runUntil(1000 + 1000 * 24 * 8);
    auto c = sim.metro();
    int downsEarly = 0;
    for (auto& k : c) if (k.t < 2000 && k.type == kClickDown) ++downsEarly;
    CHECK(downsEarly == 1 && c.size() == 8, "one downbeat, 8 beats total (%d, %zu)", downsEarly, c.size());
}

static void testMidiClockOnly() {
    std::printf("  MIDI clock-only mode, loss and recovery\n");
    Sim sim;
    sim.s.source = kSourceMIDI;
    sim.s.midiMode = kMidiClockOnly;
    sim.addMidi(900, 0xFC);
    sim.addMidiClocks(1000, 100, 24 * 8);
    double p = SR * 60.0 / (100 * 24.0);
    double resume = 1000 + p * 24 * 8 + SR * 2;
    sim.addMidiClocks(resume, 100, 24 * 4);
    sim.runUntil((uint64_t)(1000 + p * 24 * 8 + SR));
    auto c = sim.metro();
    CHECK(c.size() == 8 && c[0].type == kClickDown, "clicks from first clock (%zu)", c.size());
    CHECK(sim.e.status() == kStatusLost, "CLOCK LOST after clock stops");
    sim.runUntil((uint64_t)(resume + p * 24 * 4));
    c = sim.metro();
    CHECK(c.size() == 12 && c[8].type == kClickDown, "recovers at beat 1");
}

// ================================================================== polyrhythm

static void testPolyCv(int x, int y) {
    std::printf("  Polyrhythm %d:%d on CV 4 PPQN @120: positions, accents, realignment\n", x, y);
    Sim sim;
    sim.s.polyEnable = true;
    sim.s.polyX = x;
    sim.s.polyY = y;
    sim.gate.push_back({500, (uint64_t)(SR * 60)});
    int cycles = 6;
    sim.addPulses(1000, 6000, 4 * y * cycles + 1);
    sim.runUntil(1000 + 6000 * 4 * y * cycles + 10);
    auto c = sim.layer(kLayerPoly);
    double cycle = 24000.0 * y, step = cycle / x;
    double maxErr = 0;
    bool acc = true;
    for (size_t i = 0; i < c.size(); ++i) {
        double ideal = 1000 + i * step;
        maxErr = std::max(maxErr, std::fabs(c[i].t - ideal));
        if ((c[i].type == kClickPolyAccent) != (i % x == 0)) acc = false;
    }
    CHECK((int)c.size() == x * cycles + 1, "%d poly clicks (got %zu)", x * cycles + 1, c.size());
    CHECK(maxErr <= 1.0, "evenly spaced, max error %.2f samples", maxErr);
    CHECK(acc, "accent on the first pulse of every cycle only");
}

static void testPolyMidiInterpolation() {
    std::printf("  Polyrhythm 7:4 on jittered MIDI clock: interpolated between ticks\n");
    Sim sim;
    sim.s.source = kSourceMIDI;
    sim.s.polyEnable = true;
    sim.s.polyX = 7;
    sim.s.polyY = 4;
    double p = 1000;  // 120 BPM
    sim.addMidi(500, 0xFA);
    uint32_t r = 777;
    int n = 24 * 4 * 20;
    for (int i = 0; i < n; ++i) {
        r = r * 1103515245u + 12345u;
        double j = ((double)((r >> 8) & 0xFFFF) / 65535.0 - 0.5) * 0.4e-3 * SR;
        sim.addMidi(1000 + i * p + j, 0xF8);
    }
    sim.runUntil((uint64_t)(1000 + n * p));
    auto c = sim.layer(kLayerPoly);
    double step = 96000.0 / 7, maxErr = 0;
    for (size_t i = 0; i < c.size(); ++i) maxErr = std::max(maxErr, std::fabs(c[i].t - (1000 + i * step)));
    CHECK(c.size() >= 7 * 19, "every 7:4 pulse sounded (%zu)", c.size());
    // fractional positions (k*96/7 ticks) land between clocks; error = block + MIDI jitter
    CHECK(maxErr < sim.block + 0.2e-3 * SR + 12, "max error %.1f samples (block %d)", maxErr, sim.block);
}

static void testPolyChangeAtCycleBoundary() {
    std::printf("  Polyrhythm ratio change applies at the next cycle boundary\n");
    Sim sim;
    sim.s.polyEnable = true;
    sim.s.polyX = 3;
    sim.s.polyY = 4;
    sim.gate.push_back({500, (uint64_t)(SR * 60)});
    sim.addPulses(1000, 6000, 4 * 4 * 4);
    sim.runUntil(1000 + 40000);  // into the first cycle (after pulse 2)
    sim.s.polyX = 4;
    sim.runUntil(1000 + 96000 * 3 - 100);
    auto c = sim.layer(kLayerPoly);
    std::vector<uint32_t> want = {1000, 33000, 65000, 97000, 121000, 145000, 169000, 193000, 217000, 241000, 265000};
    bool ok = c.size() == want.size();
    for (size_t i = 0; ok && i < c.size(); ++i) ok = c[i].t == want[i];
    CHECK(ok, "3 pulses finish the current cycle, then 4 per cycle");
    if (!ok) dumpClicks(c);
}

static void testIndependentCycles() {
    std::printf("  Layers keep independent cycles (3:2 poly + Habanera in 3/4)\n");
    Sim sim;
    sim.s.meter = 1;  // 3/4
    sim.s.polyEnable = true;
    sim.s.polyX = 3;
    sim.s.polyY = 2;
    sim.s.patEnable = true;
    sim.s.pattern = patternIndex("Habanera");  // 2 quarters
    sim.gate.push_back({500, (uint64_t)(SR * 60)});
    sim.addPulses(1000, 6000, 4 * 12);
    sim.runUntil(1000 + 6000 * 4 * 12);
    auto poly = sim.layer(kLayerPoly);
    auto pat = sim.layer(kLayerPattern);
    int polyAcc = 0;
    bool ok = true;
    for (auto& k : poly)
        if (k.type == kClickPolyAccent) { ++polyAcc; if ((k.t - 1000) % 48000) ok = false; }
    CHECK(ok && polyAcc == 6, "poly accents every 2 quarters, not every 3/4 bar (%d)", polyAcc);
    std::vector<uint32_t> hab = {0, 3, 4, 6};
    bool pok = pat.size() >= 8;
    for (size_t i = 0; pok && i < 8; ++i) pok = pat[i].t == 1000 + (uint32_t)((i / 4) * 48000 + hab[i % 4] * 6000);
    CHECK(pok, "habanera onsets at 16ths 0,3,4,6 of each 2/4 cycle");
    auto m = sim.metro();
    int downs = 0;
    for (auto& k : m) if (k.type == kClickDown) { ++downs; if ((k.t - 1000) % 72000) ok = false; }
    CHECK(ok && downs == 4, "metronome downbeats every 3/4 bar (%d)", downs);
}

static void testPolyCvOutputs() {
    std::printf("  Poly clock/reset CV outs: 24 per pulse, reset leads clock, mute keeps CV\n");
    Sim sim;
    sim.s.polyEnable = true;
    sim.s.polyX = 3;
    sim.s.polyY = 4;
    sim.s.polyClockRes = kPoly24;
    sim.s.polyAudio = false;  // audio off must not stop CV
    sim.trigEnabled[kOutPolyClock] = sim.trigEnabled[kOutPolyReset] = true;
    sim.gate.push_back({500, (uint64_t)(SR * 60)});
    sim.addPulses(1000, 6000, 4 * 4 * 3 + 1);
    sim.runUntil(1000 + 96000 * 3 + 100);
    auto ck = sim.edges(kOutPolyClock);
    auto rs = sim.edges(kOutPolyReset);
    CHECK(sim.layer(kLayerPoly).empty(), "no poly audio when poly audio is off");
    CHECK(rs.size() == 4 && rs[0] == 1000 && rs[1] == 97000, "reset at every cycle start");
    CHECK(!ck.empty() && ck[0] == 1000 + 48, "clock follows its reset by 1 ms (first at %llu)",
          (unsigned long long)(ck.empty() ? 0 : ck[0]));
    // From the second input pulse on (tempo known), every derived pulse is on the grid.
    double step = 96000.0 / 72, maxErr = 0;
    size_t after = 0;
    for (size_t i = 0; i < ck.size(); ++i) {
        if (ck[i] < 8000) continue;
        ++after;
        double k = std::floor(((double)ck[i] - 1048) / step + 0.5);
        maxErr = std::max(maxErr, std::fabs((double)ck[i] - 1048 - k * step));
    }
    size_t want = 0;
    for (int k = 0; k <= 3 * 72; ++k) if (1048 + k * step >= 8000) ++want;
    CHECK(after == want, "72 clock pulses per cycle (%zu of %zu after the first input pulse)", after, want);
    CHECK(maxErr <= 1.0, "clock pulses evenly spaced (max err %.2f)", maxErr);
    // pulse width must not merge consecutive pulses
    size_t high = 0;
    for (size_t i = 1000; i < 1000 + 96000; ++i) if (sim.trigLog[kOutPolyClock][i] > 2.5f) ++high;
    CHECK(high < 96000 / 2 + 100, "pulses shorter than half their spacing");
}

// ================================================================== pattern

static int patternIndex(const char* name) {
    for (int i = 0; i < kNumPatterns; ++i) {
        const char* a = kPatterns[i].name; const char* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (!*a && !*b) return i;
    }
    std::printf("    no pattern %s\n", name);
    return 0;
}

static void testPattern(const char* pname, const std::vector<int>& onsets, int steps, int cycleTicks) {
    int idx = patternIndex(pname);
    std::printf("  Pattern %s: onsets, grid and cycle length\n", kPatterns[idx].name);
    Sim sim;
    sim.s.patEnable = true;
    sim.s.pattern = idx;
    sim.trigEnabled[kOutPatTrig] = sim.trigEnabled[kOutPatReset] = true;
    sim.gate.push_back({500, (uint64_t)(SR * 60)});
    int cycles = 3;
    sim.addPulses(1000, 6000, cycleTicks / 6 * cycles + 1);
    sim.runUntil(1000 + 1000 * cycleTicks * cycles + 200);
    auto c = sim.layer(kLayerPattern);
    double stepS = 1000.0 * cycleTicks / steps;
    bool ok = c.size() == onsets.size() * cycles + (onsets[0] == 0 ? 1 : 0);
    for (size_t i = 0; ok && i < onsets.size() * cycles; ++i) {
        double ideal = 1000 + (i / onsets.size()) * 1000.0 * cycleTicks + onsets[i % onsets.size()] * stepS;
        if (std::fabs(c[i].t - ideal) > 1.0) ok = false;
    }
    CHECK(ok, "%zu onsets per cycle at the right steps (got %zu clicks)", onsets.size(), c.size());
    if (!ok) dumpClicks(c, 12);
    auto tr = sim.edges(kOutPatTrig), rs = sim.edges(kOutPatReset);
    CHECK(tr.size() == c.size(), "one trigger per pattern event (%zu vs %zu)", tr.size(), c.size());
    CHECK(rs.size() == (size_t)cycles + 1, "pattern reset once per cycle (%zu)", rs.size());
}

static void testPatternChangeAtBoundary() {
    std::printf("  Pattern change mid-cycle waits for the cycle boundary\n");
    Sim sim;
    sim.s.patEnable = true;
    sim.s.pattern = patternIndex("Tresillo");  // 96 ticks
    sim.gate.push_back({500, (uint64_t)(SR * 60)});
    sim.addPulses(1000, 6000, 64);
    sim.runUntil(1000 + 40000);
    sim.s.pattern = patternIndex("Cinquillo");
    sim.runUntil(1000 + 96000 * 2 - 100);
    auto c = sim.layer(kLayerPattern);
    std::vector<uint32_t> want = {1000, 37000, 73000, 97000, 121000, 133000, 157000, 169000};
    bool ok = c.size() == want.size();
    for (size_t i = 0; ok && i < c.size(); ++i) ok = c[i].t == want[i];
    CHECK(ok, "tresillo finishes, then cinquillo");
    if (!ok) dumpClicks(c);
}

// ================================================================== base CV outs, transport

static void testBaseOutputs() {
    std::printf("  Base clock (1/16) and bar reset outs from a 24 PPQN clock\n");
    Sim sim;
    sim.s.cvResolution = kCv24ppqn;
    sim.trigEnabled[kOutBaseClock] = sim.trigEnabled[kOutBaseReset] = true;
    sim.gate.push_back({500, (uint64_t)(SR * 60)});
    sim.addPulses(1000, 1000, 96 * 3 + 1);
    sim.runUntil(1000 + 96000 * 3 + 100);
    auto ck = sim.edges(kOutBaseClock), rs = sim.edges(kOutBaseReset);
    bool ok = ck.size() == 16 * 3 + 1;
    for (size_t i = 0; ok && i < ck.size(); ++i) ok = ck[i] == 1048 + i * 6000;
    CHECK(ok, "1/16 clock every 125 ms, 1 ms after reset (%zu)", ck.size());
    CHECK(rs.size() == 4 && rs[0] == 1000 && rs[1] == 97000, "reset at every bar line");
}

static void testStartOnlyResets() {
    std::printf("  'Start only' reset modes fire once per session\n");
    Sim sim;
    sim.s.polyEnable = sim.s.patEnable = true;
    sim.s.polyResetMode = sim.s.patResetMode = kResetStart;
    sim.s.baseResetMode = kResetStart;
    sim.trigEnabled[kOutPolyReset] = sim.trigEnabled[kOutPatReset] = sim.trigEnabled[kOutBaseReset] = true;
    sim.gate.push_back({500, 200000});
    sim.gate.push_back({300000, 600000});
    sim.addPulses(1000, 6000, 100);
    sim.runUntil(600000);
    auto a = sim.edges(kOutPolyReset), b = sim.edges(kOutPatReset), c = sim.edges(kOutBaseReset);
    CHECK(a.size() == 2 && b.size() == 2 && c.size() == 2, "one reset per RUN session (%zu/%zu/%zu)", a.size(),
          b.size(), c.size());
}

static void testMidiStartResetsLayers() {
    std::printf("  MIDI Start realigns all layers; Continue preserves them\n");
    Sim sim;
    sim.s.source = kSourceMIDI;
    sim.s.polyEnable = true;
    sim.s.polyX = 3;
    sim.s.polyY = 4;
    sim.addMidi(500, 0xFA);
    sim.addMidiClocks(1000, 120, 24 * 30);
    sim.addMidi(1000 + 1000 * 50 - 300, 0xFC);   // stop mid-cycle (tick 50)
    sim.addMidi(1000 + 1000 * 80 - 300, 0xFA);   // Start again at tick 80 (new position 0)
    sim.runUntil(1000 + 1000 * 24 * 12);
    auto c = sim.layer(kLayerPoly);
    // session 1: pulses at ticks 0, 32 ; session 2 starts at the clock at t=81000
    bool ok = c.size() >= 4 && c[0].t < 1100 && c[1].t >= 33000 && c[1].t < 33100 && c[2].t >= 81000 &&
              c[2].t < 81100 && c[2].type == kClickPolyAccent;
    CHECK(ok, "poly restarts its cycle on Start");
    if (!ok) dumpClicks(c);
}

static void testMuteAndOutputs() {
    std::printf("  Mute preserves sync; output level; onset sample; layers summed on a shared bus\n");
    Sim a, b;
    for (Sim* s : {&a, &b}) {
        s->gate.push_back({100, (uint64_t)(SR * 30)});
        s->addPulses(1000, 6000, 160);
    }
    a.runUntil(1000 + 6000 * 160);
    b.runUntil(1000 + 6000 * 40);
    b.s.mute = true;
    b.runUntil(1000 + 6000 * 40 + 960);
    b.peak = 0;
    b.runUntil(1000 + 6000 * 100);
    double mutedPeak = b.peak;
    b.s.mute = false;
    b.runUntil(1000 + 6000 * 160);
    auto ma = a.metro(), mb = b.metro();
    bool same = ma.size() == mb.size();
    for (size_t i = 0; same && i < ma.size(); ++i) same = ma[i].t == mb[i].t && ma[i].type == mb[i].type;
    CHECK(same, "identical click schedule with and without mute");
    CHECK(mutedPeak < 0.05, "muted output silent (peak %.3f V)", mutedPeak);

    Sim o;
    o.logOut = true;
    o.gate.push_back({100, (uint64_t)(SR * 10)});
    o.addPulses(1000, 6000, 4 * 8);
    o.runUntil(1000 + 6000 * 32);
    CHECK(o.peak > 2.4 && o.peak < 3.7, "downbeat peak ~3 V at the 60%% default master (%.2f V)", o.peak);
    CHECK(std::fabs(o.outLog[1000]) > 0.05 && o.outLog[999] == 0.0f, "audio onset on the clock-edge sample");

    Sim m;
    m.logOut = true;
    m.s.polyEnable = true;
    m.s.polyX = 1;
    m.s.polyY = 1;
    m.replace[kLayerPoly] = true;  // both replace on the same bus: must still sum
    m.gate.push_back({100, (uint64_t)(SR * 10)});
    m.addPulses(1000, 6000, 8);
    m.runUntil(1000 + 6000 * 4);
    CHECK(m.peak > o.peak * 1.05, "metro + poly summed on a shared bus (%.2f V vs %.2f V)", m.peak, o.peak);
}


// ================================================================== v1_3 additions

static void testPatternTable() {
    std::printf("  Pattern table: %d patterns in %d groups are well formed\n", kNumPatterns, kNumPatternGroups);
    bool ok = kPatternGroupStart[kNumPatternGroups] == kNumPatterns;
    for (int i = 0; i < kNumPatterns; ++i) {
        const PatternDef& p = kPatterns[i];
        uint32_t all = (1u << p.steps) - 1;
        if (p.steps < 1 || p.steps > 16 || p.cycleTicks != p.steps * p.stepTicks || !p.onsets || (p.onsets & ~all) ||
            (p.accents & ~p.onsets) || (p.ghosts & ~p.onsets) || (p.accents & p.ghosts)) {
            std::printf("    bad pattern %s\n", p.name);
            ok = false;
        }
    }
    CHECK(ok, "all patterns consistent");
}

static void testSwing() {
    std::printf("  Swing: 8th swing at 50%% / 66%% / 75%%\n");
    for (int sw : {50, 66, 75}) {
        Sim sim;
        sim.s.patEnable = true;
        sim.s.pattern = patternIndex("8th swing");
        sim.s.swing = sw;
        sim.gate.push_back({500, (uint64_t)(SR * 60)});
        sim.addPulses(1000, 6000, 4 * 8 + 1);
        sim.runUntil(1000 + 96000 * 2 + 10);
        auto c = sim.layer(kLayerPattern);
        bool ok = c.size() == 17;
        for (size_t i = 0; ok && i < 16; ++i) {
            double ideal = 1000 + (i / 2) * 24000.0 + ((i & 1) ? 24000.0 * sw / 100.0 : 0.0);
            if (std::fabs(c[i].t - ideal) > 1.0) ok = false;
        }
        CHECK(ok, "swing %d%%: offbeats at %.0f%% of the beat", sw, (double)sw);
        if (!ok) dumpClicks(c);
    }
    Sim sim;  // non-swing patterns ignore the swing setting
    sim.s.patEnable = true;
    sim.s.pattern = patternIndex("8th offbeats");
    sim.s.swing = 75;
    sim.gate.push_back({500, (uint64_t)(SR * 60)});
    sim.addPulses(1000, 6000, 17);
    sim.runUntil(1000 + 96000);
    auto c = sim.layer(kLayerPattern);
    CHECK(c.size() >= 2 && c[0].t == 13000 && c[1].t == 37000, "straight pattern unaffected by swing");
}

static void testAccentOut() {
    std::printf("  Pattern accent out fires on accented hits only; ghosts still trigger\n");
    Sim sim;
    sim.s.patEnable = true;
    sim.s.pattern = patternIndex("Dembow");  // X..xX.x.X..xX.x.
    sim.trigEnabled[kOutPatTrig] = sim.trigEnabled[kOutPatAccent] = true;
    sim.gate.push_back({500, (uint64_t)(SR * 60)});
    sim.addPulses(1000, 6000, 33);
    sim.runUntil(1000 + 96000 * 2 + 300);
    auto tr = sim.edges(kOutPatTrig), ac = sim.edges(kOutPatAccent);
    bool ok = ac.size() == 9;
    for (size_t i = 0; ok && i < 8; ++i) ok = ac[i] == 1000 + i * 24000;
    CHECK(ok, "accents (kicks) on every beat (%zu)", ac.size());
    CHECK(tr.size() == 17, "all 8 hits per bar on the trigger out (%zu)", tr.size());
}

static void testInternalClock(double bpm) {
    std::printf("  Internal clock %.1f BPM for 10 min: exact spacing, no drift\n", bpm);
    Sim sim(32);
    sim.s.source = kSourceInternal;
    sim.s.play = true;
    sim.s.cvTransport = kCvReset;
    sim.s.internalBpm = (float)bpm;
    sim.s.division = kDiv16;
    sim.runUntil((uint64_t)(SR * 600));
    auto c = sim.metro();
    double step = SR * 60.0 / bpm / 4.0, maxErr = 0;
    for (size_t i = 0; i < c.size(); ++i) maxErr = std::max(maxErr, std::fabs(c[i].t - i * step));
    CHECK(!c.empty() && c[0].t == 0 && c[0].type == kClickDown, "starts on a downbeat immediately");
    CHECK(c.size() == (size_t)std::floor((SR * 600 - 1) / step) + 1, "%zu sixteenths", c.size());
    CHECK(maxErr <= 1.0, "max error %.2f samples after 10 minutes", maxErr);
    CHECK(std::fabs(sim.e.bpm() - bpm) < 0.06, "BPM display %.1f", sim.e.bpm());
}

static void testInternalTransport() {
    std::printf("  Internal clock: Play stop/start, RESET input restarts the bar\n");
    Sim sim;
    sim.s.source = kSourceInternal;
    sim.s.play = true;
    sim.s.cvTransport = kCvReset;
    sim.s.internalBpm = 120;
    sim.runUntil(24000 * 2 + 100);
    sim.s.play = false;
    sim.runUntil(24000 * 10);
    size_t stopped = sim.metro().size();
    CHECK(stopped == 3 && sim.e.status() == kStatusStopped, "stops when Play = Stop (%zu clicks)", stopped);
    sim.s.play = true;
    sim.runUntil(24000 * 12);
    auto c = sim.metro();
    CHECK(c.size() > stopped && c[stopped].type == kClickDown && c[stopped].t == 24000 * 10, "Play restarts at beat 1");
    // reset trigger mid-bar: next tick becomes the downbeat
    sim.gate.push_back({24000 * 13 + 5000, 24000 * 13 + 5240});
    sim.runUntil(24000 * 16);
    c = sim.metro();
    Click after{};
    for (auto& k : c) if (k.t >= 24000 * 13 + 5000) { after = k; break; }
    CHECK(after.type == kClickDown && after.t < 24000 * 13 + 5000 + 1100, "reset -> downbeat on the next tick");
}

static void testTapTempo() {
    std::printf("  Tap tempo from a CV input and from the Tap parameter\n");
    Sim sim;
    sim.s.source = kSourceInternal;
    sim.s.play = true;
    sim.s.cvTransport = kCvReset;
    sim.s.internalBpm = 90;
    for (int i = 0; i < 4; ++i) sim.taps.push_back({(uint64_t)(10000 + i * 24000), (uint64_t)(10000 + i * 24000 + 240)});
    sim.runUntil(10000 + 3 * 24000 + 200);
    float bpm = 0;
    bool got = sim.e.takeTapBpm(bpm);
    CHECK(got && std::fabs(bpm - 120.0f) < 0.05f, "4 taps 500 ms apart -> 120.0 BPM (%.2f)", bpm);
    // engine follows the tapped tempo right away, even before the host parameter updates
    size_t before = sim.metro().size();
    uint64_t t0 = sim.now;
    sim.runUntil(t0 + 24000 * 8);
    auto c = sim.metro();
    bool ok = c.size() > before + 2;
    for (size_t i = before + 1; ok && i < c.size(); ++i) ok = c[i].t - c[i - 1].t == 24000;
    CHECK(ok, "clicks now 500 ms apart");

    Sim p;
    p.s.source = kSourceInternal;
    p.s.play = true;
    p.s.cvTransport = kCvReset;
    p.runUntil(1000);
    for (int i = 0; i < 3; ++i) { p.e.tapNow(); p.runUntil(p.now + 32000); }
    got = p.e.takeTapBpm(bpm);
    CHECK(got && std::fabs(bpm - 90.0f) < 0.3f, "Tap parameter every 667 ms -> ~90 BPM (%.2f)", bpm);
}

static void testDefaultsAndBigBlocks() {
    std::printf("  Defaults (transport = RESET) and blocks larger than the internal chunk\n");
    Settings d;
    CHECK(d.cvTransport == kCvReset, "CV transport defaults to RESET");
    Sim sim(200);
    sim.gate.push_back({10000, (uint64_t)(SR * 60)});
    sim.addPulses(12000, 6000, 4 * 4 * 8);
    sim.runUntil(12000 + 6000 * 4 * 4 * 8);
    auto c = sim.metro();
    bool ok = c.size() == 32;
    for (size_t i = 1; ok && i < c.size(); ++i) ok = c[i].t - c[i - 1].t == 24000;
    CHECK(ok && c[0].t == 12000, "200-frame blocks: identical timing (%zu)", c.size());
}

static void testSoftSound() {
    std::printf("  'Soft' click sound: same timing, lower and shorter\n");
    Sim a, b;
    b.s.sound = kSoundSoft;
    for (Sim* s : {&a, &b}) {
        s->logOut = true;
        s->gate.push_back({100, (uint64_t)(SR * 10)});
        s->addPulses(1000, 6000, 33);
        s->runUntil(1000 + 6000 * 32);
    }
    auto ma = a.metro(), mb = b.metro();
    bool same = ma.size() == mb.size();
    for (size_t i = 0; same && i < ma.size(); ++i) same = ma[i].t == mb[i].t;
    CHECK(same, "identical click timing");
    // energy 15-40 ms after the downbeat: soft must be far quieter (less ring)
    double ea = 0, eb = 0;
    for (int i = 1000 + 720; i < 1000 + 1920; ++i) { ea += a.outLog[i] * a.outLog[i]; eb += b.outLog[i] * b.outLog[i]; }
    CHECK(eb < ea * 0.1, "soft tail much shorter (%.3g vs %.3g)", eb, ea);
    CHECK(b.peak > 1.5 && b.peak < 4.5, "soft peak level sensible (%.2f V)", b.peak);
    // zero crossings in the first 5 ms ~ pitch: soft must be lower
    auto zc = [](const std::vector<float>& v, int from, int to) { int n = 0; for (int i = from + 1; i < to; ++i) if ((v[i] > 0) != (v[i - 1] > 0)) ++n; return n; };
    int za = zc(a.outLog, 1000, 1240), zb = zc(b.outLog, 1000, 1240);
    CHECK(zb < za, "soft is lower pitched (%d vs %d zero crossings in 5 ms)", zb, za);
}

static void testGhostVelocity() {
    std::printf("  Pattern ghost out, 'No ghosts' trig mode, velocity CV\n");
    for (int mode : {kTrigAllHits, kTrigNoGhosts}) {
        Sim sim;
        sim.s.patEnable = true;
        sim.s.pattern = patternIndex("7/8 2+2+3");  // Xgxgxgg
        sim.s.patTrigMode = mode;
        sim.logVel = true;
        sim.trigEnabled[kOutPatTrig] = sim.trigEnabled[kOutPatAccent] = sim.trigEnabled[kOutPatGhost] = true;
        sim.gate.push_back({500, (uint64_t)(SR * 60)});
        sim.addPulses(1000, 6000, 14 * 3 + 1);  // 3 cycles of 7 eighths
        sim.runUntil(1000 + 84000 * 3 - 100);
        auto tr = sim.edges(kOutPatTrig), ac = sim.edges(kOutPatAccent), gh = sim.edges(kOutPatGhost);
        CHECK(ac.size() == 3 && gh.size() == 12, "per 3 cycles: 3 accents, 12 ghosts (%zu, %zu)", ac.size(), gh.size());
        size_t want = mode == kTrigAllHits ? 21 : 9;
        CHECK(tr.size() == want, "trig out %s: %zu hits (got %zu)", mode ? "no ghosts" : "all hits", want, tr.size());
        if (mode == kTrigAllHits) {
            // velocity CV held after each hit: step 0 accent, 1 ghost, 2 hit
            bool ok = std::fabs(sim.velLog[1000 + 10] - 5.0f) < 1e-6 && std::fabs(sim.velLog[13000 + 10] - 1.5f) < 1e-6 &&
                      std::fabs(sim.velLog[25000 + 10] - 3.5f) < 1e-6 && sim.velLog[999] == 0.0f &&
                      std::fabs(sim.velLog[12999] - 5.0f) < 1e-6;
            CHECK(ok, "velocity CV 5 / 1.5 / 3.5 V for accent / ghost / hit, held between hits");
        }
    }
}

static void testMidiDrumOut() {
    std::printf("  MIDI drum output: notes, velocities, channel, note-offs, layer selection\n");
    Sim sim;
    sim.s.midiOut = true;
    sim.s.metroAudio = false;  // MIDI is independent of the audio switches
    sim.gate.push_back({500, (uint64_t)(SR * 60)});
    sim.addPulses(1000, 6000, 17);
    sim.runUntil(1000 + 96000 + 2000);
    std::vector<MidiOutEv> on, off;
    for (auto& m : sim.midiOut) ((m.b0 & 0xF0) == 0x90 ? on : off).push_back(m);
    bool ok = on.size() == 5 && (on[0].b0 == 0x99) && on[0].b1 == 76 && on[0].b2 == 127 && on[1].b1 == 77 &&
              on[1].b2 == 83 && on[4].b1 == 76;
    CHECK(ok, "downbeat 76 vel 127, beats 77 vel 83 on channel 10 (%zu notes)", on.size());
    CHECK(sim.metro().empty(), "no audio while Metro audio is off");
    CHECK(off.size() == 5 && off[0].b0 == 0x89 && off[0].t >= 1000 + 700 && off[0].t <= 1000 + 800, "note-offs ~15 ms later");
    CHECK(on[0].t <= 1000 && on[0].t > 1000 - (uint64_t)sim.block, "note-on sent with the block containing its click");

    Sim p;  // pattern-only layer selection, accent velocities
    p.s.midiOut = true;
    p.s.midiLayers = 4;
    p.s.patEnable = true;
    p.s.pattern = patternIndex("7/8 2+2+3");
    p.s.midiChannel = 1;
    p.gate.push_back({500, (uint64_t)(SR * 60)});
    p.addPulses(1000, 6000, 15);
    p.runUntil(1000 + 84000 - 100);
    std::vector<int> vels;
    for (auto& m : p.midiOut) if ((m.b0 & 0xF0) == 0x90) { if (m.b0 != 0x90 || m.b1 != 75) vels.push_back(-1); else vels.push_back(m.b2); }
    std::vector<int> want = {127, 36, 89, 36, 89, 36, 36};
    CHECK(vels == want, "pattern only: accent 127 / hit 89 / ghost 36 on channel 1, no metronome notes");
}

static void testMidiAccentGhostNotes() {
    std::printf("  MIDI poly accent note and pattern ghost note\n");
    Sim sim;
    sim.s.midiOut = true;
    sim.s.midiLayers = 2 | 4;
    sim.s.polyEnable = true;
    sim.s.polyX = 5;
    sim.s.polyY = 4;
    sim.s.midiNote[kVoicePoly] = 60;
    sim.s.midiNote[kVoicePolyAccent] = 61;
    sim.s.patEnable = true;
    sim.s.pattern = patternIndex("7/8 2+2+3");
    sim.s.midiNote[kVoicePattern] = 70;
    sim.s.midiNote[kVoicePatAccent] = 71;
    sim.s.midiNote[kVoicePatGhost] = 72;
    sim.gate.push_back({500, (uint64_t)(SR * 60)});
    sim.addPulses(1000, 6000, 17);
    sim.runUntil(1000 + 96000 - 100);
    std::vector<int> poly, pat;
    for (auto& m : sim.midiOut) {
        if ((m.b0 & 0xF0) != 0x90) continue;
        if (m.b1 == 60 || m.b1 == 61) poly.push_back(m.b1);
        else pat.push_back(m.b1);
    }
    CHECK((poly == std::vector<int>{61, 60, 60, 60, 60}), "5:4 -> ONE on the accent note, two..five on the poly note");
    std::vector<int> want = {71, 72, 70, 72, 70, 72, 72, 71};
    CHECK(pat == want, "7/8 Xgxgxgg -> accent 71, ghost 72, hit 70 (%zu notes)", pat.size());
}

// ================================================================== v1.8: transport, count-in, recording

static std::vector<Click> banksOnly(const Sim& sim, int b0, int b1) {
    std::vector<Click> r;
    for (auto& c : sim.all) if (c.type >= b0 && c.type <= b1) r.push_back(c);
    return r;
}
static std::vector<MidiOutEv> realtime(const Sim& sim) {
    std::vector<MidiOutEv> r;
    for (auto& m : sim.midiOut) if (m.len == 1) r.push_back(m);
    return r;
}
static Sim* internalSim(int countBars) {
    Sim* sim = new Sim();
    sim->s.source = kSourceInternal;
    sim->s.cvTransport = kCvReset;
    sim->s.internalBpm = 120;
    sim->s.countInBars = countBars;
    return sim;
}
static uint64_t firstHigh(const std::vector<float>& v, uint64_t from = 0) {
    for (uint64_t i = from; i < v.size(); ++i) if (v[i] > 2.5f) return i;
    return (uint64_t)-1;
}
static uint64_t firstLow(const std::vector<float>& v, uint64_t from) {
    for (uint64_t i = from; i < v.size(); ++i) if (v[i] < 2.5f) return i;
    return (uint64_t)-1;
}

static void testCountIn() {
    std::printf("  Count-in: 1 bar of count clicks, then bar 1 with gates, resets and MIDI Start\n");
    Sim& sim = *internalSim(1);
    sim.s.midiClockOut = sim.s.midiOut = true;
    sim.logLevel[kLvlRun] = sim.logLevel[kLvlCountIn] = true;
    sim.trigEnabled[kOutBaseClock] = sim.trigEnabled[kOutBaseReset] = true;
    sim.runUntil(1000);
    CHECK(sim.e.status() == kStatusStopped && sim.all.empty(), "stopped until Play");
    sim.s.play = true;
    const uint64_t T0 = sim.now;
    sim.runUntil(T0 + 96000 * 2 + 100);
    auto cnt = banksOnly(sim, kClickCount, kClickCountAccent);
    bool ok = cnt.size() == 4;
    for (size_t i = 0; ok && i < 4; ++i) ok = cnt[i].t == T0 + i * 24000 && (cnt[i].type == kClickCountAccent) == (i == 0);
    CHECK(ok, "4 count clicks on the beats, first accented (%zu)", cnt.size());
    auto m = sim.metro();
    CHECK(!m.empty() && m[0].t == T0 + 96000 && m[0].type == kClickDown, "bar 1 downbeat right after the count");
    CHECK(firstHigh(sim.levelLog[kLvlRun]) == T0 + 96000, "Run out goes high on bar 1");
    CHECK(firstHigh(sim.levelLog[kLvlCountIn]) == T0 && firstLow(sim.levelLog[kLvlCountIn], T0) == T0 + 96000,
          "Count-in out high exactly during the count");
    auto ck = sim.edges(kOutBaseClock), rs = sim.edges(kOutBaseReset);
    CHECK(!ck.empty() && ck[0] >= T0 + 96000 && !rs.empty() && rs[0] == T0 + 96000, "no CV clock/reset during the count");
    auto rt = realtime(sim);
    ok = !rt.empty() && rt[0].b0 == 0xFA && rt[0].t <= T0 + 96000 && rt[0].t + sim.block > T0 + 96000;
    int clocks = 0;
    for (auto& r : rt) if (r.b0 == 0xF8) ++clocks;
    CHECK(ok, "MIDI Start first, in the block of bar 1");
    CHECK(rt.size() > 1 && rt[1].b0 == 0xF8 && clocks == 96 * 2 / 2 + 1, "24 MIDI clocks per beat after Start (%d)", clocks);
    delete &sim;

    Sim& c2 = *internalSim(2);
    c2.s.midiClockOut = c2.s.countInClock = c2.s.midiOut = true;
    c2.s.countInMidi = true;
    c2.s.play = true;
    c2.runUntil(96000 * 3);
    rt = realtime(c2);
    int before = 0;
    size_t i = 0;
    for (; i < rt.size() && rt[i].b0 != 0xFA; ++i) if (rt[i].b0 == 0xF8) ++before;
    CHECK(before == 192 && i < rt.size() && i + 1 < rt.size() && rt[i + 1].b0 == 0xF8,
          "Clock in count-in: 192 clocks during 2 bars, then Start + clock (%d)", before);
    std::vector<int> notes;
    for (auto& mm : c2.midiOut) if ((mm.b0 & 0xF0) == 0x90 && mm.t < 192000) notes.push_back(mm.b1);
    CHECK(notes.size() == 8 && notes[0] == 76 && notes[1] == 77 && notes[4] == 76, "Count-in MIDI: 8 count notes (%zu)",
          notes.size());
    delete &c2;

    Sim& c3 = *internalSim(4);  // 6/8 counts two dotted-quarter beats per bar
    c3.s.meter = 2;
    c3.s.play = true;
    c3.runUntil(72000 * 4 + 100);
    cnt = banksOnly(c3, kClickCount, kClickCountAccent);
    CHECK(cnt.size() == 8 && cnt[1].t - cnt[0].t == 36000 && c3.metro().size() == 1, "6/8 count-in: 2 clicks per bar");
    delete &c3;
}

static void testCountInCancel() {
    std::printf("  Stop during the count-in cancels it\n");
    Sim& sim = *internalSim(2);
    sim.logLevel[kLvlCountIn] = true;
    sim.s.play = true;
    sim.runUntil(30000);
    sim.s.play = false;
    sim.runUntil(96000 * 3);
    CHECK(sim.metro().empty() && banksOnly(sim, kClickCount, kClickCountAccent).size() == 2, "nothing plays after cancel");
    CHECK(sim.e.status() == kStatusStopped && sim.levelLog[kLvlCountIn].back() < 1.0f, "stopped, count gate low");
    delete &sim;
}

static void testRecording() {
    std::printf("  Recording: manual take ends on the bar line after disarm\n");
    {
        Sim& sim = *internalSim(0);
        sim.s.recArm = true;
        sim.trigEnabled[kOutRecTrig] = true;
        sim.s.play = true;
        const uint64_t T0 = sim.now;
        sim.runUntil(T0 + 96000 * 2 + 48000);
        sim.s.recArm = false;  // disarm mid bar 3
        sim.runUntil(T0 + 96000 * 5);
        auto rt = sim.edges(kOutRecTrig);
        CHECK(rt.size() == 2 && rt[0] == T0 && rt[1] == T0 + 96000 * 3, "record pulse at bar 1, stop pulse on bar 4 line");
        CHECK(sim.e.takeRecDisarm() && sim.e.transportOn() && sim.metro().back().t > T0 + 96000 * 4,
              "keeps playing, asks the host to disarm");
        delete &sim;
    }
    std::printf("  Recording: fixed 2-bar take (gate mode) and rec MIDI note\n");
    {
        Sim& sim = *internalSim(1);
        sim.s.recArm = true;
        sim.s.recMode = kRecGate;
        sim.s.recLengthBars = 2;
        sim.s.recNote = 60;
        sim.s.midiOut = true;
        sim.s.midiLayers = 0;  // MIDI layers: None
        sim.logLevel[kLvlRecGate] = true;
        sim.s.play = true;
        const uint64_t T0 = sim.now, B1 = T0 + 96000;  // after a 1-bar count-in
        sim.runUntil(B1 + 96000 * 4);
        CHECK(firstHigh(sim.levelLog[kLvlRecGate]) == B1 && firstLow(sim.levelLog[kLvlRecGate], B1) == B1 + 192000,
              "rec gate high for exactly 2 bars from bar 1 (after the count-in)");
        std::vector<MidiOutEv> nm;
        for (auto& m : sim.midiOut) if (m.len == 3 && m.b1 == 60) nm.push_back(m);
        CHECK(nm.size() == 2 && nm[0].b0 == 0x99 && nm[1].b0 == 0x89 && nm[1].t <= B1 + 192000 && nm[1].t + 24 > B1 + 192000,
              "rec note: on at the start, off at the end (gate mode)");
        delete &sim;
    }
    std::printf("  Recording: stop while recording is quantized to the bar line\n");
    {
        Sim& sim = *internalSim(0);
        sim.s.recArm = true;
        sim.s.recMode = kRecGate;
        sim.s.midiClockOut = sim.s.midiOut = true;
        sim.logLevel[kLvlRecGate] = sim.logLevel[kLvlRun] = true;
        sim.s.play = true;
        const uint64_t T0 = sim.now;
        sim.runUntil(T0 + 96000 + 48000);  // halfway through bar 2
        sim.s.play = false;
        sim.runUntil(T0 + 96000 + 48010);
        CHECK(!sim.e.transportOn(), "reports stopped right away (stop pending)");
        sim.runUntil(T0 + 96000 * 4);
        const uint64_t B3 = T0 + 192000;
        CHECK(firstLow(sim.levelLog[kLvlRecGate], T0) == B3 && firstLow(sim.levelLog[kLvlRun], T0 + 10) == B3,
              "rec gate and Run out fall exactly on the next bar line");
        auto m = sim.metro();
        CHECK(!m.empty() && m.back().t < B3, "no click on the stopping bar line");
        bool fc = false;
        for (auto& r : realtime(sim)) if (r.b0 == 0xFC && r.t <= B3 && r.t + 24 > B3) fc = true;
        CHECK(fc && sim.e.status() == kStatusStopped, "MIDI Stop on the bar line");
        delete &sim;
    }
    std::printf("  Recording: arm while playing punches in on the next bar line; plain stop is immediate\n");
    {
        Sim& sim = *internalSim(0);
        sim.trigEnabled[kOutRecTrig] = true;
        sim.s.play = true;
        const uint64_t T0 = sim.now;
        sim.runUntil(T0 + 96000 + 30000);
        sim.s.recArm = true;
        sim.runUntil(T0 + 96000 * 3 + 500);
        auto rt = sim.edges(kOutRecTrig);
        CHECK(!rt.empty() && rt[0] == T0 + 192000, "punch-in at bar 3");
        delete &sim;

        Sim& p = *internalSim(0);
        p.s.play = true;
        p.runUntil(30000);
        p.s.play = false;
        p.runUntil(30024);
        size_t n = p.metro().size();
        p.runUntil(96000 * 2);
        CHECK(!p.e.transportOn() && p.metro().size() == n, "stop without recording is immediate");
        delete &p;
    }
}

static void testTransportInputs() {
    std::printf("  CV transport TOGGLE (internal) and MIDI clock out following a CV clock\n");
    {
        Sim& sim = *internalSim(0);
        sim.s.cvTransport = kCvToggle;
        sim.s.play = false;
        sim.gate.push_back({10000, 10240});
        sim.gate.push_back({10000 + 96000 + 3000, 10000 + 96000 + 3240});
        sim.runUntil(10000 + 96000 * 3);
        auto m = sim.metro();
        CHECK(!m.empty() && m[0].t == 10000 && m.back().t < 10000 + 96000 + 3000, "first trigger starts, second stops");
        delete &sim;
    }
    {
        Sim sim;
        sim.s.cvResolution = kCv24ppqn;
        sim.s.midiClockOut = sim.s.midiOut = true;
        sim.gate.push_back({500, 1000 + 24000 * 8 + 500});
        sim.addPulses(1000, 1000, 24 * 10);
        sim.runUntil(1000 + 24000 * 10);
        auto rt = realtime(sim);
        int f8 = 0;
        for (auto& r : rt) if (r.b0 == 0xF8) ++f8;
        CHECK(!rt.empty() && rt[0].b0 == 0xFA && rt.back().b0 == 0xFC && f8 == 24 * 8 + 1,
              "Start, one MIDI clock per 24 PPQN pulse, Stop on RUN low (%d clocks)", f8);
        delete (Sim*)nullptr;
    }
    {
        Sim sim;
        sim.s.source = kSourceMIDI;
        sim.s.midiClockOut = sim.s.midiOut = true;  // never echoes clock when following MIDI
        sim.addMidi(500, 0xFA);
        sim.addMidiClocks(1000, 120, 48);
        sim.runUntil(60000);
        CHECK(realtime(sim).empty(), "no MIDI clock out when the source is MIDI");
    }
}

static void testMidiMaster() {
    std::printf("  MIDI out is the master switch; MIDI layers None sends clock without notes\n");
    Sim& a = *internalSim(1);
    a.s.midiClockOut = true;  // MIDI out off: nothing
    a.s.countInMidi = true;
    a.s.recArm = true;
    a.s.recNote = 60;
    a.s.play = true;
    a.runUntil(96000 * 3);
    CHECK(a.midiOut.empty(), "no MIDI at all with MIDI out off");
    delete &a;
    Sim& b = *internalSim(0);
    b.s.midiClockOut = b.s.midiOut = true;
    b.s.midiLayers = 0;
    b.s.play = true;
    b.runUntil(96000 * 2);
    size_t notes = 0;
    for (auto& m : b.midiOut) if (m.len == 3) ++notes;
    CHECK(notes == 0 && !realtime(b).empty(), "layers None: clock and Start only (%zu notes)", notes);
    delete &b;
}

static void testToggleInput() {
    std::printf("  Toggle input: one flip per trigger, noise and bounce rejected, no flip on prime\n");
    ToggleInput ti;
    ti.init((float)SR);
    std::vector<float> in(48000, 0.0f);
    for (int i = 0; i < 48000; ++i) in[i] = 0.05f * ((i * 7919) % 13 - 6) / 6.0f;  // +-50 mV noise
    auto pulse = [&](int at, int len) { for (int i = at; i < at + len && i < 48000; ++i) in[i] = 5.0f; };
    pulse(5000, 240);                           // trigger
    pulse(20000, 100); pulse(20300, 100);       // bouncy contact: counts once
    pulse(30000, 12000);                        // long gate: counts once
    int total = 0;
    for (int b = 0; b < 48000; b += 24) total += ti.scan(&in[b], 24);
    CHECK(total == 3, "3 triggers counted (%d)", total);
    ToggleInput hi;
    hi.init((float)SR);
    std::vector<float> g(480, 5.0f);
    CHECK(hi.scan(g.data(), 480) == 0, "an input already high when connected doesn't flip");
}

int main() {
    std::printf("Disting NT Metronome engine tests v1.10 (48 kHz)\n\n[CV]\n");
    testCvRunBasic(kDivBeats, 24000, "beat");
    testCvRunBasic(kDiv8, 12000, "1/8");
    testCvRunBasic(kDiv16, 6000, "1/16");
    testCvInterpolationDrift(120.0, kCv1ppqn, kDiv16, 10);
    testCvInterpolationDrift(123.7, kCv1ppqn, kDiv16, 10);
    testCvInterpolationDrift(97.3, kCv2ppqn, kDiv16, 10);
    testTempoRamp(90, 150);
    testTempoRamp(150, 90);
    testRunCoincidence(0, "same sample as");
    testRunCoincidence(48, "1 ms after");
    testRunCoincidence(-48, "1 ms before");
    testRunStopRestart();
    testResetMode();
    testNoneModeAndClockLoss();
    std::printf("\n[CV clock resolutions + time signatures]\n");
    test24ppqn();
    testBarClock(0, 120);
    testBarClock(7, 140);
    testBarClock(4, 90);
    testMeters();
    std::printf("\n[MIDI]\n");
    testMidiBasic();
    testMidiSpp();
    testMidiDrift();
    testMidiStartClockSameBlock();
    testMidiClockOnly();
    std::printf("\n[Polyrhythm]\n");
    testPolyCv(3, 4);
    testPolyCv(5, 4);
    testPolyCv(3, 2);
    testPolyCv(7, 4);
    testPolyCv(3, 8);
    testPolyMidiInterpolation();
    testPolyChangeAtCycleBoundary();
    testIndependentCycles();
    testPolyCvOutputs();
    std::printf("\n[Patterns]\n");
    testPattern("Tresillo", {0, 3, 6}, 8, 96);
    testPattern("Son clave 3-2", {0, 3, 6, 10, 12}, 16, 192);
    testPattern("Son clave 2-3", {2, 4, 8, 11, 14}, 16, 192);
    testPattern("Std bell (bembe)", {0, 2, 4, 5, 7, 9, 11}, 12, 144);
    testPattern("Rumba clave 3-2", {0, 3, 7, 10, 12}, 16, 192);
    testPattern("Habanera", {0, 3, 4, 6}, 8, 48);
    testPattern("7/8 2+2+3", {0, 1, 2, 3, 4, 5, 6}, 7, 84);
    testPattern("Jhaptal 10", {0, 1, 2, 3, 4, 6, 7, 8, 9}, 10, 240);
    testPattern("Teentaal 16", {0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15}, 16, 384);
    testPattern("Jazz ride", {0, 3, 5, 6, 9, 11}, 12, 96);
    testPattern("Dembow", {0, 3, 4, 6, 8, 11, 12, 14}, 16, 96);
    testPatternTable();
    testSwing();
    testAccentOut();
    testPatternChangeAtBoundary();
    std::printf("\n[Outputs / transport]\n");
    testBaseOutputs();
    testStartOnlyResets();
    testMidiStartResetsLayers();
    testMuteAndOutputs();
    std::printf("\n[Internal clock / tap]\n");
    testInternalClock(120.0);
    testInternalClock(123.7);
    testInternalTransport();
    testTapTempo();
    testDefaultsAndBigBlocks();
    testSoftSound();
    testGhostVelocity();
    testMidiDrumOut();
    testMidiAccentGhostNotes();
    std::printf("\n[Transport / count-in / recording]\n");
    testCountIn();
    testCountInCancel();
    testRecording();
    testTransportInputs();
    testMidiMaster();
    testToggleInput();
    std::printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
