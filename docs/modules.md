# Implemented Module Reference

This is the authoritative agent-facing reference for every module currently in
the runtime registry. It documents implemented behavior, not roadmap entries.

## Signal conventions

- Every connection carries a float at audio rate. Audio/bipolar CV is normally
  `-1..1`; unipolar CV is `0..1`; pitch is volts/octave.
- Gates are high above `0.5`. “Rising edge” means crossing from `<=0.5` to
  `>0.5`; a trigger is one sample, while a gate has duration.
- Values in `{ ... }` set input bases or compile-time configs. Cables sum onto
  input bases. Configs (waveform, mode, arrays, seed) cannot be modulated. Config value
  types and enum entries are checked before a graph is installed.
- No normalling is implicit. Seeded modules are repeatable. Runtime state
  survives hot reload when module name and type remain unchanged.

## DSL essentials

```text
osc = vco { freq: 110hz, wave: saw }
osc -> filter.in
filter.lp -> amp.in : 0.5       # cable gain
0.58333 -> osc.v_oct            # constants also sum onto inputs
seq = stepseq { steps: [0, 0.25, 0.58333], gates: [1, 0, 1] }
```

Unit suffixes are `hz`, `khz`, `ms`, `s` and `bpm`, lowercase, written
directly after the number (`2khz`, `250ms`). They are plain scale factors —
`khz` ×1000, `ms` ×0.001, the rest ×1 — and are **not** checked against the
parameter: `lfo { freq: 500ms }` is 0.5 Hz and `delay { time: 2hz }` is two
seconds. Pick the suffix that matches the input's unit (Hz for `freq`/`cutoff`/
`rate`, seconds for times, BPM for `bpm`). Omitted output means the first output
(a bare `stepseq` name reads its `cv`, a bare `svf` its `lp`). Validate with
`ratsnest check patch.patch`; `ratsnest render ... --stats [dur]` prints
overall peak/RMS/DC plus a per-window level timeline for mix work. CLI
durations (`-d`, `--start`, `--stats`) take `90`, `90s`, `500ms` or `2m`;
`ratsnest` with no arguments prints every option and its default.
`ratsnest describe` lists the runtime modules as JSON; `ratsnest describe fm`
returns one interface, including input bases, config types and enum choices,
outputs, and the default output. It describes built-in modules; compile-time
subpatch constructs are documented below. `check` validates the expanded patch
and exits nonzero on errors; a misspelled module, port, parameter, enum value
or subpatch port is reported with its closest valid name. `check` and `render`
also warn when nothing is cabled into `out` (the patch would be silent). A
clean check does not establish musical balance or sound
quality. A rejected hot reload leaves the previous graph playing.

**Note names** are numeric literals, usable anywhere a number is (`freq: A4`,
array elements, cable literals). Scientific pitch notation: an uppercase letter
`A`–`G`, an optional accidental — `s` = sharp, `b` = flat (`#` can't be used, it
starts a comment) — and one octave digit `0`–`9`, with `C4` = middle C and
`A4` = 440 Hz. A bare note is a **frequency in Hz**: `A4` (440), `Cs4` = `Db4`
(277.18), `A1` (55). So `vco { freq: A1 }` is `vco { freq: 55hz }`.

A trailing **`v` makes it v/oct** instead — octaves from `A3` = 0.0
(`voct = (midi − 57) / 12`), the domain where summed modulation transposes
musically. So `A4v` = `1.0`, `A3v` = `0.0`, `C4v` = `0.25`, `Bb2v` = `−0.9167`.
Feed a pitch straight into an oscillator (`A4v -> osc.v_oct`) or write melodies
as `stepseq { steps: [C4v, E4v, G4v] }` — the sequence carries pitch, not Hz,
so it tracks transposition and lands in tune through a `quant`.

A `…v` note is an **absolute** 12-TET pitch, not an interval. An offset summed
onto a `v_oct` input stays a bare fraction: `0.08333 -> osc.v_oct` is up a
semitone, `1 -> osc.v_oct` up an octave — no note-name form (the reference for
those is the base pitch, not `A3`). Use `…v` for the pitch itself, fractions for
the distance you move it.

### Voice groups

Declared names may contain a dot (`lead.osc = vco { ... }`); no new statement
type, just a name. A dotted declaration whose prefix isn't a `voice` group is
an ordinary module with a dotted name.

```text
poly = alloc { n: 3 }
clk -> poly.gate
key -> poly.voct

harp = voice { notes: poly }
harp.osc = vco { freq: 220hz, wave: tri }
harp.pluck = lpg { damp: 0.5, decay: 0.9 }
poly.voct -> harp.osc.v_oct     # bound bank, bare name: per-instance
harp.osc -> harp.pluck.in       # inside -> inside: instance-local
poly.gate -> harp.pluck.strike
harp.pluck -> out.in : 0.45     # inside -> outside: straight sum of voices
```

`name = voice { notes: <module> }` stamps every `name.<member>` declaration
once per voice, as `name.<member>#i` (`#` starts a comment in source, so
instance names never collide with user names; `watch` shows `harp.osc#2`
like any node). Voice count `n` is inherited from the module bound by
`notes:` — usually `alloc` — which must expose the indexed banks
(`gate1..N`; `voct`/`vel` follow the same convention — validation keys on
`gate1`); an
explicit `n:` on the `voice` block must match or it is a compile error.

Cable resolution depends on where each endpoint lives:

1. **inside -> inside** (same group): instance-local, one cable per voice.
2. **bound module's bare bank name, read inside** (`poly.voct`,
   `poly.spread`): resolves to `<bank><i>` per instance, for every output
   family the bound module exposes. This shadows nothing — `alloc` has no
   mono output named `voct`, so that spelling is an error everywhere else.
   Bank families are 8-wide (`<stem>1..<stem>8`); a narrower run such as
   midi's `cc1..cc4` does not qualify, so bare `mid.cc` is an ordinary
   unknown-output error rather than a bank read.
3. **outside -> inside**: broadcast; the same source feeds every instance.
4. **inside -> outside**: the outside port receives the straight sum of all
   instances. No hidden `1/n` normalization — gain staging is the author's
   job via cable depth, same as every other sum in the DSL.

A mono gate/voct stream (a sequencer, `stepseq`, `turing.bit1..8`, …) can only
express one held note at a time, so this is strum polyphony: each trigger
claims a fresh voice and older voices ring out through their own releases.
See `patches/strum.patch` for a complete three-voice recipe (clocked
random melody -> quantizer -> `alloc` -> a `vco` + `lpg` voice group), and
`docs/superpowers/specs/2026-07-11-voice-alloc-design.md` for the full design.

### Subpatches

A subpatch is a reusable voice in its own file (convention:
`patches/lib/*.sub`) or an inline `subdef` template; instantiating one
stamps a namespaced copy at compile time, like voice groups. See `sub`,
`subdef`, `param`, `input`, `output` below and the kick recipe under
Recipes. Full design: `docs/superpowers/specs/2026-07-18-subpatch-design.md`.

## Quick palette

| Area | Modules |
|---|---|
| Sources | `vco`, `additive`, `lfo`, `noise`, `dust`, `random`, `sampler`, `modal`, `string`, `fm` |
| Tone/dynamics | `vca`, `adsr`, `func`, `lpg`, `svf`, `ladder`, `fold`, `drive`, `overdrive` |
| Routing | `math`, `xfade`, `mix`, `logic`, `switch`, `sh`, `quant`, `follow` |
| Sequencing | `transport`, `scene`, `phrase`, `clock`, `div`, `euclid`, `bern`, `turing`, `stepseq`, `tracker`, `tempo`, `alloc`, `midi` |
| Effects/I/O | `channel`, `clipper`, `compressor`, `dcblock`, `delay`, `eq`, `grains`, `limiter`, `reverb`, `chorus`, `phaser`, `scope`, `stereo`, `rec`, `extin`, `out` |

## `vca`

`vca { gain: 1, response: lin }` — inputs `in` 0, `cv` 0, `gain` 1;
output `out`. Gain is `clamp(gain + cv, 0, 8)`, squared for `response: exp`.
Typical use: `env -> amp.cv` with base gain zero.

## `out`

`out` is built in: cable to it by that name and never declare it (a declared
`o = out {}` is just another node, and `check` warns that nothing reaches the
output). Inputs `in` 0 (both channels), `left` 0, `right` 0, `level` 1; engine outputs
`l`, `r`. It sums mono into each side and applies level. Example:
`mix.out_l -> out.left`, `mix.out_r -> out.right`. Graph output and float WAVs
preserve finite values beyond ±1; use an explicit limiter for peak control.
Only the live audio-device boundary clips to ±1. Internal sums are not normalized.

## `vco`

`vco { freq: 220hz, wave: sine, pw: 0.5, phase: 0 }` — inputs `freq`, `v_oct`, relative
linear `fm`, `pw`, `pwm`, rising-edge `sync`, phase offset in cycles; output `out`. Waves: `sine`,
`tri`, `saw`, `pulse`. Effective Hz = `freq * 2^v_oct + fm * freq`, clamped
to `0..0.45*rate` (negative frequencies stop the phase). `+1` v/oct doubles pitch.
Linear FM depth scales with the `freq` base, not the tracked pitch, so a fixed
`fm` cable gives a brighter index on low notes than high ones. Pulse width is
`pw + pwm` clamped to `0.05..0.95` (non-finite reads 0.5).
Sine is analytic. Free-running triangle, saw, and pulse morph between shared
Fourier-series tables whose highest partial stays below Nyquist; hard sync,
fast FM, PWM, and phase modulation still create discontinuities and can alias.
The saw's Fourier reconstruction has brief Gibbs peaks up to about `+/-1.2`,
and a band-limited pulse rings up to about `+/-1.3`; leave headroom downstream
if it must reach the live device unclipped. Phase wraps in cycles; `freq: 0`
holds the oscillator while `phase` can drive it externally.
The 220 Hz default makes `A3v` sound A3; another base deliberately transposes. A pulse at `pw != 0.5` carries a DC offset of about
`2 * pw - 1` that low-pass filters (`svf.lp`, `ladder`) pass straight through;
keep `pw` at `0.5` or block DC downstream if the path is DC-coupled.

