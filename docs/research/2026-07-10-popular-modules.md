# Most Popular Eurorack Modules, 2025–2026

*Research agent report, 2026-07-10. Preserved verbatim.*

**Methodology:** Primary source is ModularGrid's live popularity ranking (modules sorted by all-time inclusion in user racks, ~14,800 modules indexed), cross-checked against ModularGrid's top-rated list, [DivKid's Best of 2025](https://divkidvideo.com/best2025/), [MusicRadar's best-modules guide](https://www.musicradar.com/news/the-best-eurorack-modules-in-the-world), and ModWiggler "top module in each category (2025 edition)" threads. Panel variants (Maths black/white, Veils 2015/2020, etc.) are merged; pure infrastructure (power supplies, blank panels, buffered mults, line outs) is excluded. Order below follows ModularGrid ubiquity rank. Note that Mutable Instruments modules remain top-ranked despite the company winding down in 2022 — clones keep them ubiquitous.

---

### 1. Plaits — Mutable Instruments — VCO (macro-oscillator)
Digital oscillator with ~16 (24 with firmware 1.2) switchable synthesis models: virtual analog, wavefolding, FM, granular, speech, particle noise, drum models, chords. Inputs: V/oct, FM, trigger, level, and CV for Harmonics/Timbre/Morph plus model-select CV; outputs: OUT and AUX (a second, model-dependent rendering). **Structurally unusual:** behavior changes with patch state — with nothing in the trigger input it drones; patching the trigger enables an internal decay envelope and virtual low-pass gate on the output. Model switching redefines what every knob means; the percussive models make it a complete drum voice.

### 2. Maths — Make Noise — Function generator / analog computer
Two function generators (channels 1 & 4) with Rise/Fall time controls (each with CV), signal input (slew), trigger input (envelope), cycle input (loop = LFO/oscillator), log/lin/exp curve; channels 2 & 3 are attenuverters whose inputs are normalled to +10 V (so they double as DC offset sources). Outputs: unity outs for ch 1/4, per-channel attenuverted signals feeding SUM and OR (analog logic max) bus outputs, plus EOR (end-of-rise, ch1) and EOC (end-of-cycle, ch4) gate outputs. **Structurally unusual:** internal mix bus with output-normalling (patching a channel out removes it from SUM), gate outputs derived from function state enable self-patching feedback (the canonical "Maths self-patching" idiom); one module is simultaneously envelope, LFO, slew, mixer, logic, and comparator.

### 3. Pamela's NEW / PRO Workout — ALM Busy Circuits — Clock / modulation source
Master-clock "workout": 8 outputs, each independently a clock division/multiplication (÷512…×48) of the master BPM with a per-output waveshape (gate, sine, triangle, saw, envelope shapes, random, Euclidean gate patterns, arbitrary patterns on the Pro). Per-output parameters: level, offset, phase, width, delay, skip-probability, loop length, quantizer scale (for random CV). Inputs: run/clock, 2 assignable CV inputs; screen + single encoder/button UI; expanders add MIDI/DIN-sync and extra CV inputs (Axon). **Structurally unusual:** massive per-channel parameter arrays behind a one-encoder menu; any parameter can be CV-assigned; outputs span audio-rate to multi-bar timing from one clock domain.

### 4. Disting mk4 / Disting EX — Expert Sleepers — Multifunction (utility/effects/everything)
Algorithm platform: mk4 has 2 inputs (X, Y), 2 outputs (A, B), a Z knob/CV, and 80+ selectable algorithms (quantizer, LFO, delay, sample player, pitch tracker, ES-5 expander, etc.). EX is the larger successor (6 in / 4 out, dual-mk4 mode plus "single" heavyweight algorithms: multisample playback, looper, granular). **Structurally unusual:** jack semantics are fully polymorphic — whether X is audio, CV, or a trigger depends on the loaded algorithm; deep menu-dive with microSD storage. A stress-test case for any patch-language type system.

### 5. Morphagene — Make Noise — Sampler (tape/microsound)
Stereo sampler treating recordings as a "reel" cut into up to 99 splices, each granulated into "genes." Controls/CV: Vari-Speed (bipolar: reverse through stop to forward, pitch-coupled), Gene Size, Slide (position), Morph (grain overlap into clouds), Organize (splice select), S.O.S. (sound-on-sound dry/overdub blend). Inputs: stereo audio, REC gate, PLAY trigger, CV per parameter; outputs: stereo audio + EOSG trigger (fires at end of splice/gene). **Structurally unusual:** persistent internal state (the reel + splice-marker array, saved to SD), record head and playback engine run simultaneously, trigger output derived from playback position enables self-clocking patches.

### 6. Quad VCA — Intellijel — VCA / mixer
Four VCAs, each with input, output, CV input with attenuator, level slider, exponential/linear response switch, and boost gain option. **Structurally unusual:** two normalling chains — each channel's output is normalled into the next channel's input (unpatched outputs cascade into a submix, so it doubles as a 4:1 mixer), and CV can carry down the chain. Patching a jack breaks the corresponding normal.

### 7. Veils (2020) — Mutable Instruments — VCA / mixer
Same archetype as Quad VCA: four channels with input, gain knob, CV input, continuously variable exponential↔linear response knob, and output; outputs normalled into a summing bus so unpatched channels mix into the last patched output. Ubiquitous via clones.

### 8. Mimeophon — Make Noise — Effects (stereo delay/repeater)
Stereo "repeater": inputs L/R (L normalled to R), outputs L/R. Controls/CV: Rate (delay time), 8 switched "Zones" (microsound/comb through multi-second delay ranges), Repeats (feedback), Color (feedback-path filter/tape character), Halo (blur/reverb-like diffusion), Skew (offsets L vs R time), Flip (reverse playback), Hold (infinite loop/freeze gate), plus clock input for tempo-synced time. **Structurally unusual:** zone switching quantizes the time-range; internal cross-channel feedback network; Hold turns a delay into a looper.

### 9. Rings — Mutable Instruments — Resonator (physical modeling)
Modal/string resonator excited by any signal. Inputs: audio IN, V/oct, STRUM trigger, and CV (with attenuverters) for Frequency, Structure, Brightness, Damping, Position. Outputs: ODD and EVEN (partials split across two jacks — stereo-ish pair, mono-summed when only one is patched). Three models (modal, sympathetic strings, inharmonic string) and 1/2/4-voice polyphony selected by buttons. **Structurally unusual:** famous normalization logic — with nothing in IN, an internal exciter is used; with nothing in STRUM, note changes at V/oct auto-strum; polyphony mode round-robins voices across the two outputs.

### 10. Clouds / Beads — Mutable Instruments — Granular processor
Real-time granular texture engine over a rolling audio buffer. Clouds: stereo in/out, Freeze gate, Trig input (spawn a grain), Position/Size/Pitch/Density/Texture controls + CV, Blend knob multiplexing dry-wet/stereo-spread/feedback/reverb (mode button). Beads (2021 successor): analog input gain stage, stereo out, dedicated knobs for the blend functions, Seed gate, time-stretching "tape" modes. **Structurally unusual:** buffer freeze as gate-controlled state; Clouds' one-knob-four-parameters multiplexing; grain spawning driven by either internal density clock or external triggers.

### 11. Marbles — Mutable Instruments — Random / stochastic sequencer
Two linked random generators. "t" side: clock in or internal rate, Bias and Jitter controls, three gate outputs (t1/t2/t3 — related random rhythmic streams). "X" side: three stepped random CV outputs plus smooth Y output, with Spread (distribution width), Bias, and Steps (continuous → quantized to scales) controls, separate X clock input. **Structurally unusual:** the Déjà Vu control records both gate and CV streams into a loop buffer (adjustable length) and continuously blends between locked repetition and fresh randomness — array-like sequence state emerging from a random source; external CV can be sampled in place of the internal RNG.

### 12. Ochd — DivKid / Instruō — LFO bank
Eight free-running analog triangle LFOs in 4 HP, ordered fastest (top) to slowest (bottom, minutes-long), all scaled by a single Rate knob and one rate CV input; eight outputs, no other controls. **Structurally unusual:** channels drift in phase and subtly interact (organic non-repetition is the point); no sync/reset. An expander adds rectified/inverted copies and gate versions.

### 13. Batumi — Xaoc Devices — Quad LFO
Four digital LFOs; per channel: frequency fader, frequency CV input, reset/sync input, a sine output, and an "assign" output selectable between saw/square/trapezoid. Global mode button switches Free / Quad (fixed 90° phase offsets) / Phase (ch1 master, others phase-shifted copies) / Divide (others are clock divisions of ch1). **Structurally unusual:** mode switching redefines channels 2–4 from independent oscillators into functions of channel 1; Poti expander exposes waveform-assign and switch options.

### 14. Zadar — Xaoc Devices — Quad envelope / function generator
Four channels (A–D), each with trigger input, assignable CV input, and output; a screen + encoder selects from ~200 preset envelope shapes per channel, which can be warped, repeated, delayed, and stretched from microseconds to ~30 minutes. **Structurally unusual:** envelopes are chosen from a shape library rather than built from stages; heavy display/menu interaction; Nin expander adds manual fire buttons and per-channel outputs.

### 15. Quadrax (+ Qx) — Intellijel — Quad function generator
Four channels, each: trigger input, Rise/Fall sliders, shape control, and mode selection (AD, AHR, looping/LFO, burst). Four CV inputs are assignable to any channel parameter via a built-in CV matrix. Qx expander adds per-channel EOR/EOC trigger outputs, which can be internally linked so channels chain/trigger each other. **Structurally unusual:** internal routing matrix (CV-to-parameter and channel-to-channel linking) configured by button combos rather than patch cables.

### 16. Stages — Mutable Instruments — Segment generator (envelope/LFO/sequencer constructor)
Six segments, each with a slider, pot, CV input, gate input, and output; each segment is typed (ramp / hold / step) via button. **Structurally unusual:** topology is defined by patching — contiguous segments group into a single multi-stage envelope starting where a gate is patched; an ungated looping segment becomes an LFO; single step segments act as S&H or slew. Multiple Stages chain over a rear connector into one long segment array. Effectively a user-programmable state machine — highly relevant to patch-language semantics.

### 17. QPAS — Make Noise — Filter (stereo multimode)
"Quad-core" stereo analog filter: stereo inputs (L normalled to R) and four simultaneous stereo output pairs (LP, BP, HP, and "Smile Pass"). Controls: cutoff, resonance, and Radiate L/R (spreads the four filter cores' frequencies for stereo movement); CV: two FM inputs, Q CV, Radiate CVs, plus two "!‽" ping inputs that strike/excite the cores. **Structurally unusual:** one filter, eight outputs, all response types available at once in stereo.

### 18. Ripples (2020) — Mutable Instruments — Filter
4-pole analog "liquid" filter: audio in, cutoff and resonance knobs, two frequency CV inputs (one attenuated FM), and simultaneous BP2 / LP2 / LP4 outputs. **Structurally unusual:** the LP4 path includes a built-in VCA with its own CV input (filter + VCA voice in one), and resonance is gain-compensated.

### 19. Three Sisters — Mannequins — Filter (triple)
Three linked filters (LOW / CENTRE / HIGH bands), each with its own input and output, plus an ALL input (normalled to all three) and ALL (mixed) output. Shared controls: Freq, Span (spacing of the three cutoffs), Quality (resonance), an FM input, and a Formant/Crossover mode switch that changes how the bands relate. **Structurally unusual:** three filters sharing one control surface; input normalling makes it either one 3-band device or three independent filters.

### 20. Optomix — Make Noise — Low pass gate / mixer
Two vactrol-based low pass gate channels; each has signal in, Control CV in (opens amplitude and brightness together), Strike input (a trigger "pings" the vactrol for natural percussive decay), Damp control, and an output; channel outputs are normalled into a summed mix output with an auxiliary input for chaining. **Structurally unusual:** the Strike input means a trigger alone produces a full percussive event from sustained audio; combined VCA+filter behavior in one gain element.

### 21. Richter Wogglebug — Make Noise — Random / chaos
Buchla-lineage random voltage generator. Inputs: rate CV, external clock, "Disturb"/influence input. Outputs: Stepped, Smooth, and Woggle (bouncy, ringing) random CVs, a clock/burst gate output, plus chaotic audio outputs from its internal ring-modulated oscillator pair. **Structurally unusual:** internal cross-coupled oscillators and S&H feed each other — internal feedback and multiple correlated random streams from one clock.

### 22. Turing Machine Mk II — Music Thing Modular — Random looping sequencer
Open-source 16-bit shift register sequencer: clock input, main CV output (DAC across the register bits), noise-driven bit source. The big knob sets bit-flip probability: fully clockwise locks the loop, noon is fully random, counter-clockwise locks a double-length loop; a switch sets loop length (2–16). **Structurally unusual:** its "sequence" is emergent array state in a shift register, never directly editable; rear bus headers feed expanders (Pulses: 7 gate pattern outputs; Volts: additional weighted CV). Ubiquitous in DIY form (undercounted by rack stats).

### 23. Metropolix — Intellijel — Sequencer (pitch/gate)
Performance sequencer: 8 stages with per-stage pitch slider, pulse-count and gate-mode switches; two tracks independently traverse the same 8 sliders with their own length, direction, clock division, and octave; plus 8-step mod lanes for CV, per-stage ratcheting, probability, accumulation/transpose, swing, and scale quantization. I/O: clock/reset in, 2 assignable CV ins; per-track pitch CV + gate outs and assignable aux CV outs. **Structurally unusual:** multiple playheads over one shared stage array; large saved-state (presets) plus menu layer under a performance surface.

### 24. René mk2 — Make Noise — Sequencer (Cartesian)
"3D" Cartesian sequencer: a 4×4 touch-plate grid of knob values traversed by X, Y, and Z channels, each with its own clock and CV/mod inputs; outputs are three CV + three gate streams derived from the same grid. Snake, Cartesian, and programmable access patterns; multiple storable states; Select-Bus compatible for preset changes from other Make Noise modules. **Structurally unusual:** one 2D data array read concurrently by three independently-clocked traversal functions.

### 25. Pressure Points (+ Brains) — Make Noise — Touch controller / sequencer
Four touch plates; each plate, when touched, emits its column's three tuned CVs (rows A/B/C) plus a gate and a pressure CV. **Structurally unusual:** it is a manually-scanned sequencer — the human finger is the clock; adding the Brains expander turns it into a clocked 4/8-step analog sequencer, and units chain to 8 steps. Purely analog array state (12 knobs = 3×4 matrix).

### 26. Tempi — Make Noise — Clock generator/divider
Six-channel polyphonic clock machine: external tempo input or internal clock; each of 6 outputs gets a division/multiplication programmed by per-channel touch buttons, with mute, phase shift/rotate, and hump ("humanize") functions; 64 storable states, Select-Bus preset recall. **Structurally unusual:** programming by touch gestures rather than knobs; whole clock configurations are savable/recallable state.

### 27. Basimilus Iteritas Alter — Noise Engineering — Drum voice
Parameterized digital drum synthesizer: trigger input, pitch knob + V/oct, and ~6 timbre/envelope knobs (spread, fold, harm, morph, attack, decay) each with a CV input; two 3-position switches select waveform bank and drum archetype (bass/snare/hi-hat harmonic layouts); single audio output. **Structurally unusual:** entire percussion voice (oscillators + envelope + noise) behind one trigger in / one audio out; mode switches re-map the harmonic engine.

### 28. Arbhar — Instruō — Granular processor
Granular engine with six recording-buffer layers: stereo (or mono) audio in, onboard condenser microphone, and automatic capture via onset detection ("follow" mode); controls/CV for grain size, pitch (with quantized intervals), intensity/density, scan position, layer select, spray; stereo outputs; included 2 HP expander breaks out extra CV/gate I/O. **Structurally unusual:** multi-buffer state with live capture triggered by audio analysis, not just gates.

### 29. Magneto — Strymon — Effects (stereo tape delay/looper)
Four-playhead stereo tape-delay emulation: stereo in/out, per-head level/pan/feedback routing, tape speed/pitch with V/oct tracking (playable as a resonant instrument), spring reverb, phrase-looper and sound-on-sound modes, clock in/out for sync, and CV inputs over major parameters. **Structurally unusual:** mode switching between delay/looper/sampler personalities; internal multi-tap feedback matrix; one of the few modules with both clock sync and pitch tracking of the delay line.

### 30. Data Bender — Qu-Bit Electronix — Effects (buffer glitch)
"Circuit-bent digital audio buffer": continuously records stereo input into a rolling buffer; controls/CV/gates over buffer time/window, repeats, and three corruption families — Bend (tape-style pitch/speed), Break (CD-skip repeats), Corrupt (bit/sample-rate damage); clock-syncable; stereo out. **Structurally unusual:** effect state is a rolling record buffer; gate inputs momentarily engage corruptions, making it rhythm-programmable.

### 31. Desmodus Versio — Noise Engineering — Effects (stereo reverb / DSP platform)
Stereo-in/stereo-out synthetic-tail reverb: blend, regen, size, tone, speed knobs — every knob mirrored by a CV input, plus gate input. **Structurally unusual:** it is the flagship of the Versio platform — the same hardware can be re-flashed over USB into a delay, distortion, etc., so the panel legend is only one of several personalities.

### 32. Dixie II+ — Intellijel — VCO (analog)
Bread-and-butter triangle-core VCO: V/oct, exponential and linear FM inputs, PWM input, hard/soft sync, octave switch, LFO mode; six simultaneous waveform outputs (sine, triangle, zigzag, saw, pulse, sub). Structurally the canonical "plain multi-waveform VCO" baseline: one frequency, many concurrent shaped outputs.

---

### Next tier / notable mentions (ranks ~30–120, compressed)
- **Mutable Braids / Tides / Peaks / Warps / Elements** — legacy MI: macro-osc predecessor of Plaits; tidal slope/LFO; dual trigger-to-envelope/drum ("dead man's catch" menu modes); meta-modulator (ring-mod→vocoder cross-fading algorithms, internal VCO normalled to one input); modal voice.
- **Mutable Links & Kinks** — precision utilities: Links = 1:3 buffered mult + 2:1 and 3:1 unity mixers; Kinks = rectifier/logic/S&H with internal noise normalled to the S&H input.
- **Mutable Ears** — contact mic/preamp: one input, three outputs (audio, envelope-follower CV, gate) — canonical audio→CV/gate extractor.
- **Make Noise STO / DPO** — simple sub-timbral VCO; and a dual "complex oscillator" with internal FM/mod bus between its two VCOs plus wavefolder (structurally: two oscillators pre-wired through an internal modulation matrix).
- **Make Noise X-PAN / Erbe-Verb / Rosie** — voltage-controlled stereo mixing/panning; fully CV-able algorithmic reverb; output/cue module.
- **Mannequins Mangrove** — formant oscillator with unusual "air/barrel" params and gate-syncable formant behavior.
- **Doepfer A-124 Wasp / A-140 / A-151** — classic 12 dB multimode filter with separate BP out and LP↔HP crossfade output; textbook ADSR; quad sequential switch (clock-addressed 4-way signal router — pure routing-as-module).
- **Intellijel Planar 2** — joystick vector mixer/router with gesture recording: 4-in/1-out or 1-in/4-out plus X/Y CV outs — direction-reversible signal path.
- **Intellijel Plonk / Rainmaker / Scales / Steppy** — physical-modeling percussion (preset-based, menu); 16-tap stereo delay + comb resonator; quantizer; 4-track gate sequencer.
- **Malekko Voltage Block** — 8 parallel 16-stage CV sequencers with per-channel slew/quantize — an 8×16 CV array under one clock.
- **Mordax Data** — 4-channel oscilloscope/tuner/spectrum tool that is also a dual waveform generator — instrumentation as a module.
- **Expert Sleepers ES-9 / ES-8** — DC-coupled USB audio interfaces (multi-channel CV+audio bridge to computer) — structurally, a bus boundary.
- **Happy Nerding FX Aid / Erica Pico DSP / ALM MFX** — compact multi-effect platforms with switchable algorithms (FX Aid: user-loadable algorithm sets).
- **Moog Mother-32 / DFAM** — semi-modular voices counted in racks; their 32-point normalled patchbays (every jack has a default internal connection that patching overrides) are the purest large-scale example of normalling.
- **4ms RCD / Xaoc Belgrad, Tallin / WMD C4RBN / Bastl Ikarie / Instruō Cš-L / Joranalogue Filter 8** — rotating clock divider (division set rotates under CV); well-regarded filters/VCAs/complex VCO (Filter 8 doubles as an 8-phase quadrature oscillator).

### Currently trending, 2025–26 releases (ModularGrid "popular in racks" hot list)
Distinct from all-time ubiquity but indicative of current design direction: **Intellijel Swells**, **Make Noise Plexiphon** and **GTE**, **Mylar Melodies RANDOM8**, **Shakmat Ballista Blast**, **Befaco Iroi**, **Music Thing Workshop Computer** (user-programmable computer module), **Bastl Déjà Vu**, **Schlappi Eddy/Cascade**, **Xaoc Kamieniec II/Skopje**, **ALM FMco**. Top-rated (quality, not ubiquity) also adds **Pamela's PRO Workout** (#1 rated overall), **Joranalogue Step 8**, **ACL Sinfonion** (chord/harmony quantizer), **Frap Tools Falistri/Brenso/Fumana**, and the **DROID** system (fully text/config-programmable CV computer — directly relevant prior art for a patch language).

**Recurring structural patterns relevant to a patch-language design:** normalled connections broken by patching (Maths SUM bus, Quad VCA/Veils cascades, Rings' input/strum logic, Mother-32); patch-state-dependent behavior (Plaits' trigger detection); polymorphic jacks whose meaning depends on loaded algorithm/model (Disting, Plaits, Versio, FX Aid); multi-channel modules with shared global controls (Batumi, Ochd, QPAS, Three Sisters); internal modulation matrices configured without cables (Quadrax, DPO); persistent array state (Morphagene reels, Turing Machine register, René grid, Metropolix stages, Tempi/René save-states); and one-to-many simultaneous outputs from a single process (QPAS's 8 outs, Ripples' 3 taps, Dixie's 6 waves).

Sources: [ModularGrid module browser, popularity sort](https://modulargrid.net/e/modules/browser) · [ModularGrid Top 100 evaluation lists](https://modulargrid.net/e/modules/evaluationlists) · [DivKid Best Eurorack 2025](https://divkidvideo.com/best2025/) · [MusicRadar best Eurorack modules](https://www.musicradar.com/news/the-best-eurorack-modules-in-the-world) · [ModWiggler: top module per category, 2025](https://www.modwiggler.com/forum/viewtopic.php?t=297178) · [Perfect Circuit: Best of 2025](https://www.perfectcircuit.com/signal/best-eurorack-modular-2025)
