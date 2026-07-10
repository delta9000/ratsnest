# Eurorack Module I/O & Parameter Semantics — Research for Patch-Language Design

*Research agent report, 2026-07-10. Preserved verbatim for module design in Phases 1c/2/3.*

Legend for signal types: **A** = audio, **CV** = control voltage (continuous), **G** = gate, **T** = trigger, **C** = clock, **1V/oct** = pitch CV. "List-shaped state" flags anything that is a sequence/scale/pattern rather than a scalar.

---

## 1. Make Noise Maths (2013 revision)

Four channels sharing an output bus. Channels 1 & 4 are full function generators; channels 2 & 3 are just DC-coupled attenuverters with offset normalling.

**Channel 1 and Channel 4 (identical, mirrored):**

| Jack/Control | Type | Notes |
|---|---|---|
| Signal IN | CV/A in, ±10V | Slew-limits/follows the input (lag, portamento, ASR envelope from a gate) |
| Trigger IN | T/G in | Fires a one-shot 0→10V→0V function regardless of Signal IN |
| Rise CV IN | CV in | Modulates rise time |
| Fall CV IN | CV in | Modulates fall time |
| Both CV IN | CV in | Modulates rise and fall simultaneously |
| Rise knob, Fall knob | param | Time range huge: ~ms up to ~25 minutes |
| Vari-Response knob | param | Continuous curve morph: logarithmic ↔ linear ↔ exponential |
| Cycle button | param (bool) | Self-retrigger at end of fall → loops as LFO. No CV input for cycle; remote cycling is done by patching EOC/EOR back to Trigger IN |
| Unity OUT | CV out, 0–10V | Pre-attenuverter, full-level copy; **not** on the buses |
| EOR OUT (ch1 only) | G out | End of Rise: high once rise completes (i.e., during fall) |
| EOC OUT (ch4 only) | G out | End of Cycle: high when the fall stage completes / function idle |
| Variable OUT (1 & 4) | CV out | Post-attenuverter version of the function |

**Channels 2 & 3:** Signal IN (±10V) + attenuverter knob + Variable OUT. **Normalling:** Signal IN is normalled to +10V, so with nothing patched the attenuverter is a manual offset generator (−10V…+10V).

**Bus outputs:** SUM (sum of all four attenuverted channel signals), INV (inverted SUM), OR (analog OR / maximum of the bus signals).

**Critical normalling semantics:** each channel's attenuverted output is normalled onto the SUM/INV/OR buses; **inserting a cable into a channel's Variable OUT removes that channel from the buses**. That's why the manual tells you to use the Unity outs (ch 1/4) if you want a channel both on the bus and patched out, and to zero unused ch 2/3 knobs or insert dummy cables to keep the +10V normal off the bus. A patch language must model "output jack patched → bus contribution broken" and "input unpatched → constant normal source."

List-shaped state: none. All state is scalar knobs + one boolean (cycle) per end channel.

## 2. Mutable Instruments Plaits

Macro-oscillator: one engine ("model") active at a time.

**Inputs:** MODEL (CV), TIMBRE (CV, with attenuverter), FM (CV, attenuverter), MORPH (CV, attenuverter), HARMONICS (CV, **no** attenuverter), V/OCT (1V/oct, −3..+7 oct), TRIG (T/G), LEVEL (CV, opens internal LPG / accent for percussive models).
**Outputs:** OUT (A, main), AUX (A, a variant/by-product of the main signal — e.g., different waveform, sub-signal, or raw exciter depending on model).

**Model selection:** two buttons step through banks of 8 models; original firmware = 16 models (bank 1 pitched: VA, waveshaper, 2-op FM, granular formant, harmonic/additive, wavetable, chord, speech; bank 2 noisy/percussive: swarm, filtered noise, particle, inharmonic string, modal, bass drum, snare, hi-hat). Firmware 1.2 adds a third bank of 8 (VA with filter, phase distortion, 3× six-op FM, wave terrain, string machine, chiptune) → 24 total. The MODEL CV input offsets the selection around the panel-selected model (an *enumerated*, list-shaped parameter, not a smooth scalar). **When TRIG is patched, MODEL CV is sampled only on triggers.**

