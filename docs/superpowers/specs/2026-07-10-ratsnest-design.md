# ratsnest — Design

**Date:** 2026-07-10
**Status:** Approved design, pre-implementation

## 1. Vision

A software modular synthesizer for Linux where **the patch is a plain-text file**.
Hand-written C++ DSP underneath, PipeWire out, hot-reload on save so editing a
patch feels like turning a knob. A graphical node canvas arrives later as
another *view* of the same file — never as the source of truth.

What it is not: a DAW, a plugin host, a livecoding performance language, or a
module marketplace. It is a personal instrument whose patches are readable,
diffable, versionable, and shareable as plain text.

Primary use cases, in build order: generative drone/ambient, MIDI keyboard
performance, sequenced patterns (Berlin school / techno), sound design and
recording.

## 2. Design principles

1. **Patch as text.** The `.patch` file is the single source of truth. Anything
   a GUI can do must round-trip through the file.
2. **Everything is one signal.** Audio, CV, gates, clocks are all floats at
   audio rate. Any output patches to any input. No separate control rate.
3. **No normalling.** Hardware's unpatched-jack magic (Plaits' attenuverter
   rerouting, Maths' bus-dropout, Veils' cascade) exists because panels are
   fixed. Our modules expose ordinary named params with defaults. Nothing
   implicit.
