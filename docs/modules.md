# Implemented Module Reference

This is the authoritative agent-facing reference for every module currently in
the runtime registry. It documents implemented behavior, not roadmap entries.

## Signal conventions

- Every connection carries a float at audio rate. Audio/bipolar CV is normally
  `-1..1`; unipolar CV is `0..1`; pitch is volts/octave.
- Gates are high above `0.5`. “Rising edge” means crossing from `<=0.5` to
  `>0.5`; a trigger is one sample, while a gate has duration.
- Values in `{ ... }` set input bases or compile-time configs. Cables sum onto
  input bases. Configs (waveform, mode, arrays, seed) cannot be modulated.
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

Suffixes include `hz`, `ms`, and `s`. Omitted output means the first output
(a bare `stepseq` name reads its `cv`, a bare `svf` its `lp`). Validate with
`modsynth check patch.patch`; `modsynth render ... --stats [Ns]` prints
overall peak/RMS/DC plus a per-window level timeline for mix work.

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

## Quick palette

| Area | Modules |
|---|---|
| Sources | `vco`, `lfo`, `noise`, `random`, `sampler`, `modal`, `string` |
| Tone/dynamics | `vca`, `adsr`, `func`, `lpg`, `svf`, `ladder`, `fold`, `drive` |
| Routing | `math`, `xfade`, `mix`, `logic`, `switch`, `sh`, `quant`, `follow` |
| Sequencing | `clock`, `div`, `euclid`, `bern`, `turing`, `stepseq`, `tempo`, `alloc`, `midi` |
| Effects/I/O | `clipper`, `compressor`, `dcblock`, `delay`, `eq`, `grains`, `limiter`, `reverb`, `scope`, `stereo`, `out` |

## `vca`

`vca { gain: 1, response: lin }` — inputs `in` 0, `cv` 0, `gain` 1;
output `out`. Gain is `clamp(gain + cv, 0, 8)`, squared for `response: exp`.
Typical use: `env -> amp.cv` with base gain zero.

## `out`

Inputs `in` 0 (both channels), `left` 0, `right` 0, `level` 1; engine outputs
`l`, `r`. It sums mono into each side and applies level. Example:
`mix.out_l -> out.left`, `mix.out_r -> out.right`.

## `vco`

`vco { freq: 440hz, wave: sine, pw: 0.5 }` — inputs `freq`, `v_oct`, relative
linear `fm`, `pw`, `pwm`, rising-edge `sync`; output `out`. Waves: `sine`,
`tri`, `saw`, `pulse`. `+1` v/oct doubles pitch; frequency is band-limited and
clamped below Nyquist. A pulse at `pw != 0.5` carries a DC offset of about
`2 * pw - 1` that low-pass filters (`svf.lp`, `ladder`) pass straight through;
keep `pw` at `0.5` or block DC downstream if the path is DC-coupled.

## `lfo`

`lfo { freq: 1hz, wave: sine, phase: 0 }` — inputs `freq`, rising-edge `reset`,
phase offset; bipolar output `out`. Waves: `sine`, `tri`, `saw`, `square`.
Example: `slow -> filter.cutoff : 300`.

## `clock`

`clock { bpm: 120, run: 1 }` — inputs `bpm`, `run`, rising-edge `reset`;
outputs 50%-duty `out` and normalized `run`. Stop forces clock low but holds
phase. Use BPM 240 for eighth-note gates at a musical 120 BPM.

## `div`

`div { by: 2 }` — inputs `in`, rounded positive `by`, rising-edge `reset`;
output `out`. Produces an approximately 50%-duty divided gate. Coincident reset
and input edge counts as step zero.

## `func`

`func { rise: 0.01, fall: 0.1, curve: lin, cycle: off }` — inputs rising-edge
`trig`, slew `in`, rise/fall seconds; outputs unipolar `out`, `eor` gate, and
one-sample `eoc`. Curves: `lin`, `exp`, `log`; cycle: `off`/`on`. With nothing
patched to `trig`, `out` slews toward `in` at the rise/fall rates — this is
the intended way to smooth stepped CV (section fades, portamento).

## `svf`

`svf { cutoff: 1000, res: 0 }` — inputs `in`, cutoff Hz, resonance `0..1`;
outputs `lp`, `bp`, `hp`. Cutoff clamps to `5..0.45 * sample_rate`.

## `noise`

`noise { color: white, seed: 1 }` — no inputs; bipolar `out`. Colors: `white`
or `pink`. Equal seeds produce repeatable streams.

## `math`