**Normalling (the key behavioral quirk set):**
- Nothing in TRIG or LEVEL → LPG bypassed, oscillator drones at full level.
- TRIG patched, LEVEL unpatched → each trigger fires an **internal decay envelope** that strikes the LPG. LPG character (VCFA↔VCA) and decay time are hidden parameters (button + TIMBRE / MORPH knobs).
- Additionally: **if TRIG is patched and a CV input (TIMBRE/FM/MORPH) is unpatched, that input's attenuverter routes the internal envelope to the parameter** instead. Unplugging a CV cable silently re-purposes the attenuverter — a classic normalling trap.
- LEVEL patched → external CV drives LPG amplitude/brightness directly (and acts as accent for percussive models).

List-shaped state: the model list (enumeration); everything else scalar.

## 3. Mutable Instruments Marbles

Random sampler with three coupled generators: **t** (random gates), **X** (random voltages), **Y** (bonus slow random).

**Outputs (7):** t1, t2, t3 (G; t2 is the master clock, t1/t3 are derived random gate streams); X1, X2, X3 (CV, selectable range: 0–2V / 0–5V / ±5V per the range button); Y (CV, ±5V smooth random, by default clocked at 1/16 the rate of X2; its rate/spread/bias/steps editable via hidden button-hold combos).

**Inputs (9):** t CLOCK (C, replaces internal clock when patched); X CLOCK (C); CV inputs for RATE (with 1V/oct scaling), JITTER, t-BIAS, SPREAD, X-BIAS, STEPS, DEJA VU.

**Knobs/buttons:** RATE (±4× range switch), JITTER (stable → humanized → chaotic timing), t-BIAS (gate distribution t1 vs t3), t-MODE button (coin toss / ratio div-mult / drum-pattern alternation — an enumerated mode); SPREAD (voltage distribution width), X-BIAS, STEPS (smooth → stepped → progressively **quantized to a scale**), X-MODE button (all channels alike / X1,X3 opposite / X3-only variants); DEJA VU knob + two buttons routing déjà vu to t, X, both, or neither; LENGTH button (loop length 1–16).

**Déjà vu:** probability of replaying values from a recirculating history buffer. At 12 o'clock it's a locked loop; past 12 it re-shuffles loop order. The loop buffer is **list-shaped state** (up to 16 gates + 16 voltages).

**Scales:** the STEPS quantizer has 6 preset scales, and scales are **user-reprogrammable by feeding example pitch CV** (scale "learning") — list-shaped state.

**Normalling:** when X CLOCK is unpatched, each X output is sampled by its corresponding t stream (same "melody," notes held at different times); patching a common X clock syncs all three. **External processing mode** (button [O]): instead of internal random numbers, the X generator samples the voltage at the **SPREAD CV input** whenever it needs a value — so all looping/shuffling/quantizing/lag machinery applies to an external CV, and the SPREAD jack changes meaning entirely. The range LED turns off because output range now depends on the input.

## 4. Mutable Instruments Beads (with Clouds notes)

Granular processor; internal chain is always stereo.

**Inputs:** IN L, IN R (A; mono-normalled), SEED (G/T/C — audio-rate capable), FREEZE (G), DENSITY CV (±5V, 1V/oct exponential FM of grain rate at audio densities), TIME/PITCH/SIZE/SHAPE CV (one jack routed via four "attenurandomizers" — each small knob sets CV amount *or* random modulation amount when no CV is present: another unpatched-jack repurposing), assignable CV (button-routed to FEEDBACK, DRY/WET, or REVERB).
**Outputs:** OUT L, OUT R (A). **Normalling: if OUT R is unpatched, L carries the mono sum**; OUT R can optionally emit grain triggers (G) instead.

**Parameters:** TIME (playback position: newest ↔ oldest audio in buffer), PITCH (±24 semitones, V/oct tracked), SIZE (30ms–4s, forward/backward; full CW = delay mode), SHAPE (grain envelope), DENSITY (grain rate; in externally-clocked mode becomes divider/probability), FEEDBACK, DRY/WET, REVERB, quality button (buffer fidelity: "cold digital / sunny tape / scorched cassette" — enum trading buffer length vs. fidelity).

