# Core Module Sets in Established Software Modulars — Research Synthesis

*Research agent report, 2026-07-10. Preserved verbatim.*

## 1. Per-system findings

### VCV Rack "Fundamental" (now "VCV Free") — 37 modules
The authoritative list ([VCV Library](https://library.vcvrack.com/Fundamental)):

- **Generators:** VCO, Wavetable VCO, LFO, Wavetable LFO, Noise (multicolored), Random (random CV gen), Random Values (7 fixed random voltages)
- **Processors:** VCF, VCA, Delay, ADSR
- **Mixing/routing:** Mix (6-ch), VCA Mix (4-ch with VCAs), CV Mix (3-ch with attenuverters), Fade (crossfader), Mid/Side, Mult (1→8), Mutes (10x), Sequential Switch 1→4 and 4→1
- **Pitch:** Octave (1V/oct shifter), Quantizer (12-note scale)
- **Utilities:** 8vert (8x attenuverter / constant-voltage source), Rescale (gain/offset/limit), Sum, Logic (gate logic), Compare (comparator + limiter), Gates (gate/trigger manipulator), Process (slew + S&H + shift register), Sample & Hold ASR (8-ch S&H/shift register), Push (manual gate button), Pulses (10x trigger/gate)
- **Polyphony plumbing:** Split, Merge, Sum, Viz
- **Sequencing/visual:** SEQ-3 (3×8-step), Scope

**Belt commentary:** He has never published an explicit "why this set" essay. His stated philosophy is UNIX-like — "stable, minimal modules working together are preferred to a monolithic platform" ([VCV manual](https://vcvrack.com/manual/About)), and "keep getting lower and lower level into the fundamentals" ([Synthtopia interview](https://www.synthtopia.com/content/2018/01/22/open-source-synthesis-behind-the-scenes-with-vcv-rack-creator-andrew-belt/)). Telling evolution: the July 2022 additions were pure utilities — Compare, Gates, Logic, Process — which the community explicitly welcomed as "basic functionalities that were missing" ([announcement thread](https://community.vcvrack.com/t/vcv-compare-gates-logic-and-process/17828)). Fundamental converged toward *more* logic/comparator/slew/S&H utilities over time, not more exotic generators. The community "[Fundamental Constructs](https://community.vcvrack.com/t/vcv-fundamental-constructs/15895)" thread demonstrates the set is deliberately Turing-complete-ish for patching: users build ring mods, complex LFOs, and Krell patches from Fundamental alone.

**Polyphony:** Rack's signature design — any cable carries 1–16 channels; poly modules use SIMD (~4x cheaper than duplicated mono modules); mono inputs silently take channel 1. Split/Merge/Sum/Viz exist *solely* to manage this ([Polyphony manual](https://vcvrack.com/manual/Polyphony)).

### Cardinal (FOSS VCV fork)
Cardinal doesn't curate a small core — it bundles ~1,382 modules from 84 collections, all built-in, no external loading ([GitHub](https://github.com/DISTRHO/Cardinal)). What it reveals about "essential beyond Fundamental" is which collections it chose: **Audible Instruments** (Mutable Instruments ports), **Bogaudio** (the deepest utility/mixer/analyzer set), **MindMeld** (real mixing consoles), **Impromptu** (Clocked — a proper master clock, which Fundamental lacks), **stoermelder** (param mapping/meta-utilities), AS, JW, ML. Two design decisions matter: all Rack Core modules were replaced with plugin-host equivalents (audio/MIDI via host, host time/params), and **every included module supports polyphony, "reducing confusion for new users not yet used to mono vs poly"** ([Cardinal docs](https://cardinal.kx.studio/)).

### Pure Data vanilla — the tilde vocabulary
Canonical signal objects ([plugdata vanilla object list](https://plugdata-team-plugdata.mintlify.app/objects/vanilla-objects)):

- **Sources:** `osc~` (cosine), `phasor~` (0–1 ramp — the master phase primitive), `noise~`, `cos~` (waveshaper for building everything else)
- **Tables:** `tabread4~`, `tabosc4~`, `tabwrite~`, `tabplay~` — wavetable + sampler as one concept
- **Filters:** `lop~`, `hip~`, `bp~`, `vcf~` (signal-rate resonant), plus raw `rpole~`/`rzero~` primitives, `biquad~`
- **Delays:** `delwrite~`/`delread~`/`vd~`
- **Envelopes/control:** `line~`, `vline~`, `env~` (follower), `snapshot~`, `samphold~` (sample & hold), `threshold~` (comparator)
- **Math:** `+~ -~ *~ /~`, `clip~`, `min~`/`max~`, `abs~`, `wrap~`, `sig~` (constant), `sqrt~`, `pow~`
- **Routing:** `send~/receive~`, `throw~/catch~` (summing bus), `switch~`, `block~`

Note the deliberate minimalism: **there is no "ring mod module" — it's `*~`; no attenuverter — it's `*~` with a negative constant; no crossfader — it's two `*~` and a `+~`.** Famous gaps users complain about: no built-in reverb worth using (freeverb~ is an external; the pd-list and [ModWiggler threads](https://www.modwiggler.com/forum/viewtopic.php?t=183923) recur on this), no polyphony primitive (you build voice allocation with `[clone]`/`[poly]`), and historically the vanilla/extended split confused beginners.

### Reaktor Blocks Base / Primes
**Blocks Base** (free): 24 blocks — the 11 "Bento Box" modules (**OSC, SVF filter, VCA, ADSR, LFO, Mix, XFade (crossfader), CV Processor (offset/attenuvert/scale), S&H / 4 Mods, 8-step Sequencer, Scope**) plus Util blocks for clocking, note/gate I/O, CV mixing and triggers ([gearnews](https://www.gearnews.com/reaktor-6-3-update-blocks-front-patching-and-free-blocks-base-modular-rig/), [CDM](https://cdm.link/2019/04/reaktor-modular-patching-free/)). This is the tightest professional "starter core" of all surveyed — note it still includes a dedicated crossfader, CV processor, and S&H. **Blocks Primes** (paid, 23 blocks) adds character: Monark (Minimoog) filter/osc, Driver, Rounds, the Kodiak family, and four explicitly **Buchla-inspired West Coast blocks** ([NI](https://www.native-instruments.com/en/products/komplete/synths/blocks-primes/)). Polyphony: Blocks racks are effectively monophonic — a long-standing user complaint; real polyphony means leaving the Blocks paradigm for core Reaktor.

### Bitwig Grid
~231 modules in strictly enforced categories ([Bitwig userguide](https://www.bitwig.com/userguide/latest/grid_modules/)): I/O, Display (scopes/meters), **Phase (18 modules — the signature category: phase ramps as the universal driver of oscillators, LFOs, sequencing, waveshaping)**, Data, Oscillator, Random, LFO, Envelope (9, incl. Follower; envelopes have a built-in "normalled VCA"), Filter (14), Shaper (15), Delay/FX, Mix (18, incl. crossfade/blend), Level (22 — value, attenuate, bias, average), Pitch (17), **Math (22 — add, subtract, multiply, MinMax, Round, quantize)**, **Logic (22 — comparators, XOR, clock divide)**. Design conventions worth stealing: everything defaults to sensible pre-cabled behavior ("pre-cords"), bipolar ±1 signals everywhere, every input has an inline attenuverter, and **polyphony is per-voice: the whole patch is instantiated per note (Poly Grid), so no module ever needs poly-awareness** ([Polarity's Grid course](https://polarity.me/bitwig-grid-course/007-grid-modules/)). Complaints center on voice-management edge cases and sequencing polyphonically from within one patch ([KVR](https://www.kvraudio.com/forum/viewtopic.php?t=605116)).

### SuperCollider — tutorial-idiom UGens
From the official [Tour of UGens](https://doc.sccode.org/Guides/Tour_of_UGens.html) and standard tutorials: **SinOsc, Saw, Pulse, Blip, LFTri/LFSaw, Impulse; WhiteNoise/PinkNoise, LFNoise0/1/2, Dust; LPF/HPF/BPF, RLPF, Resonz/Ringz, OnePole; EnvGen (+Env), Decay/Decay2, Line/XLine; Lag (slew), Latch (S&H), Trig, Gate, PulseCount, PulseDivider, TDelay; DelayL/C, CombL/C, AllpassL/C, FreeVerb; Pan2, XFade2, Balance2; Shaper; PlayBuf; Mix; arithmetic operators as first-class signal ops (`*` is ring mod).** SC's polyphony model: multichannel expansion plus one-Synth-per-voice allocation — closest cousin to Bitwig's per-voice model.

### Puckette, *Theory and Technique of Electronic Music*
Chapter structure defines the pedagogically "complete" primitive set ([book](https://msp.ucsd.edu/~msp/techniques/v0.07/book.pdf)): (1) sinusoids, amplitude, multiplication as VCA; (2) **wavetables and samplers as one unified technique** (phasor → table lookup); (3) audio vs. control computation — line segments/envelope generators, **sample-and-hold**; (4) voice management/ADSR/muting; (5) **modulation: ring mod, AM, waveshaping, FM**; (6) designer spectra (pulse trains, formants); (7) time shifts and delays (variable delay, **comb filters**, pitch shift); (8) filter taxonomy (one-pole/one-zero up to resonant LP); (9) Fourier; (10) classical band-limited waveforms. The implicit claim: **phasor + table lookup + line~ + multiply + delay line + elementary filters is a complete basis** — everything else (west-coast included: waveshaping/wavefolding is ch. 5, LPG is a VCA+filter) is composed from these.

## 2. Categories every system agrees on

| Category | VCV | Cardinal | Pd | Blocks Base | Grid | SC | Puckette |
|---|---|---|---|---|---|---|---|
| Oscillator (audio, multi-wave) | yes | yes | yes | yes | yes | yes | yes |
| Noise | yes | yes | yes | (in osc) | yes | yes | yes |
| Resonant filter (LP at minimum) | yes | yes | yes | yes (SVF) | yes | yes | yes |
| VCA / multiply | yes | yes | `*~` | yes | yes | `*` | yes |
| ADSR / segment envelope | yes | yes | line~/vline~ | yes | yes | EnvGen | yes |
| LFO | yes | yes | osc~ | yes | yes | LF* | yes |
| Mixer + CV mixer | yes | yes | +~ | yes | yes | Mix | yes |
| Delay line | yes | yes | yes | (FX) | yes | yes | yes |
| Sample & hold | yes | yes | yes | yes | yes | Latch | yes |
| Slew / lag | yes (Process) | yes | lop~ | (CV Proc) | yes | Lag | implied |
| Attenuvert / offset / constant | 8vert, Rescale | yes | *~, sig~ | CV Processor | Level cat. | ops | yes |
| Comparator / logic | Compare, Logic | yes | threshold~ | (util) | Logic cat. | ops, Trig | ch. 3 |
| Scope / visualizer | yes | yes | arrays | yes | Display cat. | plotters | — |
| Basic step sequencer | SEQ-3 | many | metro+counter | 8-step | Data/Pitch | Patterns | ch. 4 |
| Quantizer | yes | yes | (build) | util | Pitch cat. | (build) | — |
| Crossfader | Fade | yes | (build) | **XFade** | Mix cat. | XFade2 | — |

The consensus core is unambiguous: **VCO, noise, resonant VCF, VCA, ADSR, LFO, mixer, delay** — plus a utility tier that every mature system independently converged on.

## 3. The utilities that matter most (the ones novices forget)

Ranked by how universally they appear and how loudly their absence is felt:

1. **Attenuverter + offset + constant voltage** — appears in *every* system. This is the single most-patched utility class in modular. VCV needed both 8vert *and* Rescale; Grid gives every input an inline one.
2. **Sample & hold (+ track & hold + shift register)** — in every system; VCV eventually shipped *two*. Shift registers keep reappearing — worth bundling.
3. **Slew limiter / lag** — portamento, envelope smoothing, gate-to-envelope. SC calls Lag one of its most useful UGens; VCV had to add it in 2022.
4. **Comparator + gate logic (AND/OR/XOR, clock divide)** — Grid has a 22-module Logic category; VCV's 2022 utility drop was mostly this.
5. **Crossfader** — notable: tiny Blocks Base *still* spent a slot on XFade; VCV has Fade; SC XFade2. Signal-blending is more idiomatic than novices expect.
6. **Ring mod / multiply, sum/difference, min/max, rectifier (abs), wrap/fold** — the systems that expose raw math (Pd, SC, Grid) never need dedicated ring-mod or rectifier modules. A general-purpose **signal math module** covers six "missing modules" at once.
7. **Sequential/routing switch** — VCV ships both directions (1→4, 4→1); essential for making small sequencers do big things.
8. **Mult, mutes, manual gate button (Push)** — trivial but constantly used; Push-style manual triggers are a beloved late addition to Fundamental.
9. **Scope/visualizer** — every system ships one in the core; it's a debugging tool, and debugging is most of learning modular.

## 4. Famous gaps (what users complain about)

- **VCV Fundamental:** no master **clock/clock divider** (everyone installs Impromptu Clocked), no reverb (free users install Plateau), quantizer and logic arrived years late, SEQ-3 is weak, no waveshaper/wavefolder (no west-coast path at all), Delay was long mono-only.
- **Cardinal:** the opposite complaint — 1,382 modules is overwhelming; no curation.
- **Pd vanilla:** no usable reverb, no polyphony primitive, no band-limited classic waveforms out of the box (pedagogically intentional).
- **Blocks:** effectively monophonic racks; small free set means paid Primes for any character.
- **Grid:** voice-management edge cases; a *dense* palette navigable only because categories are strict and defaults are excellent.
- **SC:** discoverability of 250+ UGens is the classic beginner wall.

## 5. Polyphony — three distinct models

1. **Poly cables (VCV/Cardinal):** cables carry up to 16 channels; modules opt in; SIMD makes poly ~4x cheaper. Cost: mono/poly confusion is real — Cardinal's response was to make *every* module poly, and VCV needed four dedicated modules just to manage it.
2. **Per-voice patch instancing (Bitwig Poly Grid, SC Synth-per-voice):** the whole patch runs once per note; no module ever knows about polyphony. Conceptually cleanest; costs CPU and makes cross-voice interaction (unison detune spread, paraphony) need special "voice stack" modulators.
3. **None (Pd vanilla, Blocks):** build it yourself with `[clone]` — the most frequent complaint about both.

## 6. Lessons for curating a new ~25-module core

1. **The consensus 10 are non-negotiable, so spend your thought on the other 15.** VCO(+noise), VCF, VCA, ADSR, LFO, mixer, delay, S&H, scope, sequencer appear in every system. Blocks Base is proof a credible core fits in ~12 "meat" modules + utilities.
2. **Combine utilities aggressively — the Swiss-army pattern wins.** VCV's best-loved late additions bundle functions (Process = slew + S&H + T&H + shift register; 8vert = attenuverter + constant; Compare = comparator + limiter; VCA Mix = mixer + VCAs). One "CV Processor", one "Math" (add/sub/mult/min/max/abs/fold/wrap — this *is* your ring mod and rectifier), one "Logic", one "Process", one crossfader = the entire section-3 list in five slots.
3. **Ship a clock and a quantizer on day one.** The two most-documented gaps in Fundamental's history. A core set that can't make a tuned, synced patch without third-party modules fails its first-session test.
4. **Decide the polyphony model before designing any module.** The deepest architectural fork in the survey; the two mature answers impose opposite requirements on the core palette.
5. **Include one west-coast primitive and one "character" voice.** Pure east-coast cores draw complaints; Blocks monetized the gap; Puckette treats waveshaping as core theory. A wavefolder plus an LPG-ish VCA mode costs 1–2 slots and doubles the timbral territory. Grid and Bento Box also show excellent defaults matter as much as module choice — the best "26th module" is convention.

**Sources:** [VCV Library – Fundamental](https://library.vcvrack.com/Fundamental) · [VCV Polyphony manual](https://vcvrack.com/manual/Polyphony) · [VCV About/manual](https://vcvrack.com/manual/About) · [Compare/Gates/Logic/Process announcement](https://community.vcvrack.com/t/vcv-compare-gates-logic-and-process/17828) · [Fundamental Constructs thread](https://community.vcvrack.com/t/vcv-fundamental-constructs/15895) · [Synthtopia Belt interview](https://www.synthtopia.com/content/2018/01/22/open-source-synthesis-behind-the-scenes-with-vcv-rack-creator-andrew-belt/) · [Cardinal GitHub](https://github.com/DISTRHO/Cardinal) · [plugdata vanilla objects](https://plugdata-team-plugdata.mintlify.app/objects/vanilla-objects) · [NI Blocks](https://www.native-instruments.com/en/products/komplete/synths/reaktor-6/blocks/) · [Blocks Primes](https://www.native-instruments.com/en/products/komplete/synths/blocks-primes/) · [Bitwig Grid userguide](https://www.bitwig.com/userguide/latest/grid_modules/) · [Polarity Grid course](https://polarity.me/bitwig-grid-course/007-grid-modules/) · [KVR Grid discussion](https://www.kvraudio.com/forum/viewtopic.php?t=605116) · [SC Tour of UGens](https://doc.sccode.org/Guides/Tour_of_UGens.html) · [Puckette, Theory and Technique of Electronic Music](https://msp.ucsd.edu/~msp/techniques/v0.07/book.pdf)
