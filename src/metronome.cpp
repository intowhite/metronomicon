// Metronomicon for Disting NT — plugin glue for the Expert Sleepers Disting NT C++ API (v13).
// All timing and sound live in metronome_engine.h; this file maps parameters,
// buses, MIDI callbacks and the display onto it.

#include <stddef.h>  // NULL (api.h's macros use it but don't include it)
#include <stdint.h>
#include <new>
#include <distingnt/api.h>

#include "metronome_engine.h"

#define METRO_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

// Shown on the algorithm's screen and in its description. Keep in step with CHANGELOG.md.
#define METRONOMICON_VERSION "1.10"

enum {
    // Clock & Transport
    kParamSource,
    kParamMidiMode,
    kParamCvClockIn,
    kParamCvClockRes,
    kParamCvTransportIn,
    kParamCvTransportMode,
    kParamInternalBpm,
    kParamInternalRun,
    kParamTapIn,
    kParamTap,
    // Metronome
    kParamTimeSig,
    kParamDivision,
    kParamMetroAudio,
    kParamDownLevel,
    kParamBeatLevel,
    kParamSubLevel,
    kParamMetroOut,
    kParamMetroOutMode,
    kParamBaseClockOut,
    kParamBaseClockRes,
    kParamBaseResetOut,
    kParamBaseResetMode,
    // Polyrhythm
    kParamPolyEnable,
    kParamPolyPreset,
    kParamPolyX,
    kParamPolyY,
    kParamPolyAudio,
    kParamPolyLevel,
    kParamPolyAccent,
    kParamPolyOut,
    kParamPolyOutMode,
    kParamPolyClockOut,
    kParamPolyClockRes,
    kParamPolyResetOut,
    kParamPolyResetMode,
    // Pattern
    kParamPatEnable,
    kParamPatGroup,
    kParamPattern,
    kParamSwing,
    kParamPatAudio,
    kParamPatLevel,
    kParamPatAccent,
    kParamPatOut,
    kParamPatOutMode,
    kParamPatTrigOut,
    kParamPatAccentOut,
    kParamPatResetOut,
    kParamPatResetMode,
    // Global
    kParamMaster,
    kParamMute,
    kParamSound,
    // v1_5 (appended so earlier presets keep their values)
    kParamPatTrigMode,
    kParamPatGhostOut,
    kParamPatVelOut,
    kParamMidiOut,
    kParamMidiDest,
    kParamMidiChannel,
    kParamMidiLayers,
    kParamNoteDown,
    kParamNoteBeat,
    kParamNoteSub,
    kParamNotePoly,
    kParamNotePattern,
    kParamNoteAccent,
    // v1_6
    kParamNotePolyAccent,
    kParamNoteGhost,
    // v1.8
    kParamCountIn,
    kParamCountMidi,
    kParamCountClock,
    kParamMidiClockOut,
    kParamRecArm,
    kParamRecLength,
    kParamRecOut,
    kParamRecMode,
    kParamRecNote,
    kParamRunOut,
    // v1.10
    kParamMetroToggleIn,

    kNumParams
};

static const char* const enumSource[] = {"CV", "MIDI", "Internal", NULL};
static const char* const enumMidiMode[] = {"Transport", "Clock only", NULL};
static const char* const enumCvRes[] = {"24 PPQN", "4 PPQN (1/16)", "2 PPQN (1/8)", "1 PPQN (1/4)", "1 per bar", NULL};
static const char* const enumCvTransport[] = {"RUN", "RESET", "NONE", "TOGGLE", NULL};
static const char* const enumRun[] = {"Stop", "Play", NULL};
static const char* const enumTimeSig[] = {"4/4", "3/4", "6/8", "2/4", "12/8", "2/2",
                                          "5/4", "7/8", "9/8", "5/8", "7/4", NULL};