## `additive`

`additive { freq: 220hz, levels: [0.6, 0.3, 0.15] }` — a free-running sine
bank with 1..128 partials. Inputs `freq` 220 Hz, `v_oct` 0, `gain` 1
(clamped −8..8), `tilt` 0 (dB/oct, clamped ±48), `phase` 0 (cycles added to
every partial), and rising-edge `sync` 0. Output `out` is the straight sum,
without normalization or embedded envelopes. Patch an external envelope into
`gain` with declared `gain: 0`, or use a VCA after the bank.

`levels` sets signed amplitudes in −1..1 (default `[1]`). Optional `ratios`
sets positive frequency multipliers (default 1,2,3,...); optional `phases`
sets offsets in cycles (default all zero). Supplied arrays must match the
length of `levels`; ratios are limited to 1000000. Arrays are static configs.
Use gain for overall articulation and tilt for live spectral changes:
each amplitude is multiplied by `10^(tilt * log2(ratio) / 20)`, with that dB
adjustment bounded to ±120. A −6.0206 dB/oct tilt halves successive octaves.

Partial Hz = `freq * 2^v_oct * ratio`. Base freq clamps to `0..sample_rate`,
v_oct to ±24. Partials fade linearly between 0.45 and 0.5 times sample rate
and are silent at/above Nyquist; their phase continues advancing. This protects
steady partials, not sidebands from fast gain/pitch/phase modulation.
Sync resets all accumulators; configured offsets still apply. On reload,
phase follows array position; new positions start at zero. A stopped bank
(`freq: 0`) can produce DC according to its phases.

## `lfo`

`lfo { freq: 1hz, wave: sine, phase: 0 }` — inputs `freq`, rising-edge `reset`,
phase offset in cycles; bipolar output `out`. Double-precision accumulation
keeps minute-long periods accurate. Waves: `sine`, `tri`, `saw`, `square`.
Negative `freq` clamps to 0 (stopped; it never runs backward). The waves are
naive (not band-limited): at audio rates `saw` and `square` alias — use `vco`.
Example: `slow -> filter.cutoff : 300`.

**Starting value at `phase: 0`** (measured) — this matters whenever an LFO is
the structure of a piece rather than a wobble:

| wave | value at t=0 | then |
|---|---|---|
| `saw` | −1 | ramps up to +1 |
| `tri` | +1 | ramps down, then back |
| `square` | +1 | holds, then flips |
| `sine` | 0 | rises |

So a single `saw` at 0.0033 Hz is a monotonic −1 → +1 ramp across five
minutes: the cheapest "one thing changes slowly" arc there is. A `tri` at the
same rate ramps the wrong way first.

## `clock`

`clock { bpm: 120, run: 1 }` — inputs `bpm`, `run`, rising-edge `reset`;
outputs 50%-duty `out` and normalized `run`. Stop forces clock low but holds
phase. Negative BPM clamps to 0 (holds). Use BPM 240 for eighth-note gates at
a musical 120 BPM.

## `transport`

`transport { bpm: 120, beats: 4, bars: 4, division: 4, swing: 0 }` is a shared
musical clock. BPM counts quarter notes. Config `beats` is quarter notes per bar
(1..32), `bars` is bars per phrase (1..256), and `division` is subdivisions per
quarter note (1 or an even integer through 32). Thus 6/8 uses `beats: 3`, and
`division: 2` produces eighth notes. This is a quarter-note count, not a meter label.

Outputs `clock`, `beat`, `bar`, and `phrase` are **one-sample pulses**, including
at the first running sample. `gate` is high for half of each subdivision;
`beat_phase`, `bar_phase`, and `phrase_phase` are ramps from 0 up to (not including)
1. `running` reports playback state. Use `clock` for sequencing and `gate` when
note duration should follow the subdivision. Use `phrase_phase` into an
`lfo { freq: 0 }` phase input for tempo-locked slow modulation.

Input `swing` clamps to 0..0.75 and delays every second subdivision in a pair;
0.5 produces a 3:1 pair. Swing is latched at beat boundaries. Downbeats, bars,
and phrase duration stay fixed. `run: 0` (or BPM <= 0) stops pulses and gate,
freezing phase; resume continues there without an extra clock pulse. A gate may
rise on resume if paused in its high half. Rising `reset` rewinds to the first
beat, even while stopped; the downbeat fires when running. BPM clamps to 0..1000;
invalid BPM stops. Timing/phase survive reload; configuration changes reinterpret
that position. It does not yet follow external MIDI clock.

## `scene`

`scene { bars: [4,8,4], request: -1, hold: 0 }` advances an arrangement from
bar-clock edges. Config `bars` requires 1..64 positive integer durations
(1..65536 bars each). Connect `transport.bar` to `clock`.

Outputs: zero-based `index`, entry pulse `trig`, zero-based local `bar`, and
`pending` (-1 means none). First clock enters scene zero; durations expire into
the next scene and the final scene wraps. Connect `index` to addressed `switch`
modules to select section harmony, density, levels, and freeze controls from one
shared address. Switches support eight alternatives each; larger forms can nest them.

Changing input `request` queues a scene for the next bar; another request replaces
it. Setting -1 cancels a pending request. A rising `launch` queues the current
request again, allowing restart of the same scene. Requests clamp to valid indices.
The request arriving on a bar edge wins on that edge. `hold: 1` repeats the current
scene's duration without an entry trigger; an explicit launch still wins. Rising
`reset` clears position and pending state; the next bar starts scene zero unless
a new request is also made. Pending selection and position survive reload.

Discrete inputs (including scene requests) jump on live parameter edits rather
than sliding through intermediate indices. `describe` exposes this as
`glide: false`. Cable values remain audio-rate; this flag only controls engine
parameter glides. Scene indices are numeric; named scene authoring is not yet a
DSL feature. See `patches/keepsake.patch` for discover/bloom/dissolve sections.

## `phrase`

`phrase { length: 16, freeze: 0, rotate: 0, transpose: 0 }` captures a control
phrase into 256 preallocated slots. Each rising `clock` advances one step and,
when `freeze` is low, writes `in`. `freeze: 1` loops the captured values without
writing. `length` is rounded/clamped to 1..256; unwritten slots contain zero.
Optional config `values: [...]` initializes up to 256 slots; set `freeze: 1` to
play them unchanged. It does not set length automatically.

`out` reads the current slot, `tap1..tap3` the preceding slots (wrapping within
length); integer `rotate` shifts the read position, leaving write position alone.
`transpose` adds to all four values (v/oct when carrying pitch). Outputs `trig`
and `eoc` mark clock edges and wraps; `position` is the zero-based write/play step.
Rising `reset` rewinds without erasing memory; a coincident clock reads/writes
step zero. Frozen playback, read rotation, and transpose remain independent.
Live memory and position survive reload; `values` is initialization data, so
changing it on a migrated instance does not replace the captured phrase.

## `div`

`div { by: 2 }` — inputs `in`, rounded positive `by`, rising-edge `reset`;
output `out`. Produces an approximately 50%-duty divided gate. Coincident reset
and input edge counts as step zero.

## `func`

`func { rise: 0.01, fall: 0.1, curve: lin, cycle: off }` — inputs rising-edge
`trig`, slew `in`, rise/fall seconds; outputs unipolar `out` and two
one-sample pulses: `eor` when the rise reaches 1, `eoc` when the fall reaches 0.
Curves (as in `adsr`): `lin`, `exp`, `log`, `quad`; cycle: `off`/`on`. A trigger
mid-cycle rises again from the current level. With nothing
patched to `trig`, `out` slews toward `in` at the rise/fall rates — this is
the intended way to smooth stepped CV (section fades, portamento).

## `svf`

`svf { cutoff: 1000, res: 0, v_oct: 0 }` — inputs `in`, cutoff Hz,
resonance `0..1`, octave tracking `v_oct`; outputs `lp`, `bp`, `hp`.
Effective cutoff = `cutoff * 2^v_oct`, clamped to `5..0.45 * sample_rate`
(`v_oct` clamps to ±24). Small-signal response at the cutoff: −6 dB at
`res: 0`, unity at 0.5, +14 dB at 0.9, +23 dB at 0.95. Damping grows with the
band-pass level (scaled by `res`, so `res: 0` is exactly linear and unchanged),
which keeps loud resonance bounded: a 0.8 sine at the cutoff peaks at 1.7 at
`res: 0.9` and 2.2 at `res: 1` (a ±4 input: 3.5 / 3.7). Above about **0.97 it
self-oscillates** with no input into a clean sine of about ±1 on all three
outputs (THD ≈0.4 % `lp`, 1.3 % `bp`, 4 % `hp`), within ~1 cent of the
effective cutoff from 55 Hz to 3.5 kHz — at `res: 1` it is a sine oscillator
played through `v_oct`. Input still mixes in, and a loud input pulls the
oscillation's level around. Patch note pitch into `v_oct` for musical
tracking. Modulate `cutoff` for additive Hz movement, or `v_oct` for
proportional movement; both remain audio-rate.

## `noise`

`noise { color: white, seed: 1 }` — no inputs; bipolar `out`.
Colors describe approximate power-spectrum slopes:

| Color | Slope | Construction / use |
| --- | --- | --- |
| `white` | 0 dB/oct | Uniform random samples; broad hiss and excitation |
| `pink` | −3 dB/oct | Economy pink filter; balanced octave energy |
| `brown` | −6 dB/oct above 10 Hz | Leaky integration; rumble, finite DC power |
| `blue` | +3 dB/oct below Nyquist | Differentiated pink; brighter air |
| `violet` | +6 dB/oct below Nyquist | Differentiated white; high-frequency emphasis |

