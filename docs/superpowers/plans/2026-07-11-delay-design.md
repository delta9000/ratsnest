# delay — design

Phase 2's `delay`: a mono feedback delay with tape-style time behavior.
Research basis: repo-constraint audit (2026-07-11) + DSP survey (JOS,
Signalsmith, Niemitalo, Valhalla, VCV Fundamental). Decisions confirmed by Ben:
10 s max buffer, v_oct pitch modulation of time, feedback allowed to 1.2.

## Ports

| Port | Kind | Default | Meaning |
|---|---|---|---|
| `in` | input | 0 | audio in, ±1 |
| `time` | input | 0.4 | base delay in seconds (`ms` sugar works); clamped 0..10 |
| `pitch` | input | 0 | v_oct scaling: effective time = time · 2^(−pitch). +1 = halve (octave up for combs) |
| `feedback` | input | 0.35 | 0..1.2; >1 runs away into tanh-saturated self-oscillation |
| `mix` | input | 0.5 | equal-power dry/wet like `xfade` |
| `tone` | input | 8000 | one-pole lowpass cutoff (Hz) in the feedback path; each repeat darkens |
| `slew` | input | 0.1 | time-smoothing τ in seconds (tape Doppler). ~0 tracks instantly for Karplus-style pitch play |
| `out` | output | — | mixed signal |

No config. Stereo is two instances (rule 5: compose with cables).

## Internals

- **Ring buffer**: `std::vector<float>` sized to the next power of two ≥
  10 s · rate + 4, allocated in `init()` (control thread — allocation legal
  there), indexed with a mask. 48 kHz → 2^19 samples ≈ 2 MB.
- **Read**: 4-point cubic Hermite (Catmull-Rom) at the fractional delay.
  Linear dulls highs under modulation (−3 dB at fs/4 at f=0.5); allpass smears
  on time jumps; Hermite is the stateless quality/cost sweet spot
  (Niemitalo/Signalsmith). Effective delay clamped to [2, size−4] samples.
- **Time slew**: one-pole toward the target delay-in-samples,
  `a = 1 − exp(−1/(slew·rate))`, snap on the very first sample (no 10 s glide
  from zero at startup). Modulating `time`/`pitch` bends pitch tape-style —
  an LFO into `time` is chorus/flange for free.
- **Feedback loop** (order): wet tap → tone lowpass → DC blocker
  (`y = x − x₁ + R·y₁`, fc ≈ 10 Hz) → `tanh(feedback · x)` → sum with `in` →
  write. tanh bounds runaway at feedback > 1 into musical saturation; the DC
  blocker stops rectified/asymmetric material from accumulating offset.
- **Denormals**: the offline render path and tests run without FTZ/DAZ (only
  the PipeWire thread sets it), so the three feedback-filter states (`lp_`, `dc_x_`, `dc_y_`) flush
  themselves to zero below 1e−20 — identical behavior in `render` and `play`.
- **Migration**: `copy_state_from` copies write index, slewed delay, filter
  states, and the buffer contents (`std::copy`, min of the two sizes —
  defensive, no alloc). ~2 MB ≈ 130 µs memcpy on the RT thread, ~2.5% of even
  a 256-frame callback; spec §3.3 explicitly promises delay buffers survive
  hot-reload. Echo tails ride through a patch edit. Copy cost scales with the number of delay instances (~130 µs each); fine at patch scale, revisit if a patch ever carries dozens.

## Non-goals (deferred, deliberately)

- **Clock sync**: needs no delay feature — a future clock-period-to-seconds
  module patched into `time` gives sync compositionally (Chronoblob/DLD prior
  art). Spec already says "clock-sync later".
- Crossfade-on-jump digital mode, multi-tap, ping-pong: compose or later.

## Also in this plan

`cmd_render` (and therefore every offline path) now enables FTZ/DAZ like the
PipeWire thread does — decaying filter tails (svf, ladder, lpg, delay) hit
denormals otherwise and render slower than realtime playback of the same
patch. One line, benefits every module.