static const char* const enumDivision[] = {"Beats", "1/4", "1/8", "1/16", NULL};
static const char* const enumBaseClockRes[] = {"1/4", "1/8", "1/16", "24 PPQN", NULL};
static const char* const enumResetMode[] = {"Cycle", "Start only", NULL};
static const char* const enumBarResetMode[] = {"Bar", "Start only", NULL};
static const char* const enumOffOn[] = {"Off", "On", NULL};
static const char* const enumSound[] = {"Classic", "Soft", NULL};
static const char* const enumCountIn[] = {"Off", "1 bar", "2 bars", "3 bars", "4 bars", NULL};
static const char* const enumRecLength[] = {"Manual", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", NULL};
static const int kRecLengthBars[] = {0, 1, 2, 4, 8, 16};
static const char* const enumRecMode[] = {"Trigger", "Gate", NULL};
static const char* const enumCountClock[] = {"Wait", "Run", NULL};
static const char* const enumTrigMode[] = {"All hits", "No ghosts", NULL};
static const char* const enumMidiDest[] = {"USB", "Breakout", "Select Bus", "Internal", "USB + Breakout", "All", NULL};
static const uint32_t kMidiDestMask[] = {
    kNT_destinationUSB, kNT_destinationBreakout, kNT_destinationSelectBus, kNT_destinationInternal,
    kNT_destinationUSB | kNT_destinationBreakout,
    kNT_destinationUSB | kNT_destinationBreakout | kNT_destinationSelectBus | kNT_destinationInternal};
static const char* const enumMidiLayers[] = {"All",           "Metronome",       "Polyrhythm",     "Pattern",
                                             "Metro + Poly",  "Metro + Pattern", "Poly + Pattern", "None", NULL};
static const uint8_t kMidiLayerMask[] = {7, 1, 2, 4, 3, 5, 6, 0};
enum { kMidiLayersNone = 7 };
static const char* const enumPolyPreset[] = {"Custom", "3:2", "2:3", "3:4", "4:3", "6:4",
                                             "5:4",    "4:5", "5:3", "7:4", "3:8", NULL};
static const char* const enumPolyClockRes[] = {"1 per pulse", "4 per pulse", "24 per pulse", NULL};

#define METRO_ENUM(n, mx, d, strings) \
    {.name = n, .min = 0, .max = mx, .def = d, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = strings},
#define METRO_PERCENT(n, mn, d) \
    {.name = n, .min = mn, .max = 100, .def = d, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL},
#define METRO_INT(n, mn, mx, d) \
    {.name = n, .min = mn, .max = mx, .def = d, .unit = kNT_unitNone, .scaling = 0, .enumStrings = NULL},

// Template; each instance keeps a copy so the Pattern list can follow the group.
static const _NT_parameter kParameterTemplate[] = {
    // Clock & Transport
    METRO_ENUM("Clock source", 2, 0, enumSource)
    METRO_ENUM("MIDI sync", 1, 0, enumMidiMode)
    NT_PARAMETER_CV_INPUT("CV clock input", 0, 1)
    METRO_ENUM("CV clock res", 4, 0, enumCvRes)
    NT_PARAMETER_CV_INPUT("CV transport in", 0, 2)
    METRO_ENUM("CV transport", 3, 1, enumCvTransport)
    {.name = "Internal BPM", .min = 200, .max = 3000, .def = 1200, .unit = kNT_unitBPM, .scaling = kNT_scaling10, .enumStrings = NULL},
    METRO_ENUM("Play", 1, 0, enumRun)  // internal clock transport; always stopped after loading
    NT_PARAMETER_CV_INPUT("Tap input", 0, 0)
    // Every step to the right is one tap (left does nothing), so you can keep turning.
    // A mapped gate/note taps once per rising edge.
    {.name = "Tap", .min = 0, .max = 1000, .def = 0, .unit = kNT_unitHasStrings, .scaling = 0, .enumStrings = NULL},
    // Metronome
    METRO_ENUM("Time signature", 10, 0, enumTimeSig)
    METRO_ENUM("Click division", 3, 0, enumDivision)
    METRO_ENUM("Metro audio", 1, 1, enumOffOn)
    METRO_PERCENT("Downbeat vol", 0, 100)
    METRO_PERCENT("Beat vol", 0, 65)
    METRO_PERCENT("Subdiv vol", 0, 25)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Metro out", 1, 13)  // bus 13 = Output 1
    NT_PARAMETER_CV_OUTPUT("Base clock out", 0, 0)
    METRO_ENUM("Base clock res", 3, 2, enumBaseClockRes)
    NT_PARAMETER_CV_OUTPUT("Base reset out", 0, 0)
    METRO_ENUM("Base reset mode", 1, 0, enumBarResetMode)
    // Polyrhythm
    METRO_ENUM("Poly enable", 1, 0, enumOffOn)
    METRO_ENUM("Ratio preset", 10, 1, enumPolyPreset)
    METRO_INT("Pulses (X)", 1, 16, 3)
    METRO_INT("Host beats (Y)", 1, 16, 2)
    METRO_ENUM("Poly audio", 1, 1, enumOffOn)
    METRO_PERCENT("Poly vol", 0, 70)
    METRO_ENUM("Poly accent", 1, 1, enumOffOn)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Poly out", 1, 13)
    NT_PARAMETER_CV_OUTPUT("Poly clock out", 0, 0)
    METRO_ENUM("Poly clock res", 2, 0, enumPolyClockRes)
    NT_PARAMETER_CV_OUTPUT("Poly reset out", 0, 0)
    METRO_ENUM("Poly reset mode", 1, 0, enumResetMode)
    // Pattern
    METRO_ENUM("Pattern enable", 1, 0, enumOffOn)
    METRO_ENUM("Pattern group", metro::kNumPatternGroups - 1, 0, metro::kPatternGroupNames)
    METRO_ENUM("Pattern", 0, 0, NULL)  // max / strings set per instance from the group
    {.name = "Swing", .min = 50, .max = 75, .def = 50, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL},
    METRO_ENUM("Pattern audio", 1, 1, enumOffOn)
    METRO_PERCENT("Pattern vol", 0, 70)
    METRO_ENUM("Pattern accent", 1, 1, enumOffOn)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Pattern out", 1, 13)
    NT_PARAMETER_CV_OUTPUT("Pattern trig out", 0, 0)
    NT_PARAMETER_CV_OUTPUT("Pat accent out", 0, 0)
    NT_PARAMETER_CV_OUTPUT("Pattern reset out", 0, 0)
    METRO_ENUM("Pat reset mode", 1, 0, enumResetMode)
    // Global
    METRO_PERCENT("Master volume", 0, 60)
    METRO_ENUM("Mute all", 1, 0, enumOffOn)
    METRO_ENUM("Click sound", 1, 0, enumSound)
    // v1_5
    METRO_ENUM("Pat trig mode", 1, 0, enumTrigMode)
    NT_PARAMETER_CV_OUTPUT("Pat ghost out", 0, 0)
    NT_PARAMETER_CV_OUTPUT("Pat velocity out", 0, 0)
    METRO_ENUM("MIDI out", 1, 0, enumOffOn)  // master switch for all MIDI output
    METRO_ENUM("MIDI out dest", 5, 0, enumMidiDest)
    METRO_INT("MIDI out channel", 1, 16, 10)
    METRO_ENUM("MIDI layers", 7, 0, enumMidiLayers)
    {.name = "Downbeat note", .min = 0, .max = 127, .def = 76, .unit = kNT_unitMIDINote, .scaling = 0, .enumStrings = NULL},
    {.name = "Beat note", .min = 0, .max = 127, .def = 77, .unit = kNT_unitMIDINote, .scaling = 0, .enumStrings = NULL},
    {.name = "Subdiv note", .min = 0, .max = 127, .def = 42, .unit = kNT_unitMIDINote, .scaling = 0, .enumStrings = NULL},
    {.name = "Poly note", .min = 0, .max = 127, .def = 56, .unit = kNT_unitMIDINote, .scaling = 0, .enumStrings = NULL},
    {.name = "Pattern note", .min = 0, .max = 127, .def = 75, .unit = kNT_unitMIDINote, .scaling = 0, .enumStrings = NULL},
    {.name = "Pat accent note", .min = 0, .max = 127, .def = 75, .unit = kNT_unitMIDINote, .scaling = 0, .enumStrings = NULL},
    // v1_6
    {.name = "Poly accent note", .min = 0, .max = 127, .def = 56, .unit = kNT_unitMIDINote, .scaling = 0, .enumStrings = NULL},
    {.name = "Pat ghost note", .min = 0, .max = 127, .def = 75, .unit = kNT_unitMIDINote, .scaling = 0, .enumStrings = NULL},
    // v1.8
    METRO_ENUM("Count-in", 4, 0, enumCountIn)
    METRO_ENUM("MIDI count out", 1, 0, enumOffOn)
    METRO_ENUM("Count-in clock", 1, 0, enumCountClock)
    METRO_ENUM("MIDI clock out", 1, 0, enumOffOn)
    METRO_ENUM("Rec arm", 1, 0, enumOffOn)
    METRO_ENUM("Rec length", 5, 0, enumRecLength)
    NT_PARAMETER_CV_OUTPUT("Rec out", 0, 0)
    METRO_ENUM("Rec out mode", 1, 0, enumRecMode)
    {.name = "Rec MIDI note", .min = 0, .max = 128, .def = 128, .unit = kNT_unitHasStrings, .scaling = 0, .enumStrings = NULL},
    NT_PARAMETER_CV_OUTPUT("Run out", 0, 0)
    // v1.10: each trigger flips Metro audio (the count-in still plays when it is off)
    NT_PARAMETER_CV_INPUT("Metro toggle in", 0, 0)
};

static const uint8_t pageMetro[] = {kParamTimeSig,      kParamDivision,     kParamMetroAudio,   kParamMetroToggleIn, kParamDownLevel,
                                    kParamBeatLevel,    kParamSubLevel,     kParamMetroOut,     kParamMetroOutMode,
                                    kParamBaseClockOut, kParamBaseClockRes, kParamBaseResetOut, kParamBaseResetMode};
static const uint8_t pagePoly[] = {kParamPolyEnable,  kParamPolyPreset,   kParamPolyX,        kParamPolyY,
                                   kParamPolyAudio,   kParamPolyLevel,    kParamPolyAccent,   kParamPolyOut,
                                   kParamPolyOutMode, kParamPolyClockOut, kParamPolyClockRes, kParamPolyResetOut,
                                   kParamPolyResetMode};
static const uint8_t pagePattern[] = {kParamPatEnable,   kParamPatGroup,    kParamPattern,      kParamSwing,
                                      kParamPatAudio,    kParamPatLevel,    kParamPatAccent,    kParamPatOut,
                                      kParamPatOutMode,  kParamPatTrigOut,  kParamPatTrigMode,  kParamPatAccentOut,
                                      kParamPatGhostOut, kParamPatVelOut,   kParamPatResetOut,  kParamPatResetMode};
static const uint8_t pageGlobal[] = {kParamMaster,       kParamMute,         kParamSound,       kParamMidiOut,
                                     kParamMidiDest,     kParamMidiClockOut, kParamMidiChannel, kParamMidiLayers,
                                     kParamCountMidi,    kParamNoteDown,     kParamNoteBeat,    kParamNoteSub,
                                     kParamNotePoly,     kParamNotePolyAccent, kParamNotePattern, kParamNoteAccent,
                                     kParamNoteGhost};

// Pages 0 (Clock & Transport) and 1 (Count-in & Rec) change with the clock source and
// MIDI clock out, so each instance builds its own copy (see buildPages()).
enum { kPageClock = 0, kPageRec, kNumPages, kMaxDynParams = 16 };
static const char* const kPageNames[] = {"Clock & Transport", "Count-in & Rec", "Metronome", "Polyrhythm", "Pattern",
                                         "Output"};

enum { kMaxGroupSize = 16 };

namespace {
struct MetronomeAlgorithm : public _NT_algorithm {
    MetronomeAlgorithm()
        : lastClockBus(-1), lastTransportBus(-1), lastTapBus(-1), lastTap(-1), applyingPreset(0), shownGroup(-1),
          blocks(0), tapRequests(0), tapsHandled(0), lastToggleBus(-1), metroOverride(-1) {}

    metro::Engine engine;
    int lastClockBus, lastTransportBus, lastTapBus, lastTap;
    int applyingPreset;  // poly preset whose X/Y values are being written (0 = none)
    int shownGroup;      // pattern group currently loaded into params[kParamPattern]
    uint32_t blocks;     // audio blocks since construction (to tell preset loading from editing)
    volatile uint32_t tapRequests;  // encoder-button taps from the UI thread
    uint32_t tapsHandled;
    metro::ToggleInput metroToggle;
    int lastToggleBus;
    int metroOverride;  // -1 = none, else Metro audio value until the parameter catches up
    _NT_parameter params[kNumParams];
    _NT_parameterPage pageList[6];
    _NT_parameterPages pageSet;
    uint8_t dynParams[kNumPages][kMaxDynParams];
    int pagesInternal, pagesMidiClock;  // state the dynamic pages were built for
    const char* groupNames[metro::kNumPatternGroups][kMaxGroupSize + 1];
};
}  // namespace

static inline int groupSize(int g) { return metro::kPatternGroupStart[g + 1] - metro::kPatternGroupStart[g]; }

static void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications) {
    req.numParameters = kNumParams;
    req.sram = sizeof(MetronomeAlgorithm);
    req.dram = 0;
    req.dtc = 0;
    req.itc = 0;
}