4. **Small, single-purpose modules.** Hardware multifunction modules (Disting,
   Pam's, Maths-as-five-things) are panel-economics workarounds. Cables are
   free in text; compose instead.
5. **Knob-per-function beats menus.** The regret-thread consensus: people sell
   modules that hide state. Every parameter is a named field in the file.
6. **Hot-reload is the instrument.** State survives edits; sound never stops
   for a typo. This property is make-or-break and gets engineering priority.
7. **Utilities outnumber sound sources.** The unanimous lesson of every mature
   system (VCV's late additions, Reaktor Bento, first-rack wisdom).

## 3. The patch DSL

### 3.1 Grammar

Two statement types only — modules and cables — mirroring hardware's two nouns.

```ebnf
patch     = { statement } ;
statement = decl | cable ;
decl      = ident "=" type [ "{" [ param { "," param } ] "}" ] ;
param     = ident ":" value ;
value     = number [ unit ] | ident | string | array ;
array     = "[" [ value { "," value } ] "]" ;
cable     = source "->" dest [ ":" number ] ;
source    = ident [ "." port ] | number [ unit ] ;
dest      = ident [ "." port ] ;
comment   = "#" … end-of-line ;
```

- **Units** are optional sugar over floats: `hz`, `khz`, `ms`, `s`, `bpm`.
- **Value kinds:** scalar (`0.4`, `800hz`), enum (`saw`, `minor`), string
  (`"reel1.wav"`), array (`[0, 3, 5, 7, 10]`, `[x, x, _, x]` for gate
  patterns). Arrays serve sequencers, scales, and mixer channel params.
- **Literal sources:** a bare number may be a cable source
  (`0.25 -> qnt.transpose`) — this is the offset/constant module, dissolved
  into grammar.
- Statement order is free; names must be unique; `out` is a reserved built-in.

### 3.2 Semantics

- **Declarations are knob positions.** `filt = ladder { cutoff: 800hz }` is
  panel state.
- **Cables are patch cords with built-in attenuverters.** `lfo -> filt.cutoff
  : 0.5` — depth is per-cable, negative inverts, default `1.0`.
- **Modulation sums onto the knob.** A param's effective value is its declared
  value plus the sum of all cables targeting it. Multiple cables into any port
  sum (mixing into audio inputs included).
- **Default ports.** Every module has a main `in`/`out`; `mix -> filt` means
  main-out to main-in. Named ports for everything else (`filt.cutoff`,
  `out.left`).
- **Seeds.** Every stochastic module takes an optional `seed:` int. Omitted =
  nondeterministic (hardware spirit); present = reproducible renders (shared
  patches, regression tests).

### 3.3 Hot-reload semantics

On save, the engine diffs old vs new IR:

- **Param-only change:** engine glides each changed param over ~20 ms.
  Feels like a knob turn.
- **Topology change:** new graph built off-thread; every module whose
  `(name, type)` pair survives has its internal state migrated (oscillator
  phase, envelope stage, delay/register buffers); pointer-swap, then ~50 ms
  linear crossfade old→new. Renaming a module intentionally resets it.
  A topology save that lands during that crossfade is held, newest only,
  and applied as soon as the fade completes; it is never dropped.
- **Parse/validation error:** last good graph keeps playing; error reported
  with line/column. The drone never stops because of a typo.

### 3.4 Canvas sidecar

The future GUI stores node positions in `<name>.patch.layout` (JSON), never in
the patch. Losing the layout loses nothing musical; diffs stay musical-only.

### 3.5 Deferred language features

Recorded here so v1 doesn't accidentally preclude them:

- **Enum-param modulation** (`lfo -> voice.model`): signal offsets the enum
  index, rounded. Deliberately deferred; v1 enums are static.
- **Subpatches / include:** a patch usable as a module with declared ports.
- **Expressions** (`freq: 55 * 2`) — probably never; a patch is data, not code.

`voice { … }` for polyphony (§6) is no longer deferred: implemented as
compile-time dotted-namespace groups over `alloc`'s indexed banks, no new
statement types or runtime voice machinery. Full design:
`2026-07-11-voice-alloc-design.md`.

## 4. Signal conventions

| Signal | Range/meaning |
|---|---|
| Audio, bipolar CV | ±1.0 |
| Unipolar CV (envelopes, gates) | 0..1 |
| Pitch | `v_oct`, octaves, offsetting the receiving module's `freq` param (0.0 = declared freq); `midi` emits 0.0 for A3/220 Hz |
| Gate/trigger threshold | rising through 0.5 |
| Sample rate | device rate via PipeWire (48 kHz typical); no internal resampling in v1 |

Conventions are enforced by every built-in and documented per port; consistent
defaults are "the 35th module."

## 5. Module palette (36)

Sound sources are the skeleton, envelopes/VCAs the muscles, utilities the
brain. Checked against: the consensus-10 every software modular ships, VCV
Fundamental's documented gaps (clock, quantizer, reverb, west-coast path — all
covered), 0-Coast/Mother-32 voice completeness, and generative-patch
essentials.

### Phase 1 — the drone/generative box (18)

| Module | Role | Key params | Key ports |
|---|---|---|---|
| `vco` | band-limited oscillator (shared Fourier-series tables; analytic sine) | `freq`, `wave` (sine\|tri\|saw\|pulse), `pw` | in: `v_oct` `fm` `pwm` `sync` `phase` · out: `out` |
| `noise` | noise source | `color` (white\|pink\|brown\|blue\|violet), `seed` | out |
| `svf` | state-variable filter | `cutoff`, `res` | in · out: `lp` (default) `bp` `hp` |
| `ladder` | 4-pole ZDF ladder LPF | `cutoff`, `res`, `drive` | in · out |
| `vca` | amplifier | `gain`, `response` (lin\|exp) | `in` `cv` · out |
| `lpg` | vactrol-model low pass gate | `damp`, `decay` | `in` `cv` `strike` · out |
| `fold` | wavefolder | `fold`, `sym` | `in` `fold` · out |
| `drive` | saturation | `amount`, `mode` (soft\|hard) | in · out |
| `func` | Maths-style function generator | `rise`, `fall`, `curve`, `cycle` (bool) | in: `trig` `in`(slew) `rise` `fall` · out: `out` `eor` `eoc` |
| `lfo` | multi-wave LFO | `freq`, `wave` (sine\|tri\|saw\|square), `phase` | in: `freq` `reset` `phase` · out |
| `random` | smooth/stepped wandering voltage | `rate`, `smooth`, `seed` | out: `out` `stepped` |
| `sh` | sample & hold / track & hold | `mode` (sample\|track) | `in` `trig` · out |
| `quant` | quantizer | `scale` (array), `mode` (continuous\|clock), `root`, `transpose` | `in` `root` `transpose` `clock` `mask` `degree` · out: `out` `changed`(one-sample trigger) |
| `math` | signal math (= ring mod, rectifier, min/max, …) | `op` (add\|sub\|mult\|min\|max\|abs\|clip\|wrap\|exp2), `b` | `a` `b` · out |
| `xfade` | linear crossfader, optional equal-power law | `mix`, `law` | `a` `b` `mix` · out |
| `mix` | stereo mixer | `levels` (array), `pans` (array); defaults 1.0 / center | `in1..in8` `lvl1..lvl8` · out: `out_l` `out_r` |
| `scope` | probe/meter for TUI & logs | `label` | in |
| `out` | master out: float sum; PipeWire boundary clips | `level` | `left` `right` `in`(→both) |

Build order note: `svf` before `ladder` (much easier; instant gratification),
`vco` first overall (polyBLEP is DSP lesson one).

### Phase 2 — playable (+5)

| Module | Role | Notes |
|---|---|---|
| `midi` | note/CC → CV | cfg `channel`(0=omni) `voices`(1..8) `mode`(last\|lowest) `bend`(semitones) `cc`(≤4 controller numbers); outs banks `gate1..8` `voct1..8` `vel1..8`, plus `bend` `at` `cc1..cc4`. No mono `gate`/`voct`/`vel` outs — those bare names are reserved by the voice-group bank-resolution rule (§6); a mono synth is `voices: 1` and `mid.gate1`. PipeWire MIDI |
| `adsr` | keyboard envelope | ins `gate` `attack` `decay` `sustain`(0..1, live) `release` · out (0..1); `curve` (lin\|exp\|log\|quad; exp is a true RC, stage time = T60); release from current level, retrigger from current level |
| `delay` | tape-style feedback delay, 10 s max | ins `in` `time`(s) `pitch`(v_oct: t·2^−p) `feedback`(0..1.2, tanh loop) `mix` `tone`(Hz) `slew`(s) · out. Clock-sync stays patchable (clock→seconds module later) |
| `reverb` | Dattorro 1997 plate | ins `in_l` `in_r` `decay` `damp` `mix` `freeze`(gate) · outs `out_l` `out_r`; cfg `predelay`(s) `bandwidth` `mod_rate` `mod_depth`. FDN variant is a Phase 5+ sibling |
| `alloc` | voice allocator | cfg `n`(1..8, default 4) `mode`(rr\|unison); ins `gate` `voct` `vel`; outs `gate1..8` `voct1..8` `vel1..8` `spread1..8`. Distributes a mono note stream across indexed banks for `voice` groups; see `2026-07-11-voice-alloc-design.md` |

### Phase 3 — the sequencer wing (+9)

| Module | Role | Notes |
|---|---|---|
| `clock` | master clock | `bpm`; ins `run` `reset` · outs `out` + `run` gate |
| `div` | clock divider | `by`; ins `in` `reset` · out. Multiplication deferred |
| `euclid` | Euclidean rhythm | ins `clock` `reset` `steps`(1..64) `hits`(0..steps) `rot`(wrapped); outs `trig`(one sample) `gate`(clock width). Live integer params round; reset makes the coincident/next edge step zero |
| `stepseq` | step sequencer | cfg `steps`/`gates`/`lens`/`ties`/`vels` arrays, `dir` (forward\|reverse\|pingpong\|random), `seed`; ins `clock` `reset` `len` `transpose`; outs `cv` `trig` `gate` `eoc` `vel` |
| `turing` | lockable shift register | ins `clock` `write` `clear` `len`(1..16) `prob`; cfg `seed`; outs `out` `pulse` `bit1..bit8`. Volts/Pulses expanders = cables |
| `bern` | Bernoulli gate | ins `gate` `prob` `reset` `length` `reroll`; cfg `seed`; mutually exclusive gate-width outs `a` `b`, held draw `value` |
| `logic` | gate logic + comparator | ins `a` `b` `threshold`; cfg `op` (and\|or\|xor\|not\|cmp); out |
| `switch` | sequential/addressed switch | ins `in1..in8` `clock` `reset` `addr` `n`; cfg `mode` (seq\|addr); out |
| `tempo` | musical time conversion | ins `bpm` `beats`; outs `seconds` `hz` |

Transposition (Berlin school) needs no module: pitch CVs sum on `v_oct`.
Drums need no module: trigger → `lpg.strike` (kick), `noise`+`svf.hp`+fast
`func` (hats), sine + noise bursts (snare).

### Phase 4 — sound design (+4)

(implemented 2026-07-14; design `2026-07-14-phase4-io-modules-design.md`. The
three I/O members share a hand-written WAV codec in `ms::io` — no new
dependency — and `extin` adds a `wants_audio_in` engine hook mirroring the MIDI
bridge. See `docs/modules.md` for behavior.)

| Module | Role | Notes |
|---|---|---|
| `sampler` | file playback | `file` (string), `pitch`, `loop`; outs `out` `eos` |
| `rec` | tap any point to WAV | `file`; ins `in_l` `in_r` `gate` |
| `extin` | external audio in | PipeWire capture ports |
| `follow` | envelope follower | `attack` `release`; outs `env` `gate` — the audio→CV bridge |

### Phase 5 — mastering modules (+6)

| Module | Role | Notes |
|---|---|---|
| `dcblock` | stereo DC blocker | first-order high-pass filter, parameter smoothing around 20 ms, denormal flushing |
| `stereo` | stereo utility | normalized M/S width, equal-power balance, level-sensed mono/swap, 300 ms correlation meter |
| `compressor` | stereo linked compressor | stereo-linked feed-forward log-domain soft-knee curve, attack/release smoothing, makeup and dry/wet |
| `eq` | stereo mastering EQ | five-band parametric EQ (HPF, low shelf, mid1 peaking, mid2 peaking, high shelf), shared coefficients, 20 ms parameter smoothing, zero-gain bypass |
| `clipper` | oversampled stereo clipper | hard/soft (tanh) oversampled clipping, 4x polyphase allpass IIR half-band oversampling, dry-delay latency alignment |
| `limiter` | lookahead peak limiter | fixed 2.0 ms lookahead, peak detection window, fast 0.1 ms attack, log-domain smoothing |

### Phase 6 — granular (+1)

| Module | Role | Notes |
|---|---|---|
| `grains` | granular processor | Clouds core only (buffer + scheduler + 64-grain player); blend section composes from `xfade`/`delay`/`reverb`/`stereo`; see `2026-07-14-grains-design.md` |

### Explicitly out of scope (Phase 5+ candidates)

Macro-oscillator (Plaits-alike), dedicated drum voice, `push` manual-gate
(needs a GUI panel), wavetable VCO, module plugin ABI. (The physical-modeling
voice landed as `modal`/`string`; see
`docs/superpowers/specs/2026-07-14-resonator-design.md`.)

## 6. Polyphony (implemented: see §3.5 and the voice-alloc spec)

**Per-voice patch instancing** (the Bitwig Poly Grid / SuperCollider model): a
`voice` group instantiates its members once per voice; modules never know
polyphony exists. Rejected alternative: VCV-style poly cables (documented
confusion; needed four modules just to manage it). The decision is made now
so no port or module design bakes in mono assumptions it needn't.

Designed in full (before `midi`, so midi's ports target a proven mechanism):
`2026-07-11-voice-alloc-design.md` — an ordinary `alloc` module with indexed
port banks plus compile-time expansion over dotted names (`lead.osc`); no new
statement types, no runtime voice machinery.

## 7. Architecture

### 7.1 Process model

Per-sample processing in topologically sorted order. Feedback edges read the
previous sample (one-sample delay — what a physical cable loop costs). This
makes self-patching (`func.eoc -> func.trig`, audio-rate FM anywhere) work
with no special cases. VCV-proven at scales far beyond ours.

### 7.2 Components

```
patch.file ──▶ dsl/     lexer → parser → Patch IR (modules, cables, params)
                │            errors with line/col, suggestions
                ▼
              graph/    IR → validated graph → topo order + feedback edges
                │       diffing + state migration by (name, type)
                ▼
   ┌─ non-RT (main thread) ────────────────┐  lock-free SPSC queues
   │ inotify watcher · CLI · WAV writer    │ ◀──▶ ┌─ RT audio thread ─────┐
   │ scope/meter sink · (later: MIDI, TUI, │      │ run compiled graph;   │
   │ GUI canvas)                           │      │ no alloc, no locks    │
   └───────────────────────────────────────┘      └───────────────────────┘
```

- **`dsl/`** — hand-written lexer + recursive-descent parser (the grammar is
  tiny) → Patch IR. Owns diagnostics quality.
- **`graph/`** — IR validation (unknown types/ports/params, arity), topo sort,
  feedback-edge marking, graph diffing, state migration.
- **`dsp/`** — one file per module. Module interface: static port/param
  metadata + `process()` (one sample) + state save/restore for migration.
  Static registry; no plugin loading.
- **`engine/`** — RT runner, PipeWire backend (`pw_stream`), SPSC command
  queue in (param glides, graph swaps), ring buffers out (meters, scope taps,
  record feed). Float output preserves headroom; the device boundary clips. NaN/denormal guards protect the signal path. (`pw_filter`
  DSP nodes are never auto-routed by WirePlumber 0.5.15 — `create-item.lua`
  string-matches `media.class` "Audio" to `si-audio-adapter`, which requires
  node-level `EnumFormat` that filter nodes don't expose; `pw_stream` was
  chosen so WirePlumber's default policy can auto-link the node.)
- **`io/`** — inotify file watching, WAV writing (disk thread fed by ring
  buffer).

### 7.3 RT discipline

The audio thread only: runs the graph, drains the command queue, feeds ring
buffers. Never allocates, locks, or touches files. Graph handover is pointer
swap; old graph freed on the main thread.

### 7.4 CLI (the whole Phase 1 UX)

```
ratsnest play   drone.patch          # watch + play + meters
ratsnest render drone.patch -d 30s -o take.wav   # offline, faster than RT
ratsnest render drone.patch -d 300s --stats 8s   # + peak/RMS/DC level timeline
ratsnest check  drone.patch          # parse/validate — editor lint
ratsnest describe [module_type]      # JSON interfaces from the runtime registry
```

### 7.5 Error handling

- Parse/validation errors: report, keep last good graph running (§3.3).
- Runtime: denormal flushing (FTZ/DAZ), NaN guard per module output (replace
  with 0, count + report). Device output clips to -1..1; use an explicit
  limiter for dynamics control.
- PipeWire xruns/disconnects: reconnect with silence fill; never crash the
  session.

## 8. Testing

- **Parser:** unit tests per production; golden error-message tests (message
  quality is a feature).
- **DSP per module:** deterministic checks — FFT alias measurement on `vco`,
  frequency response on filters, envelope timing tolerances.
- **Integration:** seeded example patches in `patches/` rendered offline and
  compared to golden WAVs (hash or spectral tolerance). Doubles as the demo
  library.
- **Hot-reload:** state-migration tests (edit param → no discontinuity beyond
  glide; rename → reset; topology change → crossfade, no clicks measured).

## 9. Tooling & layout

C++20 · CMake · doctest · deps: PipeWire + doctest only · mise tasks wrapping
configure/build/test/run.

```
ratsnest/
  src/{dsl,graph,dsp,engine,io}/   main.cpp
  tests/
  patches/            # examples = integration fixtures
  docs/superpowers/specs/
```

## 10. Phasing

1. **Drone box** — DSL, engine, hot-reload, 18 modules, `play`/`render`/
   `check`. Exit: an evening of generative ambient that's genuinely good.
2. **Playable** — MIDI in, `adsr`, `delay`, `reverb`. Implemented order
   deliberately inverted the listed sequence: polyphony (`alloc` + the
   `voice` block, §6) landed *before* `midi` so midi's indexed banks
   (`gate1..8`/`voct1..8`/`vel1..8`) target a voice-group mechanism already
   proven by `alloc` and `patches/strum.patch`, rather than midi inventing
   its own polyphony story. See `patches/keys-poly.patch` for the combined
   result.
3. **Sequencer wing** — clock/div/euclid/stepseq/turing/bern/logic/switch.
4. **Sound design** — sampler, rec, extin, follow.
5. **Later** — GUI canvas over the same file (layout sidecar), granular,
   physical modeling.

**GUI: shelved (2026-10-19).** A native SDL3/ImGui node editor plus a stdio
MCP bridge ("studio") was built as commit `4858f5d` on `feat/longwave` and
deliberately not merged: it pulled effort away from the engine. Current
priority is correctness and performance of the CLI/DSP core. The commit stays
on that branch for reference; reviving it is a fresh design decision, and
rule 1 (the patch file is the source of truth) still binds any GUI.

## 11. Prior art notes

- **DROID** — hardware CV computer programmed via plain-text config; closest
  cousin to patch-as-text. Studied in depth (see
  `docs/research/2026-07-10-droid-prior-art.md`). Five years of its user
  feedback validate this design's core choices: named instances (DROID's
  positional identity silently corrupts state on reorder — our worst-flaw
  avoided), per-cable depth as the only inline math (their hard `A·B+C` limit
  proved fast and idiomatically right), and no expression creep. Lessons
  adopted: **freeze the grammar, grow the vocabulary** (DROID shipped 7
  firmware generations with zero syntax changes — version the module library,
  not the language); `check` warns on unreachable modules / never-read
  outputs; runtime introspection (watch any port) is a Phase 1b deliverable,
  not a nicety — its absence was DROID's longest-running complaint; and for
  the GUI era, DROID's "CV-driven values are never state" boundary.
- **VCV Fundamental history** — its late utility additions and famous gaps
  directly shaped the palette.
- **Pd/SuperCollider** — evidence that raw signal math (`math` module)
  replaces a half-dozen dedicated utility modules.
- **0-Coast** — the west-coast trio (`fold` + `func` + `lpg`) is what makes a
  small system percussion- and generative-capable.