`math { op: add }` — inputs `a` 0, `b` 1; output `out`. Ops: `add`, `sub`,
`mult`, `min`, `max`, `abs`, `clip`, `wrap`. `abs` ignores b; clip/wrap use
`abs(b)` as the symmetric range. `mult` is ring modulation.

## `xfade`

Inputs `a` 0, `b` 0, `mix` 0.5 (`0..1`); output `out`. Equal-power rather
than linear, so both inputs are -3 dB at midpoint.

## `drive`

`drive { amount: 0.5, mode: soft }` — inputs `in`, amount `0..1`; output
`out`. The pre-gain is `1 + 9 * amount`, so `soft` computes
`tanh(in * (1 + 9 * amount))` and `hard` clamps the same product to `-1..1`.
Note even `amount: 0.1` is a ~2x boost into the curve; on a full mix that is
a limiter, not glue.

## `fold`

Inputs `in` 0, non-negative `fold` 1, symmetry offset `sym` 0; output `out`
folded into `-1..1`. Best paired with a triangle oscillator and modulated fold.

## `sh`

`sh { mode: sample }` — inputs `in`, `trig`; output held `out`. `sample`
captures on trig rising edges; `track` follows while trig is high and holds
while low.

## `ladder`

Inputs `in` 0, `cutoff` 1000 Hz, `res` 0 (`0..1`), `drive` 0 (`0..4`);
four-pole low-pass output `out`. Example: `env -> filter.cutoff : 1800`.

## `lpg`

Inputs `in`, continuous `cv`, rising-edge `strike`, `damp` 0.5, `decay` 0.25 s;
output `out`. Strike excites a vactrol-like amplitude/brightness decay; CV can
hold it open.

## `random`

`random { rate: 1hz, smooth: 1, seed: 1 }` — internally clocked bipolar
generator. Outputs cosine-interpolated `out` and raw `stepped`; smooth clamps
to `0..1`.

## `quant`

`quant { scale: [0,2,3,5,7,9,10], root: 0, transpose: 0 }` — inputs `in`
v/oct, root semitones, transpose v/oct; outputs quantized `out` and one-sample
`changed`. Empty scale is chromatic; ties snap down.

## `mix`

Eight inputs `in1..in8` and additive level CVs `lvl1..lvl8`; configs `levels`
(missing = 1) and `pans` (`-1` left, `0` center, `1` right; missing = center).
Outputs `out_l`, `out_r`. Negative level CV is useful for sidechain ducking.

## `scope`

`scope { label: name }` — input `in`, no patch output. Captures last/peak state
for watch and future TUI tooling without altering the signal path.

## `delay`

Inputs `in` 0, `time` 0.4 s (`0..10`), `pitch` 0 v/oct, `feedback` 0.35,
equal-power `mix` 0.5, feedback `tone` 8000 Hz, `slew` 0.1 s; output `out`.
Effective time is `time * 2^-pitch`. The dark, DC-blocked, saturated feedback
can self-oscillate above 1. Example: `{ time: 375ms, feedback: .65, mix: 1 }`.

## `adsr`

Inputs `gate`, `attack` .005 s, `decay` .15 s, `sustain` .7, `release` .3 s;
unipolar `out`. Config curve: `lin`, `exp`, `log`. Retriggers and releases from
the current level rather than jumping.

## `reverb`

Stereo Dattorro plate. Inputs `in_l`, `in_r`, `decay` .5, `damp` .25, `mix`
.3, `freeze` gate; outputs `out_l`, `out_r`. Configs: `predelay` .02 s,
`bandwidth` .9995, `mod_rate` 1 Hz, `mod_depth` 13 samples. Freeze smoothly
mutes input and makes the tank lossless while held.

## `euclid`

Inputs `clock`, rising-edge `reset`, `steps` 16 (`1..64`), `hits` 4
(`0..steps`), wrapped `rot` 0. Outputs one-sample `trig` and clock-width `gate`.
Example: `euclid { steps: 16, hits: 5, rot: 3 }`.

## `bern`

`bern { prob: .5, seed: 1 }` — input `gate`; mutually exclusive gate-width
outputs `a`, `b`. Each rising edge routes to a with probability `prob`, else b.
Use after Euclid for repeatable variations.

## `logic`

`logic { op: and, threshold: .5 }` — inputs `a`, `b`, threshold; normalized
gate `out`. Ops `and`, `or`, `xor`, `not`, `cmp`; Boolean ops threshold inputs,
not negates a, and cmp tests `a > b`.

## `switch`