static void setPage(_NT_parameterPage& pg, const char* name, const uint8_t* params, int n) {
    pg.name = name;
    pg.numParams = (uint8_t)n;
    pg.group = 0;
    pg.unused[0] = pg.unused[1] = 0;
    pg.params = params;
}

// Play appears on the Clock page only with the internal clock; MIDI out dest appears on
// the Count-in page only while MIDI clock out is on. A parameter listed on two pages is
// the same parameter, so the copies always agree.
static void buildPages(MetronomeAlgorithm* a, bool internal, bool midiClock) {
    uint8_t* c = a->dynParams[kPageClock];
    int n = 0;
    const uint8_t clock[] = {kParamSource,        kParamMidiMode,        kParamCvClockIn,   kParamCvClockRes,
                             kParamCvTransportIn, kParamCvTransportMode, kParamInternalBpm};
    for (unsigned i = 0; i < METRO_ARRAY_SIZE(clock); ++i) c[n++] = clock[i];
    if (internal) c[n++] = kParamInternalRun;
    c[n++] = kParamTapIn;
    c[n++] = kParamTap;
    setPage(a->pageList[kPageClock], kPageNames[0], c, n);

    uint8_t* r = a->dynParams[kPageRec];
    n = 0;
    r[n++] = kParamInternalRun;
    r[n++] = kParamCountIn;
    r[n++] = kParamCountClock;
    r[n++] = kParamMidiClockOut;
    if (midiClock) r[n++] = kParamMidiDest;
    const uint8_t rec[] = {kParamRecArm, kParamRecLength, kParamRecOut, kParamRecMode, kParamRecNote, kParamRunOut};
    for (unsigned i = 0; i < METRO_ARRAY_SIZE(rec); ++i) r[n++] = rec[i];
    setPage(a->pageList[kPageRec], kPageNames[1], r, n);

    setPage(a->pageList[2], kPageNames[2], pageMetro, METRO_ARRAY_SIZE(pageMetro));
    setPage(a->pageList[3], kPageNames[3], pagePoly, METRO_ARRAY_SIZE(pagePoly));
    setPage(a->pageList[4], kPageNames[4], pagePattern, METRO_ARRAY_SIZE(pagePattern));
    setPage(a->pageList[5], kPageNames[5], pageGlobal, METRO_ARRAY_SIZE(pageGlobal));
    a->pageSet.numPages = 6;
    a->pageSet.pages = a->pageList;
    a->pagesInternal = internal;
    a->pagesMidiClock = midiClock;
}