Slopes are approximations over the audio band, not exact at DC or Nyquist.
Pink coefficients are calibrated around 44.1/48 kHz and shift in Hz with sample
rate. Brown's 10 Hz corner scales with sample rate. Colors have different RMS
levels; there is no loudness matching or hard clipping. White/violet are bounded
by ±1; filtered colors may exceed ±1. Equal seeds and rates reproduce streams;
filter/RNG state survives reload. Use `random` for slow wandering CV.

## `dust`

`dust { rate: 10hz, seed: 1 }` — audio-rate input `rate` is average events per
second, clamped to `0..sample_rate`. Outputs `out` (uniform bipolar random
impulses, zero between events) and `trig` (1 on event samples, otherwise 0).
This is a Bernoulli trial per sample; timing is irregular, not a clock.
Adjacent events can keep `trig` high across samples, so rising-edge destinations
can merge them at high rates. Zero rate silences both outputs. Seeded and
state-migratable. Use `out` to excite a resonator or filter for crackle; use
`trig` for sparse irregular envelope strikes.

## `math`

`math { op: add }` — inputs `a` 0, `b` 0; output `out`. Ops: `add`, `sub`,
`mult`, `min`, `max`, `abs`, `clip`, `wrap`, `exp2`. `abs` and `exp2` ignore b;
clip/wrap use `abs(b)` as the symmetric range. `mult` is ring modulation;
set `b` or patch it explicitly. `exp2` computes `2^a` (a clamped to ±126;
non-finite a gives zero). For absolute pitch-to-Hz conversion, feed note CV
into `exp2.a`, then cable its output at depth 220 into a destination with Hz
base zero. Prefer a filter's `v_oct` input for direct key tracking.

## `xfade`

`xfade { law: linear }` — inputs `a` 0, `b` 0, `mix` 0.5 (`0..1`); output
`out`. Default linear interpolation preserves identical signals and works for
CV. `law: equal_power` gives each input −3 dB at midpoint, useful for unrelated
audio; correlated inputs can gain 3 dB at the midpoint.

## `drive`

`drive { amount: 0.5, mode: soft, quality: raw }` — inputs `in`, amount `0..1`; output
`out`. The pre-gain is `1 + 9 * amount`, so `soft` computes
`tanh(in * (1 + 9 * amount))` and `hard` clamps the same product to `-1..1`.
Note even `amount: 0.1` is a ~2x boost into the curve; on a full mix that is
a limiter, not glue. Config `quality` is `raw` (default) or `clean`. Raw is the
original zero-latency path, including its useful aliasing character. Clean runs
the same curve through an 8x elliptic oversampler, has a four-sample peak
latency (0.083 ms at 48 kHz), and costs more CPU. Its reconstruction filter can
ring beyond `+/-1` on transients even though the shaping curve itself is
bounded, so leave downstream headroom. Align parallel raw/clean paths manually;
the graph does not add hidden delay compensation.

## `fold`

`fold { quality: raw }` — inputs `in` 0, non-negative `fold` 1, symmetry offset
`sym` 0; output `out` nominally folded into `-1..1`. Config `quality` is `raw`
(default) or `clean`. Raw preserves the original zero-latency corner-rich
character. Clean combines first-order antiderivative antialiasing with 8x elliptic
oversampling and has a four-sample peak latency (0.083 ms at 48 kHz). It is the
more CPU-intensive option and is intended for exposed or high-frequency folds.
Its reconstruction filter can overshoot the nominal range on transients; leave
downstream headroom. Align parallel paths manually. Best paired with a triangle
oscillator and modulated fold.

## `sh`

`sh { mode: sample }` — inputs `in`, `trig`; output held `out`. `sample`
captures on trig rising edges; `track` follows while trig is high and holds
while low.

## `ladder`