**Gate semantics:** FREEZE (latching button or gate) stops recording into the buffer (buffer = list-shaped state; auto-backed-up after 10s frozen). SEED spawns a grain per trigger, acts as external clock, or gates continuous seeding depending on latch mode.

**Clouds (predecessor) differences:** parameters POSITION, SIZE, PITCH, DENSITY, TEXTURE plus a BLEND knob multiplexing 4 values (dry/wet, stereo spread, feedback, reverb — hidden paging, a known ergonomic pain Beads fixed); FREEZE gate; TRIG input; mono-normalled stereo I/O; alternate modes (granular/pitch-shifter/looping delay/spectral).

## 5. ALM Pamela's PRO Workout (and NEW Workout)

Master-clock with 8 independently programmed outputs. **Everything is configured through one encoder (click/turn) with a per-output parameter menu page** — the module is essentially 8 records of parameters plus a global clock.

**I/O (PRO):** Clock IN (C, rising edge), Run IN (G), **4 assignable CV inputs (0–5V)**; 8 outputs (0–5V, 12-bit, ~3.8kHz update — used as clock, gate, LFO, envelope, or stepped CV). (NEW Workout: only 2 input jacks, each assignable to clock/run/CV-anything; outputs 0–5V.) Expanders add MIDI/DIN-sync and more CV ins.

**Per-output parameter record:**
- **Div/Mult** of base BPM: ÷16384 up to ×192 including non-integer factors.
- **Waveform/type** (enum): gate/square, triangle, sine, saw-ish ramps, AD envelopes, ratchets, random S&H, smooth random, etc.
- **Level, Offset, Phase** (phase wraps — shifting a >50%-width pulse >50% can double-trigger), **Width** (gates), **Slew**.
- **Euclidean block:** ESteps (pattern length), ETrigs (hits), ERot (rotation), and PRO adds EPad (empty padding steps appended). This is *generated* list-shaped state (a boolean rhythm pattern) controlled by 4 scalars.
- **Skip/probability** (chance a pulse is dropped), **Loop** (beat-based looping of random values — makes random outputs list-shaped), **logic/cross-output modulation** (combine with another output), **"Flex" micro-timing** (swing/humanize/bounce), and on PRO, **quantization of CV waveforms to scales** (scale = enum/list).
- Any parameter can be **assigned to a CV input** with offset+attenuation — CV routing is itself per-output state.

**Presets:** per-output and whole-module saves, 7 banks × 8 = 56 slots (list-shaped preset store). For a patch language: Pam's is the strongest argument for modules-as-arrays-of-parameter-records rather than knob-per-jack.

## 6. Intellijel Metropolix

Performance pitch sequencer, Metropolis/Ryk M-185 lineage: **8 stages, 2 tracks**, where a "stage" ≠ a clock step because each stage lasts *pulse count* clocks.

**I/O:** CLK in (C), RST in (T), 3× assignable AUX inputs (CV/G — user-routable to almost any parameter); TRK1 PITCH (1V/oct) + TRK1 GATE (G), TRK2 PITCH + GATE, and 2 assignable outputs A & B (CV or gate, fed by mod lanes or internal signals). 2 assignable CTRL performance knobs.

**Per-stage (the core list-shaped record, 8 entries):**
- Pitch (slider, quantized to the selected scale/root)
- Pulse count (1–8 clocks spent on the stage)
- Gate type (enum): REST (no gate), SINGLE (gate on first pulse only), MULTI (gate every pulse), HOLD (gate held across all pulses)
- Skip (bool), Slide (bool — portamento into the stage)
- Lane overrides per stage: GATE override, PITCH override, **RATCHet count** (sub-pulse retriggers), **PROBability**, **ACCUMulate** (transposition added each pass — stateful!), plus a CV lane.