static void loadPatternGroup(MetronomeAlgorithm* a, int g) {
    g = metro::clampi(g, 0, metro::kNumPatternGroups - 1);
    a->params[kParamPattern].max = (int16_t)(groupSize(g) - 1);
    a->params[kParamPattern].enumStrings = a->groupNames[g];
    a->shownGroup = g;
}

static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req,
                                const int32_t* specifications) {
    MetronomeAlgorithm* alg = new (ptrs.sram) MetronomeAlgorithm();
    for (int i = 0; i < kNumParams; ++i) alg->params[i] = kParameterTemplate[i];
    for (int g = 0; g < metro::kNumPatternGroups; ++g) {
        int n = groupSize(g);
        for (int i = 0; i < n && i < kMaxGroupSize; ++i)
            alg->groupNames[g][i] = metro::kPatterns[metro::kPatternGroupStart[g] + i].name;
        alg->groupNames[g][n < kMaxGroupSize ? n : kMaxGroupSize] = NULL;
    }
    loadPatternGroup(alg, 0);
    alg->params[kParamPattern].def = 0;
    alg->parameters = alg->params;
    buildPages(alg, kParameterTemplate[kParamSource].def == metro::kSourceInternal,
               kParameterTemplate[kParamMidiClockOut].def != 0);
    alg->parameterPages = &alg->pageSet;
    alg->engine.init((float)NT_globals.sampleRate);
    alg->metroToggle.init((float)NT_globals.sampleRate);
    return alg;
}

static int32_t algIndex(_NT_algorithm* self) { return NT_algorithmIndex(self); }

static void setParam(_NT_algorithm* self, int p, int value) {
    int32_t idx = algIndex(self);
    if (idx < 0) return;
    NT_setParameterFromUi((uint32_t)idx, (uint32_t)p + NT_parameterOffset(), (int16_t)value);
}

static void grey(_NT_algorithm* self, int p, bool g) {
    int32_t idx = algIndex(self);
    if (idx < 0) return;
    NT_setParameterGrayedOut((uint32_t)idx, (uint32_t)p + NT_parameterOffset(), g);
}

static int currentPatternIndex(const int16_t* v) {
    int g = metro::clampi(v[kParamPatGroup], 0, metro::kNumPatternGroups - 1);
    return metro::kPatternGroupStart[g] + metro::clampi(v[kParamPattern], 0, groupSize(g) - 1);
}