Inputs `in` 0, `cutoff` 1000 Hz, `res` 0 (`0..1`), `drive` 0 (`0..4`),
`v_oct` 0; four-pole low-pass output `out`. Effective cutoff = `cutoff * 2^v_oct`,
clamped to `5..0.45*rate` (`v_oct` clamps to ±24). `drive` saturates the
input (0 is an exact bypass); the feedback path has its own tanh saturator,
so resonance is bounded: a 0.8 sine at the cutoff peaks at 1.5 at `res: 0.9`
and 2.2 at `res: 1` (a ±4 input: 4.0 / 5.5). Small-signal gain at the cutoff:
−12 dB at `res: 0`, −4 dB at 0.5, +6 dB at 0.75, +21 dB at 0.9. Above about
**0.93 it self-oscillates** with no input into a clean sine (THD ≈0.25 %) that
reaches ±1 at `res: 1`, within 0.5 cent of the effective cutoff — play it as
an oscillator through `v_oct`. The passband still drops as `res` rises, but
only partly (input gain `1 + 0.6·k·res²` against the loop's `1/(1+k)`): at
100 Hz, cutoff 1 kHz, −5.6 dB at 0.25 (uncompensated −5.9), −7 dB at 0.5
(−9.5), −6 dB at 0.75 (−12), −4.5 dB at 0.9 (−13). At `res: 0` it is
unchanged. Example: `env -> filter.cutoff : 1800` adds Hz;
`notes.cv -> filter.v_oct` tracks pitch by octaves without a negative cutoff.

## `lpg`

Inputs `in`, continuous `cv`, rising-edge `strike`, `damp` 0.5, `decay` 0.25 s;
output `out`. Strike excites a vactrol-like amplitude/brightness decay; CV can
hold it open. `cv` clamps to `0..1` and opens with a fixed ~2 ms rise; a strike
ramps the gate fully open over a fixed 2 ms (linear), then decays: still a
percussive "bonk", without the click an instant opening made on bass-heavy
sources. `decay` is the one-pole time constant (≥ 1 ms; about −60 dB
after 7× it). The two-pole, non-resonant lowpass sweeps from 30 Hz closed to
`30 * 2^(9 − 6*damp)` Hz open: `damp: 0` reaches ~15 kHz, `1` only ~240 Hz.

## `random`

`random { rate: 1hz, smooth: 1, seed: 1 }` — internally clocked bipolar
generator. Outputs cosine-interpolated `out` and raw `stepped`; smooth clamps
to `0..1`; negative `rate` clamps to 0 (holds). It has no clock input — for
clocked random values use `noise -> sh` or `turing`.

## `quant`

`quant { scale: [0,2,3,5,7,9,10], root: 0, transpose: 0 }` — inputs `in`
v/oct, root semitones relative to A, transpose v/oct; outputs quantized `out`
and one-sample `changed`. Scale entries are integer semitones reduced modulo 12;
an empty scale is chromatic. Ties snap down. Fractional scale entries are rejected.

Config `mode: continuous` (default) updates every sample. `mode: clock` captures
pitch, root, mask, and degree only on rising `clock`; output starts at zero and
holds between edges. `mask` (default 4095) is a 12-bit allowed-note mask relative
to the root, intersected with the configured scale. An empty intersection holds
the previous pitch. `degree` (rounded, -128..128) transposes through the remaining
scale degrees *after* quantization, including across octaves. `transpose` remains
pre-quantization v/oct. Root clamps to +/-1200 semitones and the relative input to
+/-12000 semitones. `changed` detects final pitch changes, including root changes,
and is silent on the first captured note. Use the clock to articulate repeated
notes; `changed` deliberately does not retrigger them.

## `mix`

Eight inputs `in1..in8` and additive level CVs `lvl1..lvl8`; configs `levels`
(missing = 1) and `pans` (`-1` left, `0` center, `1` right; missing = center).
Outputs `out_l`, `out_r`. Each gain is `clamp(level + lvl, 0, 8)`. Pans are
equal-power, so a centered input reaches each side at −3 dB (0.707). Negative
level CV is useful for sidechain ducking. `levels`/`pans` are configs: a live
edit jumps (no glide), and pans cannot be modulated — cross two `vca`s for an
auto-pan.

## `channel`

`channel { level: 0, balance: 0, mute: 0, send: -96, send_mode: post }` is a
stereo channel or bus. `in_l` and `in_r` stay separate; connect a mono source to
both explicitly. Level and send are dB (-96..24); -96 is silence. Stereo balance
(-1..1) attenuates the opposite channel, with unity on both channels at center.
This is a balance control, not a constant-power mono panner. Gain, balance, mute,
and send changes are smoothed over 5 ms. No normalization or clipping is applied.

`out_l/out_r` are the main outputs; `send_l/send_r` feed a shared effect. Config
`send_mode: pre` taps before fader/balance, while `post` taps after; mute affects
both. Effects returns can feed another channel. `peak` is a stereo sample-peak
meter with 300 ms decay; `rms` is stereo mean-square power smoothed over 300 ms,
converted to dBFS. Neither is LUFS or true peak. Meter floor is -240 dBFS.

## `scope`

`scope { label: name }` — input `in`, no patch output. Captures last/peak state
for watch and future TUI tooling without altering the signal path.

## `delay`

Inputs `in` 0, `time` 0.4 s (`0..10`), `pitch` 0 v/oct, `feedback` 0.35
(`0..1.2`), equal-power `mix` 0.5, feedback `tone` 8000 Hz (`20..0.45*rate`),
`slew` 0.1 s (one-pole time constant of the time glide); output `out`.
Effective time is `time * 2^-pitch` (at least 2 samples). The dark,
DC-blocked, tanh-saturated feedback can self-oscillate above 1. `tone` sits in
the feedback path only: the first echo is undarkened, later repeats darken. Example: `{ time: 375ms, feedback: .65, mix: 1 }`.

## `adsr`

Inputs `gate`, `attack` .005 s, `decay` .15 s, `sustain` .7 (`0..1`, live),
`release` .3 s; unipolar `out`. Retriggers and releases from the current level
rather than jumping. Segment times are durations, whatever the start level; 0
is a one-sample jump.

Config `curve` bends every segment by progress `x` through it, always
arriving on its target exactly at the segment time: `lin` is linear; `exp` is
a true RC exponential, normalized to land on time — decays and releases run
`(1 − e^(−kx)) / (1 − e^(−k))` with `k = ln 1000`, so the stage time is the
−60 dB time (T60): 0.03 of the way left at half the decay (−30 dB), the
natural percussive/plucked fall. Attacks use `k = ln 3`, the analog shape
that charges toward 1.5 and stops at 1 (convex: 0.63 at half the attack).
`log` is `√x` (0.29 left at half the decay); `quad` is `x²` (slow start: a
soft swelling attack, but decays and releases that hang near the top, then
drop — 0.75 at half the decay time; this was `exp` before it became a true
exponential). An `exp` decay reaches −20 dB at a third of the stage, `quad`
at 95%: write about 3x the time for the same heard length
(`docs/patch-craft.md` has the conversion and level makeup).

## `reverb`

Stereo Dattorro plate. Inputs `in_l`, `in_r`, `decay` .5 (`0..0.9995`, the
tank loop gain), `damp` .25 (`0..1`, tank high-frequency loss), equal-power
`mix` .3, `freeze` gate; outputs `out_l`, `out_r`. The tank input is the mono
sum; the stereo image comes from the output taps. Configs: `predelay` .02 s
(`0..0.25`), input `bandwidth` .9995 (`0..1`), `mod_rate` 1 Hz (`0..20`),
`mod_depth` 13 samples (`0..100`). Freeze smoothly mutes input and makes the
tank lossless while held.

## `euclid`

Inputs `clock`, rising-edge `reset`, `steps` 16 (`1..64`), `hits` 4
(`0..steps`), wrapped `rot` 0. Outputs one-sample `trig` and clock-width `gate`.
Example: `euclid { steps: 16, hits: 5, rot: 3 }`.

## `bern`

`bern { prob: .5, seed: 1, length: 0 }` routes each rising `gate` to either
`a` or `b`, retaining the input gate width. `prob` is the probability of A.
Held output `value` exposes the random draw, useful for pitch and accents.

`length` (rounded 0..65536) repeats the random stream every N incoming edges;
0 free-runs. Rising `reset` rewinds the current seed; rising `reroll` selects a
new deterministic seed and rewinds. Both take effect before a coincident gate.
Probability changes do not change the sequence of random values: even 0 and 1
consume one draw per edge. This corrects the old density-dependent random stream.
For a beat-length loop, reset from `transport.phrase` and clock from its regular
subdivision output. Clocking only on Euclidean hits counts *hits*, not elapsed
musical time. The decision for an already-high gate stays fixed until the next
edge. Random state, loop position, and held decision survive reload.

## `logic`

`logic { op: and, threshold: .5 }` — inputs `a`, `b`, threshold; normalized
gate `out`. Ops `and`, `or`, `xor`, `not`, `cmp`; Boolean ops threshold inputs,
not negates a, and cmp tests `a > b`.

## `switch`

Eight signal inputs, plus `clock`, rising-edge `reset`, zero-based `addr`, and
`n` 8 (`1..8`); output `out`. Config `mode`: `seq` (the **default**) advances
on clock and ignores `addr`; `addr` reads the live address (rounded, clamped
to `0..n-1`) and ignores the clock. A selector driven by a lane must say
`switch { mode: addr }`, or it sits on `in1` forever, silently. Config `fade`
(seconds, default `0.005`, clamped `0..1`): a selection change slews the
step between the old and new input out linearly over `fade`, so switching
running audio does not click. **Routing pitch CV or gates, set `fade: 0`**
— the instant cut: a slewed pitch glides through wrong notes (an `sh`
sampling on the same clock edge catches it mid-slew) and a slewed gate
crosses 0.5 late. Without a selection change the output is the selected
input exactly. Examples: `switch { mode: seq, n: 4 }` (audio),
`switch { mode: seq, n: 8, fade: 0 }` (an arp's pitch).

## `turing`

`turing { len: 8, prob: .1, seed: 1 }` — inputs `clock`, `write`, `clear`,
length `1..16`, mutation probability. Outputs normalized low-byte `out`,
one-sample incoming-bit `pulse`, and current `bit1..bit8` gates. Prob 0 locks a
loop; write forces a high incoming bit; clear zeros the whole register.

## `stepseq`

Configs `steps` CV array, `gates` array (missing = on),
`lens` (clock ticks per step, missing = 1), `ties` (missing = 0), `vels`
(missing = 1), `dir` (`forward`, `reverse`, `pingpong`, `random`), `seed`.
Inputs `clock`, reset, `len` (0 = full array; counts steps, not ticks),
transpose. Outputs held `cv`, one-sample `trig`, `gate`, forward/reverse
wrap `eoc`, and held `vel`.

An omitted or empty `steps` supplies zero pitches for the longest of
`gates`/`lens`/`ties`/`vels`, or `[0]` if all are empty. This lets rhythm-only
patterns such as `stepseq { gates: [1, 0, 1, 0] }` run all four steps.
An explicit nonempty `steps` sets the pattern length; extra companion entries
are ignored. Shortening `len` or hot-reloading a shorter note list moves a
removed current step to the last remaining step, starting its tick count
over and preserving clock-edge memory.

```text
s = stepseq { steps: [0, .25, .58333], gates: [1, 0, 1] }
clock -> s.clock
s.cv -> osc.v_oct
s.trig -> env.trig
```

The four arrays are parallel to `steps`; together they are a note list. A
step lasts `lens[i]` rising clock edges, and its `gate` stays high across the
clock's low halves until the step's **last** tick, where it follows the clock
down so the next step re-articulates. A step marked in `ties` continues the
previous note: no `trig`, and the gate never drops into it — a tie when the
pitch is the same, a slur when it moves. (A tied step after a rest simply
starts a note; the lookahead is forward-order, so slurs are a `forward`
feature.) `vel` holds `vels[i]` for the step, for accents. Note lengths round
to the nearest integer and clamp to `1..2147483647`; non-finite lengths use 1.

```text
# 4/4 at a sixteenth clock: half note, quarter slurred into an eighth,
# a sixteenth rest, an accented sixteenth. 16 ticks, five entries.
mel = stepseq { steps: [D4v, F4v, E4v, 0, A4v], gates: [1, 1, 1, 0, 1],
                lens: [8, 4, 2, 1, 1], ties: [0, 0, 1, 0, 0],
                vels: [.8, .7, .7, 0, 1] }
```

## `tracker`

`tracker { file: "song.trk" }` plays a song written as a plain-text pattern
file: tracks, named patterns of rows, and an order list. Each rising `clock`
edge plays one row (rows-per-beat is the clock's business). Inputs (all
discrete) `clock`, rising-edge `reset`, and `start` (the order step `reset`
rewinds to, 0-based, rounded and clamped). Outputs, per track n = 1..8:
`voctn`, `gaten`, `veln`, `cvan`, `cvbn` (banks like `alloc`'s; undeclared
tracks output 0); then `step` (order step), `pattern` (file index of the
step's first pattern), `row` (row within the step), one-sample `step_trig`
and `row_trig`, and `end`. A bare `song` reads `voct1`.

The `file:` path is quoted and resolves against the directory of the file
that declares the module (like a `sub`, unlike `sampler`). The loader reads
it at build time, reports `.trk` mistakes as `song.trk:line:col`, embeds the
song as config `text`, and `play` **watches it**: saving the `.trk`
hot-reloads, keeping the order step and row (clamped to the edited song).

```text
# song.trk -- one row per sixteenth
track kick  note            # 1: gate1
track bass  note vel        # 2: voct2 gate2 vel2
track harm  note cva cvb    # 3: root, third, fifth as v/oct

pattern beat 16 kick        # one bar, loops under longer patterns
00 | x
04 | x
08 | x
12 | x

pattern verse 64 bass harm  # four bars; step length comes from here
00 | D2  .9     | D3  F4  A3
08 | off        | .
16 | Bb1        | Bb2 D4  F3
32 | F2         | F3  A4  C4
48 | C2         | C3  E4  G3
62 | ~D2                         # slur: new pitch, no re-attack

order verse*2               # sections accumulate, one per line
order |: verse+beat*3       # '|:' = loop point; without one the song ends
```

- `track <name> <fields>` declares tracks 1..8 in order, with the fields its
  cells hold (`note`, `vel`, `cva`, `cvb`), written in that order.
- `pattern <name> <rows> [tracks]` (1..1024 rows) over the listed tracks
  (default all). Rows are `[n] | cell | cell`; a row number is optional
  (decimal, increasing) and rows never written are empty, so patterns can be
  sparse. Cells hold whitespace-separated field tokens; trailing ones may be
  omitted; `#` after a space starts a comment.
- Note tokens: `C4`, `Cs4`, `Bb3` (the DSL's spelling, always v/oct with
  A3 = 0) or a number = new note; `x` = new note at the held pitch; `~D4` =
  slur (pitch change, no re-attack); `off`/`===` = note off; `.`/`...`/`-`/
  `---` = empty. Values fill the track's fields in declared order, so a
  `note vel` cell is `C4 .8`, never `.8 C4`. `vel` is a number in 0..1 (1
  until first written); `cva`/`cvb` take numbers or note names (0 until
  written). Note names are uppercase.
  **Every value holds until changed.**
- `order` entries: `name`, `a+b` (layers: the first sets the step length,
  the others loop inside it, tracks must not overlap), `entry*N` repeats
  (`*N` binds to the whole layer group: `a+b*3` is `(a+b)` three times, and
  each repeat is its own `step`), and
  `|:` the loop point. Capped at 65536 rows.

Composing (all resolved when the file loads; the ports never change):

- **Transpose** a layer in the order list: `order verse verse^5 bass^-2+beat`
  (semitones, ±48). It moves the note field and any `cva`/`cvb` value written
  as a note name; numeric levels stay put.
- **Note length:** `track hat note len 1` (every note sounds one row), or per
  cell `C4:3`, `x:2`, `~D4:4`. The gate drops L rows later unless another
  note, slur or `off` on that track comes first; counted in song rows, so a
  looping layer's note can ring into the next step. No `len` holds.
- **Chord tones:** `track arp note vel from harm` (an earlier track with
  `note`, optionally `cva`/`cvb`). In `arp`'s cells `t1`, `t2`, `t3` are
  harm's resolved note, cva, cvb on that song row (after harm's `^N`), with an
  optional semitone offset and length: `| t1+12 .8`, `| ~t3-12:2`. They work
  in the note field (a note; `~t2` slurs) and in `cva`/`cvb`. One 16-row riff
  then loops under every chord: `order chords+arp`.
- **Overlays:** in a layer group, `name@R` plays once from row R of the step
  (not looping), and its non-empty events replace the earlier layers' on
  the same tracks: `order verse+beat+fill@48`. Only overlays may share
  tracks; the first layer cannot be one.
- **Drum lanes:** inside a pattern, `lane kick x... x... x..x x.0.` writes
  one row per character from row 0 (spaces ignored): `.` empty, `x` hit,
  `1`–`9` hit at vel n/9 (if the track has `vel`), `0` off. Lanes and row
  lines mix; writing the same cell twice is an error.
- **Bars:** `pattern verse 4x16 bass harm` is four bars of 16 rows (64).
  Its row labels, and `@R` overlays over it, may then be `bar:row`, bar
  from 1 and row from 0: `3:06` is row 38. Plain numbers still work.
- **Sections:** `section verse mel+pad+bass+beat` names one layer group
  (with `^N` and `@R` as usual); then `order verse*2 chorus verse^2+fill@48`.
  A section's `^N` moves every written pitch in it once: chord tones already
  follow their transposed source, so it skips them (a layer's own `^N`
  still applies). Sections share the pattern namespace and hold patterns,
  not other sections.
- **Ramps:** a trailing `>` on a `vel`/`cva`/`cvb` value (`.2>`, `A3>`)
  glides linearly, one step per row, to the next value written in that field
  later in the same pattern (an error if none follows). Still stepped per
  row: slew with `func` for a smooth fade.

Timing: row 0 sounds on the first edge (t = 0 under a `clock`/`div` chain;
before it gates are low). A re-articulated note drops its gate for the edge
sample only, so gates work with any clock duty, one-sample pulses included;
slurs keep the gate high; `off` drops it on its row. Without `|:` the edge
after the last row ends the song: gates fall, values hold, `end` stays high
until `reset`; with it the song wraps (`end` pulses). A coincident reset and
clock edge plays row 0 of step `start`. No effect column, tempo or polyphony
per track: patch modules to the CVs. Full design:
`docs/superpowers/specs/2026-10-21-tracker-design.md`; a worked port:
`patches/mainstage-trk.patch`.

## `tempo`

Inputs `bpm` 120 and `beats` 1; outputs `seconds = 60 * beats / bpm` and `hz`.
Beat values: quarter 1, eighth .5, dotted eighth .75, sixteenth .25. Example:
`tempo { bpm: 120, beats: .75 }` produces .375 seconds.

## `alloc`

`alloc { n: 4, mode: rr }` — inputs `gate`, `voct`, `vel` 1; output banks
`gate1..8`, `voct1..8`, `vel1..8`, `spread1..8` (bipolar constants spread
over -1..+1; 0 when n = 1). Each rising gate claims the next voice (`rr`)
or all voices (`unison`); a voice tracks `voct` while it is the newest
claim and holds it after, so release tails keep pitch. Reclaims insert a
one-sample gap (a defensive guarantee of the bank convention — unreachable
from a mono gate, where every reclaim already follows a low sample; midi
exercises it). Strum polyphony from any mono sequencer; see `voice`.

## `midi`

`midi { channel: 0, voices: 1, mode: last, bend: 2, cc: [] }` — no inputs;
output banks `gate1..8`, `voct1..8`, `vel1..8`, plus `bend`, `at`, `cc1..cc4`.
Pure event -> CV: it consumes raw MIDI (via the engine's `midi-in` port; see
`docs/superpowers/specs/2026-07-11-midi-design.md`) and never touches the
audio graph's inputs. Offline `render` has no transport, so a midi module
outputs defaults there — a keyboard nobody is playing.

There are deliberately **no** mono convenience outputs named `gate`/`voct`/
`vel`: those bare spellings are reserved by the voice-group bank-resolution
rule (`alloc`/`midi` expose only banks so `poly.voct` can resolve to
`voct<i>` per instance and shadow nothing). A mono synth is `voices: 1` and
`mid.gate1` — explicit, per design rule 4. Bind straight into a voice group:
`keys = voice { notes: mid }`.

- `voct<i>` = `(note − 57) / 12` (MIDI 57 / A3 = 0.0 at 220 Hz); `vel<i>` is
  velocity `0..1`. Both hold after note-off so release tails keep pitch.
- Pitch bend (scaled by `bend` semitones, default 2, clamp `0..24`) is added
  into every `voct` output; the `bend` output carries that same bipolar value
  in v/oct units (`bend / 12` at full deflection) for routing elsewhere.
- `voices` `1..8`: each note-on claims a free voice round-robin; at capacity
  it steals the **oldest** sounding voice, inserting a one-sample gate gap so
  envelopes retrigger. Note identity is tracked, so note-off drops the right
  voice.
- `mode` applies only at `voices: 1`: `last` = newest key wins and previous
  held keys restore on release (legato — the restore does **not** re-gap or
  retrigger); `lowest` = the lowest held key sounds (bass-pedal style).
- `cc: [74, 1]` maps `cc1` to controller 74, `cc2` to the mod wheel, etc.
  (≤ 4 entries, each clamped `0..127`, only the first four read); each `cc<k>`
  is `0..1`, smoothed by a fixed ~3 ms one-pole. Unmapped slots output 0.
- `at` is channel pressure `0..1`. Sustain (CC 64) defers note-offs until the
  pedal releases. `channel` `0` = omni (default), else `1..16`; velocity-0
  note-ons are note-offs. All tracker state survives hot reload.

## `dcblock`

`dcblock { cutoff: 10 }` — inputs `in_l` 0, `in_r` 0, `cutoff` 10; outputs `out_l`,
`out_r`. A first-order high-pass filter that removes DC offsets below the
cutoff frequency (clamped `1..40` Hz; changes are smoothed around 20 ms, but
the first sample starts at the patched cutoff, so there is no startup sweep). Protects speakers
and downstream dynamics processors from sub-bass drift. Silence is exact, and
all internal filter states migrate during hot reload.

## `stereo`

`stereo` — inputs `in_l` 0, `in_r` 0, `width` 1, `balance` 0, `mono` 0, `swap` 0;
outputs `out_l`, `out_r`, `mid`, `side`, `corr`. A stereo field utility.
*   `width` (clamped `0..2`, default 1): Adjusts Mid/Side width. `0` = mono, `1` = identity,
    `2` = doubled side (and zero mid). Smoothed around 20 ms.
*   `balance` (clamped `-1..1`, default 0): Equal-power balance panner. `-1` = hard left,
    `1` = hard right, `0` = center (which attenuates each side by -3 dB). Smoothed around 20 ms.
*   `mono` (default 0): Gate. If `>0.5`, sums outputs to mono. Level-sensed.
*   `swap` (default 0): Gate. If `>0.5`, swaps left and right output channels. Level-sensed.
*   `mid` / `side`: Outputs the mid and side components of the balanced output signal.
*   `corr`: Output. Real-time stereo correlation value between `-1.0` (fully out
    of phase) and `1.0` (identical in phase), accumulated using a 300 ms exponential window.

## `compressor`

`compressor { sidechain: internal }` — inputs `in_l` 0, `in_r` 0, `side_l` 0,
`side_r` 0, `threshold` -18, `ratio` 4, `knee` 6, `attack` 0.01, `release` 0.1,
`makeup` 0, `mix` 1; outputs `out_l`, `out_r`, `gain`, `gr`. A stereo-linked,
feed-forward log-domain compressor.
*   `sidechain`: Config. Can be `internal` (uses audio inputs `in_l`/`in_r` as the detector level)
    or `external` (uses sidechain inputs `side_l`/`side_r`).
*   `threshold` (clamped `-120..0`, default -18): Target threshold in dBFS.
*   `ratio` (clamped `1..1000`, default 4): Compression ratio.
*   `knee` (clamped `0..40`, default 6): Soft-knee width in dB. `0` = hard knee.
*   `attack` / `release` (clamped `0.0001..10`, default 0.01 / 0.1): Time constants in seconds
    of the branching one-pole on gain reduction (dB). The detector target is
    peak-held for `min(release, 20 ms)` before release starts, so the gain does
    not ripple through the troughs of bass (a 50 Hz sine at 1 ms attack stays
    under 0.1 % THD) and steady tones settle at the static curve of their peak.
*   `makeup` (clamped `-40..40`, default 0): Output gain makeup in dB.
*   `mix` (clamped `0..1`, default 1): Linear dry/wet interpolation (no gain boost for identical signals).
*   `gain`: Output. Linear gain reduction factor (excluding makeup).
*   `gr`: Output. Current gain reduction in positive dB.

## `eq`

`eq` — inputs `in_l` 0, `in_r` 0, `hp_freq` 0, `low_freq` 120, `low_gain` 0,
`mid1_freq` 500, `mid1_gain` 0, `mid1_q` 0.707, `mid2_freq` 2500, `mid2_gain` 0,
`mid2_q` 0.707, `high_freq` 10000, `high_gain` 0; outputs `out_l`, `out_r`.
A stereo parametric mastering equalizer with 20 ms parameter smoothing; the
first sample starts at the patched settings, so there is no startup sweep.
Consists of five cascaded biquad sections per channel:
1.  **HPF**: `hp_freq: 0` bypasses it (the default). Positive values enable a second-order high-pass, clamped `5..0.45*rate`, Q fixed to `0.707`.
2.  **Low Shelf**: Low shelving filter. Frequency clamped `5..0.45*rate`, gain clamped `+/-24` dB. Slope fixed to `1.0`.
3.  **Mid1 Peaking**: Peaking/bell filter. Frequency clamped `5..0.45*rate`, gain clamped `+/-24` dB, Q clamped `0.1..18`.
4.  **Mid2 Peaking**: Peaking/bell filter. Frequency clamped `5..0.45*rate`, gain clamped `+/-24` dB, Q clamped `0.1..18`.
5.  **High Shelf**: High shelving filter. Frequency clamped `5..0.45*rate`, gain clamped `+/-24` dB. Slope fixed to `1.0`.

Zero-gain sections are optimized with exact bypass coefficients.

## `clipper`

`clipper { mode: hard }` — inputs `in_l` 0, `in_r` 0, `threshold` -1, `drive` 0,
`mix` 1; outputs `out_l`, `out_r`. An oversampled stereo mastering clipper.
*   `mode`: Config. Can be `hard` (hard digital clipping at threshold) or `soft`
    (normalized tanh soft clipping at threshold).
*   `threshold` (clamped `-120..0`, default -1): Output ceiling threshold in dBFS.
*   `drive` (clamped `-40..40`, default 0): Input gain boost applied before clipping in dB.
*   `mix` (clamped `0..1`, default 1): Linear dry/wet interpolation (no gain boost for identical signals).

Internally oversampled at 4x using a cascade of highly optimized 2-path polyphase allpass IIR half-band filters.
The dry branch is delay-aligned by exactly 1 sample to match the filter's peak latency.

## `limiter`

`limiter` — inputs `in_l` 0, `in_r` 0, `threshold` -0.1, `release` 0.05; outputs `out_l`, `out_r`, `gain`, `gr`.
A stereo-linked peak limiter with a fixed 2.0 ms lookahead.
*   `threshold` (clamped `-24..0`, default -0.1): Target ceiling threshold in dBFS.
*   `release` (clamped `0.001..2`, default 0.05): Release time in seconds.
*   `gain`: Output. Current linear gain reduction factor.
*   `gr`: Output. Current gain reduction in positive dB.

Guarantees a hard sample-peak ceiling at the threshold with a 2.0 ms lookahead
(96 samples at 48 kHz, which is also its latency). The gain reduction each
sample needs is peak-held over the lookahead (or `min(release, 20 ms)` if
longer, so bass does not ripple the gain), released with the `release` time
constant, then box-filtered over the whole lookahead: the gain ramps down
across the full 2 ms and arrives exactly as the peak does. A 50 Hz sine under
6 dB of reduction stays below 0.01 % THD. Sample peaks only; inter-sample
(true) peaks can still exceed the threshold slightly.

## `follow`

`follow { mode: peak }` — envelope follower / audio→CV bridge. Inputs `in` 0,
`attack` 0.01 s, `release` 0.1 s (both clamped `0.0001..10`), `thresh` 0.1
(`0..8`); outputs unipolar `env` and `gate`. It rectifies `in` (peak: `|in|`; rms: `in²`), smooths it with an
asymmetric one-pole attack/release detector, and emits the linear amplitude as
`env` (`≥ 0`, `0..1` for input within ±1, unclamped above — a hot signal reads
honestly). `gate` rises when `env` exceeds `thresh` and falls at `0.75 · thresh`
(fixed hysteresis, so it does not chatter near the threshold). Config `mode`:
`peak` (transient-reactive, for ducking off a kick) or `rms` (energy/loudness,
for following a sustained part). No sensitivity knob — scale a weak source at
the cable. Example — audio-derived sidechain duck:

```text
f = follow { mode: peak, attack: 5ms, release: 120ms }
kick -> f.in
pad  -> m.in2
f.env -> m.lvl2 : -0.4
```

## `sampler`

`sampler { file: "kick.wav", loop: off }` — WAV file playback. Inputs `trig` 0,
`pitch` 0 (v/oct, clamped ±24), linear `level` 1; outputs `out`, `eos`. Loads the file at graph
build (mixing any channel count to mono) and plays it back with linear
interpolation. Playback rate is `(file_rate / engine_rate) * 2^pitch`, so a file
recorded at any rate plays at correct pitch and `+1 v/oct` doubles it. A rising
edge on `trig` restarts from the beginning at full level while a voice still
sounding fades out over a fixed 5 ms (a second read head, no allocation), so a
retrigger does not click; with nothing patched to `trig` it plays once at load.
Config `loop`: `off` stops at the end and emits a one-sample `eos` pulse; `on`
wraps. Both seams — the loop wrap and the end of a one-shot — slew out their
step over 5 ms instead of cutting (the output stays continuous; timing and
`eos` are unchanged), so a file not cut on a loop point bumps rather than
clicks. A missing or unreadable file yields silence
(the session never dies). The `file:` string **must be quoted** and resolves
against the process working directory (mise runs from the repo root); absolute
paths always work. Reads PCM 16/24/32-bit and 32-bit float WAVs.

```text
s = sampler { file: "samples/loop.wav", loop: on }
clk -> s.trig
0.083333 -> s.pitch     # +1 semitone
s.out -> amp.in
```

## `rec`

`rec { file: "take.wav", secs: 30 }` — records a signal to a WAV file. Inputs
`in_l` 0, `in_r` 0, `gate` 1; no outputs (a sink, like `scope`). While `gate`
is high it captures the stereo input into a preallocated buffer (`secs` seconds,
clamped `1..600`), and writes a 32-bit float stereo WAV to `file` when the graph
is torn down (session end or hot reload). With `gate` unpatched it records
continuously until the cap. For an offline `render`, this drops one clean take
alongside the main mix. Editing the patch while recording restarts the take
(the buffer is not migrated across reload — the outgoing take is written, the
new one overwrites on teardown). The `file:` string must be quoted; paths are
working-directory-relative.

```text
r = rec { file: "take.wav", secs: 20 }
mix.out_l -> r.in_l
mix.out_r -> r.in_r
```

## `extin`

`extin` — external audio capture from PipeWire. No inputs; output `out` (mono,
±1). Live only: under `play` it captures from a PipeWire input port (wire it to
a source in your patchbay); under offline `render` there is no capture
transport, so it outputs silence (exactly like `midi`). Latency is about one
audio block plus a small internal buffer whose slack absorbs capture/playback
clock drift — underruns emit silence, overruns drop the oldest samples. Route it
like any source:

```text
ext = extin
ext.out -> filter.in
filter.lp -> out.left
```

## `grains`

`grains { secs: 4, seed: 1 }` — granular processor: the Clouds core (record/
freeze buffer + grain scheduler + 64-voice grain player) with the blend
section left to cables (`xfade`/`mix` for dry/wet, `delay` or a patched
feedback loop, `reverb`, `stereo`). Mono `in`, stereo out. Inputs: `in` 0,
`freeze` gate 0 (halts recording; grains keep reading; unfreeze crossfades
back over ~5 ms), rising-edge `trig` (seeds exactly one grain), `position`
0.5 (0 = newest audio / file start, 1 = oldest / file end — freezing never
re-maps it), `scatter` 0 (per-grain random position offset), `size` 0.1 s
(5 ms..2 s, capped at a quarter buffer), `pitch` 0 v/oct (±4 oct, plain
resampling — upward shifts alias, like the hardware), `density` 10 grains/s
(0..200; 0 = external `trig` only), `jitter` 0.5 (0 = periodic stream, 1 =
Poisson cloud; the mean rate stays `density`), `shape` 0.5 (window morph:
0 boxcar → 0.5 triangle → 1 Hann), `spread` 0.5 (random pan width; 0 =
center), `reverse` gate (latched per grain; plays the window backward).
Outputs `out_l`, `out_r`, and a one-sample `onset` pulse per seeded grain.
Configs: `secs` buffer length 0.25..60 s; `file` (quoted WAV path) preloads
the buffer, disables the record head, and makes `in`/`freeze` inert (missing
file = silence); `seed` makes every random draw repeatable. All grain
parameters latch at onset, so `sh`/`random` into `position`/`pitch` with a
clocked `trig` gives per-grain modulation compositionally. Extra seeds beyond
64 voices drop silently; output is loudness-normalized (`1/√N`). Determinism
means two instances fed the same input and freeze gate hold identical
buffers — multi-player-per-buffer patches for free.

```text
g = grains { secs: 4, seed: 7 }
src -> g.in
hold -> g.freeze
drift -> g.position : 0.3
g.out_l -> verb.in_l : 0.8
g.out_r -> verb.in_r : 0.8
```

## `modal`

`modal { modes: 64 }` — physical resonator: a parallel bank of up to 128
bandpass modes on a stretched-harmonic series (Rings' modal model). Inputs
`in` 0 (excitation audio through a brightness-tracked lowpass), `freq` 220 Hz,
`v_oct`, rising-edge `strike` (internal mallet: a filtered, energy-normalized
impulse), `structure` 0.25 (partial spacing: below 0.25 compressed/drum-like,
0.25..0.3 perfectly harmonic, then stretching toward bell/bar at 1),
`brightness` 0.5 (spectral tilt of decay and exciter color), `damping` 0.5
(decay time, exponential across ~4 decades of Q), `position` 0.25
(strike-position comb; 0.5 nulls the even partials, the fundamental always
sounds). Outputs `out` plus `odd`/`even` partial groups (`out = odd + even`).
Config `modes` 2..128, rounded to even — the voice-quality/CPU knob — or
`ratios: [1, 1.594, 2.136, ...]` (mutually exclusive with `modes`): explicit
mode placement at multiples of the fundamental, list order = mode index
(list the fundamental first; the position comb and odd/even split follow list
order). With `ratios` given, `structure` becomes a stretch/compress warp
`w = 2^((structure - 0.25) * 2)` on the listed ratios — exact as written at
the 0.25 default. Entries clamp to at most 64; an empty list, a non-positive
entry, or combining with `modes:` is a patch error. The drumkit voices in
`patches/lib/kit/` are built on this (membrane and metal ratio sets; see
docs/attribution.md). Bowed and blown textures come from `noise -> vca -> m.in` with slow `vca.cv`.
Like Rings, it reads `freq`/`v_oct`/`structure`/`brightness`/`damping` every 32
samples (`position` is ramped per sample; `strike` and `in` are sample-exact);
the fundamental clamps to a quarter of the sample rate (12 kHz at 48 kHz).

## `string`

`string { seed: 1 }` — extended Karplus-Strong string (Rings' string models).
Inputs `in` 0 (excitation audio, brightness-tracked lowpass + DC blocker),
`freq` 220 Hz, `v_oct`, rising-edge `strike` (seeded noise-burst pluck shaped
by position and brightness), `structure` 0.25 (bipolar dispersion: below 0.24
a curved-bridge nonlinearity — gongs, sitars; 0.24..0.26 pure string; above
0.26 stiff piano-like stretched partials; above ~0.9 adds rattle noise),
`brightness` 0.5 (loop FIR tone and exciter color), `damping` 0.5 (decay
0.07 s..~18 s, exponential; ≥ 0.95 crossfades to infinite sustain — a drone),
`position` 0.25 (pluck point and pickup comb). Outputs `out` (pickup-combed
voice, Rings' mix) and `aux` (raw pickup tap; `out + aux` is the bare loop).
Config `seed`: equal seeds repeat plucks exactly. Reaches ~5.9 Hz at 48 kHz
and clamps at a quarter of the sample rate. Like Rings, controls are read every
32 samples and the loop delay, pickup position, dispersion and phase
compensation are ramped per sample; `strike` and `in` are sample-exact.

## `overdrive`

`overdrive { model: tubescreamer }` — voiced guitar overdrive/distortion.
A pre-emphasis filter → anti-aliased waveshaper → tone stack, with a clean⇄
shaped blend. Inputs `in` 0 (mono), `drive` 0.5 (pre-gain into the curve, up to
~2^14× on `rat`; ranges run hot so the sustain keeps grinding, not just the
attack), `tone` 0.5 (sweeps the tone lowpass), `level` 0.5 (±2-octave
makeup), `mix` 1 (dry/wet; lower it for the transparent "clean blend" of the
`tubescreamer`/`klon` voicings — ~0.5). Output `out`. All four controls clamp
to `0..1` (a `drive: 2` reads as 1). They are read every 32 samples: `drive`
then ramps per sample and `mix`/`level` are smoothed over 5 ms, but `tone`
steps, so a fast audio-rate tone sweep is coarser than the other inputs. Config `model`:
`tubescreamer` (default, mid-hump soft clip), `klon` (transparent, more
headroom), `rat` (hard clip, huge gain, dark), `ds1` (mid-scooped), `mxr`
(bright soft clip), `muff` (two cascaded stages — fuzz), `clean` (flat-voiced
soft clipper). Anti-aliased by antiderivative anti-aliasing + 4× oversampling
(reuses the clipper's oversampler), so even the hard-clip `rat` stays clean.
Recommended `mix`: 0.5 for `tubescreamer`/`klon`, 1.0 for the rest. Cascade two
for extra saturation, or use `model: muff`.

## `fm`

`fm { algo: dual, ratios: [...], levels: [...] }` — a 6-operator
phase-modulation voice (classic operator-FM family, locally designed; see
`docs/attribution.md`). Six sine operators with per-operator 4-segment
time/level envelopes; routing from a named algorithm set or a custom edge
list. Mono; polyphony via `voice` groups.

Inputs: `gate` 0 (rising fires all envelopes and, with `sync: on`, resets
phases if the voice is silent; unpatched gate = silence), `freq` 220 Hz, `v_oct`, `vel` 1, per-op
`lvl1..lvl6` 0, `ratio1..ratio6` 0, `fbmod` 0 (adds to every op's `fb`, sum clamped 0..1;
named apart from the `fb` config array so the DSL can tell them apart).
Outputs: `out` (straight sum of the carriers — gain-stage at the cable) and
post-envelope per-op taps `op1..op6` (pan/process carriers separately, or
tap a modulator to drive another module's FM input).

Routing: an edge a→b adds a's output to b's phase; ops with no outgoing
edge are carriers. Named algorithms: `free` (no edges — additive sine
stack), `triad` (2→1, 4→3, 6→5), `dual` (3→2→1, 6→5→4), `cascade`
(6→5→4→3→2→1), `crown` (2..6→1), `weave` (6,5→4→2, 3→2, 2→1). `algo:
custom` reads `route` as flat pairs — `route: [2,1, 3,2]` = 2→1, 3→2.
Pairs must be integral 1..6, no self-edges, no duplicates, acyclic; a
malformed route fails `check`; hot reload keeps the last good graph.

Configs (all arrays are per-op, padded with the slot default; more than six
entries is an error): `ratios` (all 1), `levels` (`[1,0,0,0,0,0]` — ops 2..6 stay off
until asked), `fixed` (per-op Hz; >0 pins the op off `v_oct`/`ratio` for
clicks and body resonances), `fb` (0..1; `fb[k] > 0` is itself the
self-edge — never put self-edges in `route`. The op's phase is pushed by
2.5·fb rad times the average of its last two enveloped outputs, taken
before its level, so `fb` means the same timbre on any op whatever its
`levels`/`lvl`/`vel`. Measured on a held carrier: 0 is a sine, ~0.6 a saw-like
1/n spectrum, 0.8 the brightest clean saw (inharmonic floor below −40 dB), and
only past ~0.9 does it turn to noise), `vel_sens` (0..1; op level ×=
(1−sens)+sens·vel), `t1..t4` segment seconds (0 = one-sample jump),
`l1..l3` segment levels, `curve` (`lin`/`exp`/`log`/`quad`, default `lin`,
as in `adsr`; rising segments take the attack shape — `exp` or `log` is the
natural percussive fall), `sync` (`on`/`off`, default `on`: a gate
rise resets every operator phase only when each carrier's envelope × level
is below −60 dB, so notes from silence start identically while a retrigger
of a still-ringing voice carries its phases on rather than stepping — a
click; `off` never resets).
Envelope: gate rise runs 0→l1 over t1 → l2 over t2 → l3 over t3 and holds
l3; gate fall runs to 0 over t4, always from the current level. A level-1
modulator at envelope peak drives ±4π radians (index ≈ 12.6). `fm {}` bare
is a plain gated sine (op 1 only).

Operator waveform per op, `waves` (bareword array, default `sine`): `half`
(half-wave rectified), `abs` (full-wave rectified, octave-up), `quarter`
(bright windowed pulse), `fold` (triangle-wavefolded). All are DC-centered;
rich waves and high-index/high-frequency FM can alias. Ratio-mode operators
can also exceed Nyquist; use `additive` for a sine bank that removes out-of-band partials. An unknown name is a validation error — e.g. `waves: [sine, abs, fold, sine, abs, fold]`.

Keyboard scaling, latched from `v_oct` at the gate's rising edge (so a
mid-note pitch glide never warps level or envelope): `key_break` (scalar
`v_oct` where scaling is neutral, default 0), `key_level` (per-op level
octave-exponent, `level ×= 2^(key_level·(v_oct−break))` — negative thins
modulators up high), `key_rate` (per-op envelope-time octave-exponent,
`times ×= 2^(−key_rate·(v_oct−break))` — positive shortens decays up high).
All default 0 (no scaling).

```text
ep = fm { algo: dual, ratios: [1, 1, 7, 1, 14, 1], levels: [.5, .3, .1, .45, .1, .15],
          t2: [.5, .07, .06, .45, .06, .09], l2: [.55, .25, .2, .5, .2, .25],
          l3: [.18, 0, 0, .15, 0, 0], fb: [0, 0, 0, 0, .23, 0], curve: log }
mid = midi { voices: 4 }
keys = voice { notes: mid }
keys.ep = fm { ...same configs... }
```

## `chorus`

`chorus { voices: 3 }` — stereo multi-voice modulated-delay chorus. Several
short delay taps with decorrelated LFO phases (L and R a half-cycle apart)
spread across the stereo field and summed with the dry signal, for widening
and thickening. Feed a mono source to both inputs to fan it into stereo.

Inputs: `in_l`, `in_r`, `rate` .5 Hz (LFO speed, `0..100`), `depth` .5
(0..1, delay swing), `mix` .5 (linear dry↔wet; the wet sum is scaled by
`1/voices`). Outputs: `out_l`, `out_r`. Configs: `voices` (1..4, default 3),
`delay` (base delay ms, `1..40`, default 12), `swing` (max swing ms at depth 1,
`0..20`, default 6), `shape` (`tri`/`sine`, default `tri`). No feedback
(that is the flanger regime — see Recipes).

## `phaser`

`phaser { stages: 6 }` — mono phaser: a cascade of first-order allpass stages
whose break frequency is swept by an internal LFO, summed with the dry signal
so moving notches sweep the spectrum. Feedback sharpens the notches.

Inputs: `in`, `rate` .3 Hz (`0..100`), `depth` .6 (0..1, sweep width),
`feedback` .3 (0..0.95, resonance), `mix` .5 (linear dry↔wet; 0.5 is the
deepest notches). Output: `out`. The sweep is fixed to 100 Hz..4 kHz
(geometric centre ~630 Hz; `depth` narrows it around that centre); there is no
centre-frequency or external sweep input. Configs: `stages` (2..12, default 6 — more stages = more notches),
`spread` (0..1, default 0 — stage-frequency stagger: 0 keeps all stages
coincident for tight Phase-90-style notches, higher fans them out around the
swept centre for wider, more vocal notches), `shape` (`tri`/`sine`).

## `sub`

`k = sub { file: "lib/kick.sub", tune: G1 }` — instantiate a subpatch.
Exactly one of `file:` (quoted path, relative to the file containing the
`sub`) or `def:` (an in-file `subdef` name); every other key overrides a
`param` of that template (unknown keys are errors). Instance ports patch
like module ports (`clk -> k.trig`, `k.main -> mix.in1`; bare `k` reads the
first declared output). Stamped internals stay addressable
(`0.1 -> k.kosc.v_oct`) — the declared interface is the stable surface, not
a wall. Same file, different params: separate instances, one parse.
Nesting is fine (16 deep, cycles rejected, at most 65536 modules or cables
once expanded); `sub` composes with `voice`
groups in both directions. Saving an included `.sub` hot-reloads every
patch using it.

## `subdef`

`td = subdef {}` — mark the `td.*` declarations that follow as an inline
template: they never run themselves, and `t = sub { def: td, tune: A2 }`
stamps them. Inside the body, params and ports are referenced with the
prefix (`freq: td.tune`); cables may not cross the template boundary. The
`demo.*` convention does not apply to inline templates. A `subdef` body may
not reference another `subdef` by `def:` (templates resolve only at the level
they are declared) — nest with file-based `sub { file: ... }` instead.

## `param`

`tune = param { default: F1 }` — a subpatch parameter (config `default:`
required). Referenced by bare name in any config value, as a constant cable
source (`tune -> osc.v_oct`), or as a cable depth (`a -> b.in : tune`).
Instantiation overrides replace the default before the body compiles; a
`param` is a compile-time value, not a signal.

## `input`

`trig = input {}` — a subpatch input port (optional numeric `default:`,
read when no outside cable feeds the port — rule 4, nothing implicit
beyond the declaration). Inside the body it is a source
(`trig -> env.trig`); outside, a destination (`clk -> k.trig`). Multiple
outside cables sum; depths multiply through the splice.

## `output`

`main = output {}` — a subpatch output port. Inside, a destination
(`amp -> main.in`); outside, a source (`k.main -> mix.in1`). The first
declared output is the instance's default (`k -> mix.in1`). Declaring
`param`/`input`/`output` makes the file a subpatch: it cannot reference
`out` outside a `demo.*` section, and compiled directly it plays as an
implicit instantiation with defaults — `demo.*` declarations (with
`demo.out` as the device sink) run only then, so every `.sub` can audition
itself: `mise run play -- patches/lib/kick.sub`.

## Recipes

### Flanger (`delay` + `lfo`)

A flanger is a short modulated delay with feedback — cable it from `delay`:

```text
lfo = lfo { freq: 0.3hz }
fl  = delay { time: 0.003, feedback: 0.55, mix: 0.5, slew: 0.0005, tone: 12000 }
src -> fl.in
lfo -> fl.time : 0.0025    # swing ~±2.5 ms around a 3 ms base
fl  -> out.in : 0.5
```

Keep `slew` near 0, or the delay's time smoother smears the sweep. A single
positive-feedback delay line has no true through-zero and no hollow
negative-feedback flange; those would need a dedicated setup.

### Euclidean variation

```text
clk = clock { bpm: 240 }
e = euclid { steps: 16, hits: 5, rot: 3 }
coin = bern { prob: .8, seed: 4 }
clk -> e.clock
e.trig -> coin.gate
coin.a -> drum_env.trig
```

### Repeatable generative melody

```text
t = turing { len: 8, prob: .1, seed: 12 }
q = quant { scale: [0, 2, 3, 5, 7, 9, 10] }
clock -> t.clock
t.out -> q.in : .4
q -> osc.v_oct
```

### Sidechain ducking

```text
m = mix { levels: [.8, .6] }
kick -> m.in1
pad -> m.in2
kick_env -> m.lvl2 : -.3
```

### Sympathetic strings (Rings recipe)

Rings' sympathetic model is just coupled strings — string 0 feeds the others
at low gain, tuned to intervals, ringing longer:

```text
main = string { seed: 2 }
s1 = string { seed: 3, damping: 0.85 }
s2 = string { seed: 4, damping: 0.85 }
seq -> main.v_oct
seq -> s1.v_oct
0.58496 -> s1.v_oct          # +7.02 st: a just fifth, log2(3/2)
seq -> s2.v_oct
1 -> s2.v_oct                # +1 octave
gate -> main.strike
main.out -> s1.in : 0.1      # Rings couples at 0.2/n
main.out -> s2.in : 0.1
main.out -> m.in1
s1.out -> m.in2 : 0.6
s2.out -> m.in3 : 0.6
```

Detune shimmer: duplicate a string and add `0.001 -> dup.v_oct`.

### A drum voice as a subpatch

`patches/lib/kick.sub` packages the kick-solo recipe behind three params
and two ports:

```text
clk = clock { bpm: 120 }
k  = sub { file: "lib/kick.sub" }             # from patches/
k2 = sub { file: "lib/kick.sub", tune: G1, click: 0.5 }
clk -> k.trig
k -> out.in
```

### Guitar pedals from existing modules

Several classic pedals need no new module — they compose from what's here:

- **Tremolo:** `lfo -> vca.cv` (1–25 Hz `lfo`; depth = cable gain).
- **Ring mod:** `math { op: mult }` with an audio-rate carrier on the second
  input (`carrier -> rm.b`).
- **Auto-wah:** `follow -> svf.cutoff`, read `svf.bp` (an `lfo` instead of
  `follow` gives a fixed-rate wah). Q via `svf.res`.
- **Octave-up (Octavia):** `math { op: abs } -> dcblock` (a full-wave
  rectifier; note it aliases on bright input).
- **Big Muff fuzz:** `overdrive { model: muff }`, or cascade two `overdrive`
  stages with a filter between them.

### Physical drumkit (`lib/kit/*.sub`)

Eight kit voices (six physically modeled, one noise clap, one ring-modulated
hat) sharing one contract: `trig` fires, `accent`
(0..1, latched per hit) drives level and strike brightness, `tune`/`decay`
shape the body (`decay` is 0..1 ring on the modal voices, seconds on the
clap and the ring-mod hat). Two hat flavours: `hat.sub` (darker, acoustic-
leaning, driven modal bank) and `hat_model.sub` (brighter, TR-808/electronic,
ring-modulated square pairs under a noise wash). Closed/open hats are two
instances of one file, choke-coupled:

```text
chat = sub { file: "lib/kit/hat.sub", decay: 0.22 }
ohat = sub { file: "lib/kit/hat.sub", decay: 0.45 }
pat.trig -> chat.trig
open_pat.trig -> ohat.trig
pat.trig -> ohat.choke       # every closed hit chokes the open ring
bern.a -> chat.accent        # per-hit dynamics from any 0..1 source (cabling accent replaces the sub's 0.7 default — add a constant base cable to keep one)
```

Full groove: `patches/kit.patch`.

### An orchestra (`lib/orch/*.sub`) and a score

Seven orchestral voices share one contract: `voct` + `gate` say what is
played, `dyn` (0..1, the part's dynamic marking) and `vel` (a per-note accent,
typically `stepseq.vel`) say how loud, and level is `(dyn*vel)^2` with the
filter opening alongside it. `strings.sub` (bowed section, stereo out via a
chorus), `brass.sub` (horn by default; trumpet by params), `wind.sub` (flute
by default; oboe, clarinet, bassoon recipes in its header), and the struck
voices `timpani.sub`, `harp.sub` (pizzicato by params) and `bell.sub`
(glockenspiel) with the kit-style `trig` + `accent`; `hall.sub` is the room.

A score is `stepseq` note lists. Each theme lives in its own `.sub`
(`patches/symphony/themes/`) with `steps`/`lens`/`ties`/`vels` for the
melody and three harmony lanes with matching `lens`, so the accompaniment
re-voices from the same chords and `transpose` moves everything together;
`len`/`hlen` params loop the theme's head. The movement's FORM is one
`stepseq` whose `lens` are bar counts, clocked by the bar; its `trig`
clocks one-value-per-section role and dynamics lanes and resets every
theme. A part's `switch { mode: addr }` (three of them: pitch, gate, accent)
picks what it plays each section. `patches/symphony/1-allegro.patch` is the
worked example; its header holds the plan for the whole work.

### Song subpatches (`lib/master-chain.sub`, `lib/tape-echo.sub`, `lib/reese.sub`)

Three shared voices for song-length patches, extracted by copy from
`mainstage`, `antigravity-dub` and `nightbus` (which still carry their own
inline versions — the duplication is deliberate, not an oversight).

```text
room = sub { file: "lib/tape-echo.sub", verb_decay: 0.9 }
bpm_lane -> room.bpm          # echoes follow a clock that ramps
bass = sub { file: "lib/reese.sub", saw_cut: 300 }
mast = sub { file: "lib/master-chain.sub", ceiling: -0.4 }
kick_drive -> mast.side_l     # external sidechain: unpatched = no pump
mast.l -> out.left
mast.r -> out.right
```

`master-chain.sub` is the full `dcblock -> eq -> glue comp -> stereo -> clip
-> limiter` bus and owns the ceiling (never scale it again with `out.level`).
`tape-echo.sub` is the dub room — dotted-eighth and dotted-quarter echoes, a
phase sweep on the long return, and a plate fed by both; its `bpm` is an
input so delay times track a tempo ramp. `reese.sub` keeps the split-bass
discipline: the sine sub is untouched by every modulation the saws receive,
and `sub_out`/`saw_out` come out separately so they can be ducked at
different depths.