**Per-track:** sequence LENgth, playback ORDER (enum: forward/reverse/pendulum/random…), clock DIVision, SWING, SLIDE time, GATE length, octave/transpose, note range. Both tracks *read the same 8 pitch sliders* but reinterpret them independently — a deliberate decoupling of data from playback heads.

**Mod lanes:** 8 additional lanes, each an 8-stage value sequence with its **own** length/order/division, routable to internal parameters or the A/B outputs. So Metropolix state = one shared pitch list + per-track playhead configs + 8 independent modulation lists. Scale/root selection is an enum over a built-in scale list.

## 7. Music Thing Turing Machine mk2 (+ expanders)

16-bit **shift register** random looping sequencer. Core state is literally a bit vector.

**Main module:** Clock IN (C); CV IN (CV, sums with the big Probability knob); Noise OUT (A, the internal analogue noise source); Pulse OUT (G, clocked pulses from the register); OUT (stepped CV, unipolar ~0–10V max, scaled by the Scale knob). Controls: **Probability** knob (chance the recirculating bit flips each clock — fully CW locks the loop, fully CCW double-length "mobius" inversion), **Length** switch (enum: 2,3,4,5,6,8,12,16), Scale (output attenuation), Write/Clear momentary switches (force 1s/0s into the register).

**Output derivation:** top 8 bits → DAC → stepped CV. The register bits are exposed on an expansion header:
- **Pulses expander:** 11 pulse-train outputs (G) — register bits gated with the clock, giving related rhythms.
- **Voltages expander:** 8 faders, one per bit; output = sum of faders whose bits are high (2 CV outs). A "random looping step sequencer" whose *sequence content* is the shared register.

List-shaped state: the 16-bit register itself (shared by main + all expanders); length is an enum. Probability is the single scalar that determines mutation rate — a nice model of "evolving list state."

## 8. Sequential switches & matrix mixers (routing modules)

**Doepfer A-151 Quad Sequential Switch:** Trig IN (T/C — advance), Reset IN (T — jump to step 1), one common O/I jack, four I/O jacks. **Bidirectional**: works 1-in→4-out or 4-in→1-out; ±8V signals, audio or CV. V2 adds a switch selecting the number of steps: 2, 3, or 4 (enum). Semantics: a clocked *connection pointer* — state is "which of N terminals is connected," advanced by trigger, not a voltage at all. (A-152 is the addressed/CV-selectable variant.)

**Matrix mixers (e.g., Doepfer A-138m 4×4):** 4 inputs, 4 outputs, 16 gain knobs. Each output = Σ(input_i × gain_{i,out}); jumpers select unipolar (attenuator) or bipolar (attenuverter) per row. Semantics for a language: a dense N×M matrix of scalar gains connecting every input to every output — routing *is* the parameter set, and any (in,out) pair may carry audio or CV. No normalling.

## 9. Quantizers (Ornament & Crime "Quantermain"; Disting)

**o_C Quantermain:** quad quantizer. Per channel: CV input (hardwired CV1–4), trigger input TR1–4 (clocked mode samples on trigger; otherwise continuous with ~100µs latency), output A–D (1V/oct). Per-channel parameters: **scale** (enum over a large built-in list — dozens of diatonic + microtonal scales — plus 4 user-editable scales), **mask** (a bitmask enabling/disabling individual scale degrees — list-shaped), **root**, **transpose** (scale degrees), **octave**, source select, and an "aux CV" routing so a second CV input can modulate scale#, root, mask, transpose, or octave (the *choice of scale itself* is CV-addressable — the scale list becomes an indexed array). Meta-Q variant sequences 4 slots of (scale, mask, root, transpose) per channel — a list of quantizer configs.

**Disting mk4 quantizer algorithms:** X = input CV, Y = transpose (when Z>0, chromatically quantized and added pre- or post- depending on parameter) or sample trigger (when Z<0), Z = scale select; outputs: quantized CV + trigger-on-note-change. Scales specifiable via built-in list or **Scala files from SD card** — scales as external list data.

Design takeaway: a quantizer's essential state is (scale = ordered pitch-class list, mask = bit list, root = scalar, transpose/octave = scalars), plus a mode flag for continuous vs. track-and-hold-on-trigger.