static void updateGreyOuts(_NT_algorithm* self) {
    const int16_t* v = self->v;
    const int src = v[kParamSource];
    const bool midi = src == metro::kSourceMIDI, cv = src == metro::kSourceCV, internal = src == metro::kSourceInternal;
    grey(self, kParamMidiMode, !midi);
    grey(self, kParamCvClockIn, !cv);
    grey(self, kParamCvClockRes, !cv);
    grey(self, kParamCvTransportIn, midi || v[kParamCvTransportMode] == metro::kCvNone);
    grey(self, kParamCvTransportMode, midi);
    grey(self, kParamInternalBpm, !internal);
    grey(self, kParamInternalRun, !internal);
    grey(self, kParamCountIn, !internal);
    grey(self, kParamCountMidi, !internal || !v[kParamMidiOut]);
    grey(self, kParamCountClock, !internal);
    grey(self, kParamMidiClockOut, midi);
    grey(self, kParamTapIn, !internal);
    grey(self, kParamTap, !internal);
    const bool poly = v[kParamPolyEnable] != 0;
    for (int p = kParamPolyPreset; p <= kParamPolyResetMode; ++p) grey(self, p, !poly);
    const bool pat = v[kParamPatEnable] != 0;
    for (int p = kParamPatGroup; p <= kParamPatResetMode; ++p) grey(self, p, !pat);
    for (int p = kParamPatTrigMode; p <= kParamPatVelOut; ++p) grey(self, p, !pat);
    if (pat) grey(self, kParamSwing, !metro::kPatterns[currentPatternIndex(v)].swing);
    const bool midiOut = v[kParamMidiOut] != 0;
    for (int p = kParamMidiDest; p <= kParamNoteGhost; ++p) grey(self, p, !midiOut);
    grey(self, kParamMidiClockOut, midi);  // a clock follower never re-sends MIDI clock
    grey(self, kParamRecNote, false);
}

static void refreshPages(_NT_algorithm* self) {
    MetronomeAlgorithm* a = (MetronomeAlgorithm*)self;
    const bool internal = self->v[kParamSource] == metro::kSourceInternal;
    const bool midiClock = self->v[kParamMidiClockOut] != 0;
    if (internal == (a->pagesInternal != 0) && midiClock == (a->pagesMidiClock != 0)) return;
    buildPages(a, internal, midiClock);
    int32_t idx = algIndex(self);
    if (idx >= 0) NT_updateParameterPages((uint32_t)idx);
}

static void parameterChanged(_NT_algorithm* self, int p) {
    MetronomeAlgorithm* a = (MetronomeAlgorithm*)self;
    const int16_t* v = self->v;
    switch (p) {
        case kParamPolyPreset: {
            int preset = v[kParamPolyPreset];
            if (preset <= 0 || preset >= metro::kNumPolyPresets) break;
            int x = metro::kPolyPresets[preset][0], y = metro::kPolyPresets[preset][1];
            if (v[kParamPolyX] == x && v[kParamPolyY] == y) { a->applyingPreset = 0; break; }
            a->applyingPreset = preset;
            setParam(self, kParamPolyX, x);
            setParam(self, kParamPolyY, y);
            break;
        }
        case kParamPolyX:
        case kParamPolyY: {
            if (a->applyingPreset) {
                int ax = metro::kPolyPresets[a->applyingPreset][0], ay = metro::kPolyPresets[a->applyingPreset][1];
                if (v[kParamPolyX] == ax && v[kParamPolyY] == ay) a->applyingPreset = 0;
                break;
            }
            int match = metro::polyPresetFor(v[kParamPolyX], v[kParamPolyY]);
            if (match != v[kParamPolyPreset]) setParam(self, kParamPolyPreset, match);
            break;
        }
        case kParamPatGroup: {
            int g = metro::clampi(v[kParamPatGroup], 0, metro::kNumPatternGroups - 1);
            if (g != a->shownGroup) {
                loadPatternGroup(a, g);
                int32_t idx = algIndex(self);
                if (idx >= 0) NT_updateParameterDefinition((uint32_t)idx, kParamPattern);
                // Editing the group starts at its first pattern. While a preset is loading
                // (first ~100 ms) keep the stored pattern unless it doesn't fit the group.
                const bool loading = a->blocks < 200;
                if (!loading || v[kParamPattern] > a->params[kParamPattern].max) setParam(self, kParamPattern, 0);
            }
            updateGreyOuts(self);
            break;
        }
        case kParamMidiClockOut:
        case kParamRecNote: {
            // MIDI clock out / Rec MIDI note need MIDI out (the master switch). Switching
            // MIDI out on for them sets MIDI layers to None, so no drum notes appear.
            const bool wants = p == kParamMidiClockOut ? v[kParamMidiClockOut] != 0 : v[kParamRecNote] < 128;
            if (wants && !v[kParamMidiOut] && a->blocks >= 200) {
                setParam(self, kParamMidiLayers, kMidiLayersNone);
                setParam(self, kParamMidiOut, 1);
            }
            refreshPages(self);
            updateGreyOuts(self);
            break;
        }
        case kParamSource:
            refreshPages(self);
            updateGreyOuts(self);
            break;
        case kParamPattern:
        case kParamMidiOut:
        case kParamCvTransportMode:
        case kParamPolyEnable:
        case kParamPatEnable:
            updateGreyOuts(self);
            break;
    }
}

static inline float* busPtr(float* busFrames, int bus, int numFrames) {
    return bus > 0 ? busFrames + (bus - 1) * numFrames : NULL;
}