Eight signal inputs, plus `clock`, rising-edge `reset`, zero-based `addr`, and
`n` 8 (`1..8`); output `out`. Config mode `seq` advances on clock; `addr`
rounds/clamps the live address. Example: `switch { mode: seq, n: 4 }`.

## `turing`

`turing { len: 8, prob: .1, seed: 1 }` — inputs `clock`, `write`, `clear`,
length `1..16`, mutation probability. Outputs normalized low-byte `out`,
one-sample incoming-bit `pulse`, and current `bit1..bit8` gates. Prob 0 locks a
loop; write forces a high incoming bit; clear zeros the whole register.

## `stepseq`

Configs `steps` CV array (default `[0]`), `gates` array (missing = on), `dir`
(`forward`, `reverse`, `pingpong`, `random`), `seed`. Inputs `clock`, reset,
`len` (0 = full array), transpose. Outputs held `cv`, one-sample `trig`,
clock-width `gate`, and forward/reverse wrap `eoc`.

```text
s = stepseq { steps: [0, .25, .58333], gates: [1, 0, 1] }
clock -> s.clock
s.cv -> osc.v_oct
s.trig -> env.trig
```

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
cutoff frequency (clamped `1..40` Hz, smoothed around 20 ms). Protects speakers
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
*   `attack` / `release` (clamped `0.0001..10`, default 0.01 / 0.1): Time constants in seconds.
*   `makeup` (clamped `-40..40`, default 0): Output gain makeup in dB.
*   `mix` (clamped `0..1`, default 1): Equal-power dry/wet crossfade mix.
*   `gain`: Output. Linear gain reduction factor (excluding makeup).
*   `gr`: Output. Current gain reduction in positive dB.

## `eq`

`eq` — inputs `in_l` 0, `in_r` 0, `hp_freq` 20, `low_freq` 120, `low_gain` 0,
`mid1_freq` 500, `mid1_gain` 0, `mid1_q` 0.707, `mid2_freq` 2500, `mid2_gain` 0,
`mid2_q` 0.707, `high_freq` 10000, `high_gain` 0; outputs `out_l`, `out_r`.
A stereo parametric mastering equalizer with 20 ms parameter smoothing.
Consists of five cascaded biquad sections per channel:
1.  **HPF**: Second-order high-pass filter. Frequency clamped `5..0.45*rate`, Q fixed to `0.707`.
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
*   `mix` (clamped `0..1`, default 1): Equal-power dry/wet crossfade mix.

Internally oversampled at 4x using a cascade of highly optimized 2-path polyphase allpass IIR half-band filters.
The dry branch is delay-aligned by exactly 1 sample to match the filter's peak latency.

## `limiter`

`limiter` — inputs `in_l` 0, `in_r` 0, `threshold` -0.1, `release` 0.05; outputs `out_l`, `out_r`, `gain`, `gr`.
A stereo-linked peak limiter with a fixed 2.0 ms lookahead.
*   `threshold` (clamped `-24..0`, default -0.1): Target ceiling threshold in dBFS.
*   `release` (clamped `0.001..2`, default 0.05): Release time in seconds.
*   `gain`: Output. Current linear gain reduction factor.
*   `gr`: Output. Current gain reduction in positive dB.

Guarantees a true hard ceiling at the target threshold by looking ahead 2.0 ms (96 samples at 48 kHz) to detect peak amplitude, applying a fast 0.1 ms attack curve before the peak arrives.

## `follow`

`follow { mode: peak }` — envelope follower / audio→CV bridge. Inputs `in` 0,
`attack` 0.01 s, `release` 0.1 s, `thresh` 0.1; outputs unipolar `env` and
`gate`. It rectifies `in` (peak: `|in|`; rms: `in²`), smooths it with an
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
`pitch` 0 (v/oct), `level` 1; outputs `out`, `eos`. Loads the file at graph
build (mixing any channel count to mono) and plays it back with linear
interpolation. Playback rate is `(file_rate / engine_rate) * 2^pitch`, so a file
recorded at any rate plays at correct pitch and `+1 v/oct` doubles it. A rising
edge on `trig` restarts from the beginning; with nothing patched to `trig` it
plays once at load. Config `loop`: `off` stops at the end and emits a one-sample
`eos` pulse; `on` wraps seamlessly. A missing or unreadable file yields silence
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
Config `modes` 2..128, rounded to even — the voice-quality/CPU knob. Bowed
and blown textures come from `noise -> vca -> m.in` with slow `vca.cv`.

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
Config `seed`: equal seeds repeat plucks exactly. Reaches ~5.9 Hz at 48 kHz.

## Recipes

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
