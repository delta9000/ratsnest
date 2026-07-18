# Modulation effects: `chorus`, `phaser` (+ `flanger` recipe)

Date: 2026-07-18. Status: approved direction. Two self-contained modulation-
effect modules — a stereo `chorus` and a mono `phaser` — plus a `flanger`
documented as a `delay`+`lfo` recipe rather than a module. These fill the
modulation-effects gap in the palette (the family of short-modulated-delay and
swept-allpass effects) alongside the existing `delay`/`reverb`.

## 1. Goal and shape

The palette has time-domain effects (`delay`, `reverb`) but none of the
modulation effects. Of the three classic ones:

- **`phaser`** is not composable from the current palette at all — allpass
  filters live only inside `reverb`/`string`, never exposed. A swept allpass
  cascade with feedback is genuinely new DSP → a module.
- **`chorus`**: single-voice is roughly `lfo → delay.time`, but the lush
  multi-voice form (several decorrelated delay lines, stereo-spread) is a
  patch-maintenance burden that justifies one self-contained module (the
  `reverb`/`fm` precedent).
- **`flanger`**: `delay` already exposes `time`, `feedback`, `mix`, `slew`, so a
  short modulated delay with feedback *is* a flanger. It becomes a documented
  **recipe**, not a module (rule 5 — don't build what cables cover).

Both modules are self-contained (internal LFO, no new dependency — rule 7),
follow the live-CV-input convention (`rate`/`depth`/`mix`/`feedback` are
patchable ports with config-free musical defaults), and obey the signal
conventions (§4): audio ±1, unipolar CV 0..1, gates n/a.

Non-goals: tempo-sync (cable a `clock`/`tempo`-derived rate); stereo phaser
(mono chosen — a quadrature-LFO stereo variant is a future option); true
through-zero flanging (needs a second/negative delay line the `delay` module
doesn't provide); tape-style wow/flutter.

## 2. `chorus` — stereo multi-voice

Stereo in/out, mirroring `reverb`: lushness comes from decorrelated voices
spread across the stereo field, so mono→stereo widening is the common use
(feed the same signal to `in_l` and `in_r`).

| Input | Default | Range | Meaning |
|---|---|---|---|
| `in_l` | 0 | audio | left input |
| `in_r` | 0 | audio | right input |
| `rate` | 0.5 | ≥0 Hz | LFO rate (sum with config base) |
| `depth` | 0.5 | 0..1 | modulation depth (delay-time swing) |
| `mix` | 0.5 | 0..1 | dry↔wet |

| Config | Kind | Default | Meaning |
|---|---|---|---|
| `voices` | int | 3 | number of modulated taps, 1..4 |
| `delay` | num | 12 | base delay per voice, ms (2..40) |
| `swing` | num | 6 | max delay swing at depth 1, ms |
| `shape` | enum | `tri` | LFO shape `tri`/`sine` |

Output: `out_l`, `out_r`.

**DSP** (per sample): one delay buffer per input channel sized for
`(delay+swing)` ms (allocated in `init()`). `voices` internal LFOs share the
rate but are phase-offset by `i / voices` of a cycle. Voice `i` reads a
linear-interpolated tap at `delay_ms + depth·swing·lfo_i` from a channel
buffer; voices are panned alternately/spread across L/R for decorrelation.
Per channel: `out = (1−mix)·dry + mix·Σ(panned wet taps)`. No feedback (that is
the flanger regime) — so there is no accumulating state to flush, only the
bounded delay reads. `rate`, `depth`, `mix` are NaN-clamped; `voices`/`delay`/
`swing` clamp at `init()`.

## 3. `phaser` — mono swept allpass cascade

| Input | Default | Range | Meaning |
|---|---|---|---|
| `in` | 0 | audio | input |
| `rate` | 0.3 | ≥0 Hz | LFO rate |
| `depth` | 0.6 | 0..1 | sweep width |
| `feedback` | 0.3 | 0..0.95 | resonance around the cascade |
| `mix` | 0.5 | 0..1 | dry↔wet (0.5 = deepest notches) |

| Config | Kind | Default | Meaning |
|---|---|---|---|
| `stages` | int | 6 | number of first-order allpass stages, 2..12 |
| `spread` | num | 0 | stage-frequency stagger 0..1 (0 = all stages coincident, Phase-90-style tight notches; 1 = stages offset across the sweep band for wider, more vocal notches) |
| `shape` | enum | `tri` | LFO shape `tri`/`sine` |

Output: `out`.

**DSP** (per sample): `stages` first-order allpass filters in series. An internal
LFO produces `m ∈ [−1,1]`; the swept centre frequency is exponential,
`fc = fmin·(fmax/fmin)^((m·depth+1)/2)` over a fixed band (fmin≈100 Hz,
fmax≈4 kHz, clamped ≤0.45·rate). With `spread > 0`, stage `k` is offset to
`fc·2^(spread·(k − (stages−1)/2)/stages)` so the stages fan out around the swept
centre — this is the "phase offset" between stages that shapes the notch
spacing. Each stage is the one-multiply first-order allpass
`y = a·x + s ; s = x − a·y` with `a = (tan(π·fc_k/rate) − 1)/(tan(π·fc_k/rate) + 1)`.
Feedback: the last stage's previous output re-enters the input scaled by
`feedback` (bounded <1, so the recursion is stable). Output blends dry with the
allpass-chain (wet): `out = (1−mix)·dry + mix·wet`; the notches arise from the
dry+wet sum. `rate`/`depth`/`feedback`/`mix` NaN-clamped; `spread`/`stages`
clamp at `init()`. State to migrate on hot reload: the per-stage allpass state
`s_k`, the feedback sample, and the LFO phase.

## 4. `flanger` — recipe (docs, not a module)

Documented in `docs/modules.md` Recipes:

```text
# flanger — short modulated delay with feedback.
lfo = lfo { freq: 0.3hz }
fl  = delay { time: 0.003, feedback: 0.55, mix: 0.5, slew: 0.0005, tone: 12000 }
src -> fl.in
lfo -> fl.time : 0.0025    # swing ~±2.5 ms around a 3 ms base
fl  -> out.in : 0.5
```

Honest limits to note in the recipe text: set `slew` very low (≈0) or the
delay-time smoother smears the sweep; the classic hollow "negative-feedback"
flange and true through-zero are not available from `delay` (single positive-
feedback line) — that is what a dedicated module would add later if wanted.

## 5. Runtime integration & robustness

- **Rule 3**: buffers/LFO state allocated in `init()`; `process()` and
  `copy_state_from()` use fixed storage, no locks/I-O.
- **Signal conventions** (rule 8): audio ±1 in/out (wet sums can exceed ±1 —
  gain-stage at the cable, `mix` precedent); `rate` Hz; `depth`/`mix` unipolar;
  `feedback` 0..<1.
- **Denormals / stability**: `chorus` has no recursive feedback. `phaser`'s
  feedback is bounded <1 and its allpass states are flushed to 0 in `init()`;
  a tiny denormal guard on the feedback sample if needed.
- **Hostile inputs** (rule 6): NaN-proof clamps on every CV; `stages`/`voices`
  clamp to their ranges; `fc` clamps below Nyquist. No config or CV can blow the
  module up.

## 6. Provenance (CLAUDE.md rules)

Both are standard, decades-old DSP with no third-party code copied:

- `chorus`: multi-voice modulated-delay chorus is textbook DSP; implemented
  locally. Concept-generic (no single source); **concept-inspired**/none.
- `phaser`: a swept first-order-allpass cascade; the allpass coefficient is the
  standard bilinear/TPT first-order allpass already used in the project's filter
  lineage (Zavalishin / Bristow-Johnson, already cited for `svf`/`eq`). No new
  source or dependency; implemented locally.
- Patents: chorus/phaser/flanger are 1970s-era effects; any patents are long
  expired. This is a factual note, not a legal opinion (cf. the `fm` patent-
  status note). No trademarks; generic descriptive names.

`docs/attribution.md` gains a brief `chorus`/`phaser` note (concept-generic /
standard-allpass, implemented locally, no third-party code). No changes to
`THIRD_PARTY_NOTICES.md`.

## 7. Docs in the same diff

- `docs/modules.md`: `## chorus` and `## phaser` sections; the `flanger` recipe
  in the Recipes section; both modules added to the Effects/I-O palette row.
- `docs/attribution.md`: the §6 note.
- `patches/`: a demo patch exercising both (e.g. an `fm`/`vco` source →
  `phaser` → `chorus` → `out`), doubling as the automatic `cli_check` fixture.

## 8. Testing (doctest, per file — `tests/test_chorus.cpp`, `tests/test_phaser.cpp`)

- Registry/ports: both registered with the declared surface; `test_docs`
  `## chorus`/`## phaser` headings.
- `chorus`: dry-through at `mix 0` (out == in per channel); wet adds delayed,
  pitch-modulated copies (a static input yields a time-varying output at `mix
  1`); L and R differ (decorrelation); `voices`/`delay`/`swing` clamp; bounded
  and finite under hostile CV.
- `phaser`: at `mix 0`, out == in; a swept notch — a broadband/step input shows
  a moving spectral null (DFT dip that moves with the LFO); `stages` changes the
  notch count; `spread` changes notch spacing (coincident vs staggered);
  `feedback` sharpens (higher peak around the notch) and stays bounded at 0.95;
  allpass magnitude ≈ flat before the dry+wet sum (each stage is unity-gain).
- Migration: `copy_state_from` continues sample-identically for both.
- Hostile inputs: NaN on every CV → finite output.
- Integration: a `chorus`/`phaser` patch parses, compiles, renders non-silent
  and bounded through `engine::Engine`.
- `mise run test`, `mise run test-san`, `mise run ci` all pass.

## 9. Done criteria

- `chorus` (stereo, multi-voice) and `phaser` (mono, swept allpass with `stages`
  and `spread`) registered, tested, documented.
- `flanger` recipe in `docs/modules.md` with its honest limits.
- Demo patch renders non-silent and bounded; all three gates green; docs and
  attribution in the diff.