static void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    MetronomeAlgorithm* a = (MetronomeAlgorithm*)self;
    const int numFrames = numFramesBy4 * 4;
    const int16_t* v = a->v;

    metro::Settings s;
    s.source = v[kParamSource];
    s.midiMode = v[kParamMidiMode];
    s.cvResolution = v[kParamCvClockRes];
    s.cvTransport = v[kParamCvTransportMode];
    s.internalBpm = v[kParamInternalBpm] * 0.1f;
    s.play = a->blocks >= 200 && v[kParamInternalRun] != 0;  // never start while a preset is loading
    s.meter = v[kParamTimeSig];
    s.division = v[kParamDivision];
    s.metroAudio = v[kParamMetroAudio] != 0;
    // Metro toggle in: each trigger flips Metro audio. Applied this block; the parameter is
    // updated too, so the screen and presets show the real state.
    {
        const int tb = v[kParamMetroToggleIn];
        if (tb != a->lastToggleBus) { a->metroToggle.prime(); a->lastToggleBus = tb; }
        if (a->metroOverride >= 0) {
            if ((int)s.metroAudio == a->metroOverride) a->metroOverride = -1;
            else s.metroAudio = a->metroOverride != 0;
        }
        const int flips = a->metroToggle.scan(busPtr(busFrames, tb, numFrames), numFrames);
        if (flips & 1) {
            s.metroAudio = !s.metroAudio;
            a->metroOverride = s.metroAudio ? 1 : 0;
            int32_t idx = algIndex(self);
            if (idx >= 0)
                NT_setParameterFromAudio((uint32_t)idx, kParamMetroAudio + NT_parameterOffset(), (int16_t)a->metroOverride);
        }
    }
    s.levelDown = v[kParamDownLevel] * 0.01f;
    s.levelBeat = v[kParamBeatLevel] * 0.01f;
    s.levelSub = v[kParamSubLevel] * 0.01f;
    s.baseClockRes = v[kParamBaseClockRes];
    s.baseResetMode = v[kParamBaseResetMode];
    s.polyEnable = v[kParamPolyEnable] != 0;
    s.polyX = v[kParamPolyX];
    s.polyY = v[kParamPolyY];
    s.polyAudio = v[kParamPolyAudio] != 0;
    s.polyLevel = v[kParamPolyLevel] * 0.01f;
    s.polyAccent = v[kParamPolyAccent] != 0;
    s.polyClockRes = v[kParamPolyClockRes];
    s.polyResetMode = v[kParamPolyResetMode];
    s.patEnable = v[kParamPatEnable] != 0;
    s.pattern = currentPatternIndex(v);
    s.swing = v[kParamSwing];
    s.patAudio = v[kParamPatAudio] != 0;
    s.patLevel = v[kParamPatLevel] * 0.01f;
    s.patAccent = v[kParamPatAccent] != 0;
    s.patResetMode = v[kParamPatResetMode];
    s.master = v[kParamMaster] * 0.01f;
    s.mute = v[kParamMute] != 0;
    s.sound = v[kParamSound];
    s.patTrigMode = v[kParamPatTrigMode];
    s.midiOut = v[kParamMidiOut] != 0;
    s.midiChannel = v[kParamMidiChannel];
    s.midiLayers = kMidiLayerMask[metro::clampi(v[kParamMidiLayers], 0, 7)];
    for (int i = 0; i <= metro::kVoicePatAccent; ++i) s.midiNote[i] = v[kParamNoteDown + i];
    s.midiNote[metro::kVoicePolyAccent] = v[kParamNotePolyAccent];
    s.midiNote[metro::kVoicePatGhost] = v[kParamNoteGhost];
    s.countInBars = v[kParamCountIn];
    s.countInMidi = v[kParamCountMidi] != 0;
    s.countInClock = v[kParamCountClock] != 0;
    s.midiClockOut = v[kParamMidiClockOut] != 0;
    s.recArm = v[kParamRecArm] != 0;
    s.recLengthBars = kRecLengthBars[metro::clampi(v[kParamRecLength], 0, 5)];
    s.recMode = v[kParamRecMode];
    s.recNote = v[kParamRecNote] >= 128 ? -1 : v[kParamRecNote];

    const int clockBus = v[kParamCvClockIn];
    const int transportBus = v[kParamCvTransportIn];
    const int tapBus = v[kParamTapIn];
    if (clockBus != a->lastClockBus || transportBus != a->lastTransportBus || tapBus != a->lastTapBus) {
        a->engine.primeInputs();  // adopt the new bus' level without a false edge
        a->lastClockBus = clockBus;
        a->lastTransportBus = transportBus;
        a->lastTapBus = tapBus;
    }
    // Tap parameter: each increase is one tap (turn right repeatedly; left is ignored).
    if (a->lastTap >= 0 && v[kParamTap] > a->lastTap && s.source == metro::kSourceInternal) a->engine.tapNow();
    a->lastTap = v[kParamTap];
    // Encoder-button taps from the custom UI
    const uint32_t req = a->tapRequests;
    if (req != a->tapsHandled) {
        if (s.source == metro::kSourceInternal) a->engine.tapNow();
        a->tapsHandled = req;
    }
    if (a->blocks == 0 && v[kParamInternalRun]) setParam(self, kParamInternalRun, 0);  // stopped after loading
    if (a->blocks < 0xFFFFFFFFu) ++a->blocks;

    metro::Outputs o;
    o.audio[metro::kLayerMetro] = busPtr(busFrames, v[kParamMetroOut], numFrames);
    o.replace[metro::kLayerMetro] = v[kParamMetroOutMode] != 0;
    o.audio[metro::kLayerPoly] = s.polyEnable ? busPtr(busFrames, v[kParamPolyOut], numFrames) : NULL;
    o.replace[metro::kLayerPoly] = v[kParamPolyOutMode] != 0;
    o.audio[metro::kLayerPattern] = s.patEnable ? busPtr(busFrames, v[kParamPatOut], numFrames) : NULL;
    o.replace[metro::kLayerPattern] = v[kParamPatOutMode] != 0;
    o.trig[metro::kOutBaseClock] = busPtr(busFrames, v[kParamBaseClockOut], numFrames);
    o.trig[metro::kOutBaseReset] = busPtr(busFrames, v[kParamBaseResetOut], numFrames);
    o.trig[metro::kOutPolyClock] = s.polyEnable ? busPtr(busFrames, v[kParamPolyClockOut], numFrames) : NULL;
    o.trig[metro::kOutPolyReset] = s.polyEnable ? busPtr(busFrames, v[kParamPolyResetOut], numFrames) : NULL;
    o.trig[metro::kOutPatTrig] = s.patEnable ? busPtr(busFrames, v[kParamPatTrigOut], numFrames) : NULL;
    o.trig[metro::kOutPatAccent] = s.patEnable ? busPtr(busFrames, v[kParamPatAccentOut], numFrames) : NULL;
    o.trig[metro::kOutPatReset] = s.patEnable ? busPtr(busFrames, v[kParamPatResetOut], numFrames) : NULL;
    o.trig[metro::kOutPatGhost] = s.patEnable ? busPtr(busFrames, v[kParamPatGhostOut], numFrames) : NULL;
    o.level[metro::kLvlVelocity] = s.patEnable ? busPtr(busFrames, v[kParamPatVelOut], numFrames) : NULL;
    float* recBus = busPtr(busFrames, v[kParamRecOut], numFrames);
    o.trig[metro::kOutRecTrig] = s.recMode == metro::kRecTrigger ? recBus : NULL;
    o.level[metro::kLvlRecGate] = s.recMode == metro::kRecGate ? recBus : NULL;
    o.level[metro::kLvlRun] = busPtr(busFrames, v[kParamRunOut], numFrames);
    o.level[metro::kLvlCountIn] = NULL;

    const bool cvSource = s.source == metro::kSourceCV;
    a->engine.process(s, cvSource ? busPtr(busFrames, clockBus, numFrames) : NULL,
                      s.source != metro::kSourceMIDI ? busPtr(busFrames, transportBus, numFrames) : NULL, o,
                      numFrames, s.source == metro::kSourceInternal ? busPtr(busFrames, tapBus, numFrames) : NULL);

    // MIDI drum output
    const int nm = a->engine.midiCount();
    if (nm) {
        const uint32_t dest = kMidiDestMask[metro::clampi(v[kParamMidiDest], 0, 5)];
        const metro::MidiMsg* m = a->engine.midiMessages();
        for (int i = 0; i < nm; ++i) {
            if (m[i].len == 1) NT_sendMidiByte(dest, m[i].b0);
            else NT_sendMidi3ByteMessage(dest, m[i].b0, m[i].b1, m[i].b2);
        }
    }

    // Keep the Play and Rec arm parameters in step with the engine (gate / toggle starts,
    // quantized stops, finished takes).
    if (a->blocks >= 200 && s.source == metro::kSourceInternal) {
        const bool on = a->engine.transportOn();
        if (on != (v[kParamInternalRun] != 0)) setParam(self, kParamInternalRun, on ? 1 : 0);
    }
    if (a->engine.takeRecDisarm() && v[kParamRecArm]) setParam(self, kParamRecArm, 0);

    float tapped;
    if (a->engine.takeTapBpm(tapped)) {
        int32_t idx = algIndex(self);
        if (idx >= 0)
            NT_setParameterFromAudio((uint32_t)idx, kParamInternalBpm + NT_parameterOffset(),
                                     (int16_t)(tapped * 10.0f + 0.5f));
    }
}