## 10. Instruo / DivKid øchd

Minimal by design: **one rate knob, one CV input with attenuverter, eight outputs, zero menus.**

- 8 all-analogue free-running **triangle LFO outputs, ±5V (10Vpp)**, arranged fastest (top) to slowest (bottom). Rates are **not** synced, divided, or phase-locked — each core is independent, with ranges hand-tuned (capacitor selection) to musical spreads; they drift organically.
- Rate knob scales **all eight** rates globally: from low audio rate (~6ms period) on the top output down to ~25-minute cycles at the bottom.
- CV input (+attenuverter): modulates global rate; **sufficiently negative CV halts the cores entirely, freezing/holding all outputs at their current values** — a gate turns it into 8-channel track-and-hold. Self-patching an output into the CV input warps waveshapes beyond triangles.
- (øchd expander derives 8 additional outputs from the same cores: inverted/full-wave/trigger variants.)

List-shaped state: none — but it's the canonical example of *one parameter fanning out to N correlated-but-independent outputs*, which a patch language should be able to express without pretending there are 8 modules.

---

## Cross-cutting observations for the patch language

1. **Normalling is semantic, not cosmetic.** Three distinct flavors appeared: (a) *input-unpatched → constant source* (Maths ch2/3 +10V); (b) *input-unpatched → internal signal takes over the same attenuverter* (Plaits TIMBRE/FM/MORPH ← internal envelope; Beads attenurandomizers ← random); (c) *output-patched → breaks an internal route* (Maths channel outs leaving the SUM/OR bus; Beads/Plaits-family R-output mono summing; Marbles X-clock cross-normalling). A language needs to represent "jack patched?" as a condition affecting internal routing.
2. **Enumerated parameters are everywhere**: Plaits model, Marbles t/X modes, Turing length, Pam's waveform, A-151 step count, gate types on Metropolix. These are index-into-list, sometimes CV-addressable (Plaits MODEL, o_C scale#).
3. **List-shaped state that isn't a jack**: Marbles déjà-vu buffer, Turing shift register, Metropolix stage/lane arrays, quantizer scales & masks, Pam's Euclidean patterns and preset banks, Beads/Clouds audio buffer. Patches aren't reproducible from knob positions alone.
4. **One physical control, multiple parameters**: hidden button-hold combos (Plaits LPG decay, Marbles Y config, Clouds blend paging) and menu-per-output modules (Pam's, Disting) mean a text language can be *more* expressive than the panel by naming hidden parameters directly.
5. **Jacks that change meaning by mode**: Marbles SPREAD input in external-processing mode, Pam's assignable inputs, Beads' OUT R (audio vs. triggers), A-151's bidirectional I/O jacks.

Sources: [Make Noise Maths manual (PDF)](https://www.makenoisemusic.com/wp-content/uploads/2024/03/MATHSmanual2013.pdf) · [Plaits manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/manual/) · [Marbles manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/marbles/manual/) · [Beads manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/beads/manual/) · [ALM Pamela's PRO Workout page](https://busycircuits.com/pages/alm034) and [manual (PDF)](https://assets.busycircuits.com/docs/alm034-manual.pdf) · [Intellijel Metropolix](https://intellijel.com/shop/eurorack/metropolix/) and [manual v1.4 (PDF)](https://intellijel.com/downloads/manuals/metropolix_manual_v1.4_2022.04.04.pdf) · [Music Thing Turing Machine](https://www.musicthing.co.uk/Turing-Machine/) · [Doepfer A-151](https://doepfer.de/a151.htm) · [o_C user manual](https://ornament-and-cri.me/user-manual-v1_3/) · [Disting mk4 manual (PDF)](https://www.expert-sleepers.co.uk/downloads/manuals/disting_user_manual_4.8.pdf) · [øchd manual (PDF)](https://www.instruomodular.com/wp-content/uploads/2020/05/Ochd-Manual-A5.pdf) · [DivKid øchd](https://divkidvideo.com/ochd-the-second-divkid-eurorack-module/)