static void midiRealtime(_NT_algorithm* self, uint8_t byte) {
    ((MetronomeAlgorithm*)self)->engine.midiRealtime(byte);
}

static void midiMessage(_NT_algorithm* self, uint8_t byte0, uint8_t byte1, uint8_t byte2) {
    ((MetronomeAlgorithm*)self)->engine.midiMessage(byte0, byte1, byte2);
}

// ---------------------------------------------------------------- display

static char* appendStr(char* p, const char* s) {
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

static char* appendInt(char* p, int v) {
    char tmp[8];
    int n = 0;
    if (v < 0) { *p++ = '-'; v = -v; }
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 7);
    while (n) *p++ = tmp[--n];
    *p = 0;
    return p;
}

static void formatBpm(char* buf, float bpm) {
    if (bpm <= 0.0f || bpm > 999.9f) {
        appendStr(buf, "---.-");
        return;
    }
    int tenths = (int)(bpm * 10.0f + 0.5f);
    char* p = appendInt(buf, tenths / 10);
    *p++ = '.';
    *p++ = (char)('0' + tenths % 10);
    *p = 0;
}

static bool draw(_NT_algorithm* self) {
    MetronomeAlgorithm* a = (MetronomeAlgorithm*)self;
    const int16_t* v = a->v;
    const metro::Engine& e = a->engine;

    static const char* const statusText[] = {"STOPPED", "WAITING", "ACQUIRING", "RUNNING", "CLOCK LOST", "COUNT-IN"};
    static const char* const sourceText[] = {"CV", "MIDI", "INTERNAL"};
    static const char* const divText[] = {"beats", "1/4", "1/8", "1/16"};

    const int st = (int)e.status();
    const bool playing = st == metro::kStatusRunning || st == metro::kStatusAcquiring || st == metro::kStatusCountIn;

    NT_drawText(0, 20, sourceText[metro::clampi(v[kParamSource], 0, 2)], 15, kNT_textLeft, kNT_textNormal);
    if (v[kParamSource] == metro::kSourceMIDI && v[kParamMidiMode] == metro::kMidiClockOnly)
        NT_drawText(30, 20, "(clock only)", 7, kNT_textLeft, kNT_textNormal);
    NT_drawText(255, 20, statusText[st], st == metro::kStatusRunning ? 15 : 10, kNT_textRight, kNT_textNormal);

    char buf[40];
    const bool counting = st == metro::kStatusCountIn;
    if (counting) {
        char* p = appendStr(buf, "IN ");
        appendInt(p, e.countBarsLeft());
        NT_drawText(0, 44, buf, 15, kNT_textLeft, kNT_textLarge);
        formatBpm(buf, e.bpm());
        NT_drawText(80, 44, buf, 8, kNT_textLeft, kNT_textNormal);
    } else {
        formatBpm(buf, e.bpm());
        NT_drawText(0, 44, buf, 15, kNT_textLeft, kNT_textLarge);
        NT_drawText(80, 44, "BPM", 8, kNT_textLeft, kNT_textNormal);
    }
    NT_drawText(80, 27, "v" METRONOMICON_VERSION, 5, kNT_textLeft, kNT_textTiny);
    if (v[kParamSource] == metro::kSourceInternal)
        NT_drawText(80, 34, st == metro::kStatusStopped ? "L:play R:tap" : "L:stop R:tap", 6, kNT_textLeft, kNT_textTiny);

    const int beats = e.beatsPerBar();
    const int cur = counting ? e.countBeat() : e.beat();
    const int gap = 4, top = 26, bottom = 42, areaW = 140;
    int cellW = (areaW - (beats - 1) * gap) / beats;
    if (cellW > 24) cellW = 24;
    const int x0 = 255 - beats * cellW - (beats - 1) * gap;
    for (int i = 0; i < beats; ++i) {
        int x = x0 + i * (cellW + gap);
        if (playing && cur == i)
            NT_drawShapeI(kNT_rectangle, x, top, x + cellW - 1, bottom, 15);
        else
            NT_drawShapeI(kNT_box, x, top, x + cellW - 1, bottom, i == 0 ? 10 : 5);
    }
    if (playing && cur < 0) NT_drawText(x0 - 4, 42, "?", 10, kNT_textRight, kNT_textNormal);

    if (v[kParamPolyEnable]) {
        char* p = appendStr(buf, "POLY ");
        p = appendInt(p, v[kParamPolyX]);
        p = appendStr(p, ":");
        p = appendInt(p, v[kParamPolyY]);
        int pulse = e.polyPulse();
        if (playing && pulse >= 0) {
            p = appendStr(p, "  ");
            p = appendInt(p, pulse + 1);
            p = appendStr(p, "/");
            appendInt(p, v[kParamPolyX]);
        }
        NT_drawText(0, 52, buf, v[kParamPolyAudio] ? 12 : 6, kNT_textLeft, kNT_textTiny);
    }
    if (v[kParamPatEnable]) {
        char* p = appendStr(buf, "PAT ");
        appendStr(p, metro::kPatterns[metro::clampi(e.activePattern(), 0, metro::kNumPatterns - 1)].name);
        NT_drawText(255, 52, buf, v[kParamPatAudio] ? 12 : 6, kNT_textRight, kNT_textTiny);
    }

    NT_drawText(0, 63, enumTimeSig[metro::clampi(v[kParamTimeSig], 0, 10)], 12, kNT_textLeft, kNT_textNormal);
    NT_drawText(40, 63, divText[metro::clampi(v[kParamDivision], 0, 3)], 12, kNT_textLeft, kNT_textNormal);
    const int rs = e.recState();
    if (rs == metro::kRecRecording || rs == metro::kRecEnding) {
        char* p = appendStr(buf, rs == metro::kRecEnding ? "REC END " : "REC ");
        p = appendInt(p, e.recBars() + 1);
        if (v[kParamRecLength] > 0) { p = appendStr(p, "/"); appendInt(p, kRecLengthBars[v[kParamRecLength]]); }
        NT_drawText(255, 63, buf, 15, kNT_textRight, kNT_textNormal);
    } else if (rs == metro::kRecWaiting) NT_drawText(255, 63, "ARMED", 15, kNT_textRight, kNT_textNormal);
    else if (v[kParamMute]) NT_drawText(255, 63, "MUTED", 15, kNT_textRight, kNT_textNormal);
    else if (!v[kParamMetroAudio]) NT_drawText(255, 63, "metro off", 8, kNT_textRight, kNT_textNormal);

    return false;
}

// ---------------------------------------------------------------- custom UI
// With the internal clock selected, pressing the right encoder on the algorithm's
// display taps the tempo. Everything else keeps its normal behaviour.

static uint32_t hasCustomUi(_NT_algorithm* self) {
    return self->v[kParamSource] == metro::kSourceInternal ? (uint32_t)(kNT_encoderButtonL | kNT_encoderButtonR) : 0u;
}

static void customUi(_NT_algorithm* self, const _NT_uiData& data) {
    MetronomeAlgorithm* a = (MetronomeAlgorithm*)self;
    if ((data.controls & kNT_encoderButtonR) && !(data.lastButtons & kNT_encoderButtonR)) a->tapRequests = a->tapRequests + 1;
    if ((data.controls & kNT_encoderButtonL) && !(data.lastButtons & kNT_encoderButtonL))
        setParam(self, kParamInternalRun, self->v[kParamInternalRun] ? 0 : 1);  // play / stop
}

static int parameterString(_NT_algorithm* self, int p, int v, char* buff) {
    if (p == kParamRecNote) {
        if (v >= 128) return (int)(appendStr(buff, "Off") - buff);
        static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        char* q = appendStr(buff, names[v % 12]);
        q = appendInt(q, v / 12 - 1);
        q = appendStr(q, " (");
        q = appendInt(q, v);
        q = appendStr(q, ")");
        return (int)(q - buff);
    }
    if (p != kParamTap) return 0;
    const char* text = self->v[kParamSource] == metro::kSourceInternal ? "turn right = tap" : "internal clock only";
    char* q = appendStr(buff, text);
    return (int)(q - buff);
}

// ---------------------------------------------------------------- factory

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('M', 'e', 't', 'r'),
    .name = "Metronomicon",
    .description = "Metronomicon " METRONOMICON_VERSION ": metronome, count-in & rec, polyrhythm, patterns",
    .numSpecifications = 0,
    .specifications = NULL,
    .calculateStaticRequirements = NULL,
    .initialise = NULL,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = draw,
    .midiRealtime = midiRealtime,
    .midiMessage = midiMessage,
    .tags = kNT_tagUtility,
    .hasCustomUi = hasCustomUi,
    .customUi = customUi,
    .parameterString = parameterString,
};

uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
        case kNT_selector_version:
            return kNT_apiVersionCurrent;
        case kNT_selector_numFactories:
            return 1;
        case kNT_selector_factoryInfo:
            return (uintptr_t)((data == 0) ? &factory : NULL);
    }
    return 0;
}
