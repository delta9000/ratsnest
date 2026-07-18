# FM waveforms + keyboard scaling: `fm` v2

Date: 2026-07-18. Status: approved direction. Two timbral additions to the `fm`
module — per-operator waveforms and DX-style keyboard scaling — plus one small,
general DSL enabler (bareword arrays). Builds directly on
`docs/superpowers/specs/2026-07-17-fm-design.md`; nothing here changes the
phase-modulation core, routing, feedback, or envelope model.

## 1. Goal

Close the two honest gaps in the sine-only `fm` core:

- **Operator waveforms** — the single biggest timbral gap. Rectified/windowed/
  folded sine operators multiply an operator's harmonic content cheaply, and as
  *modulators* they transform the whole downstream spectrum. This is the
  TX81Z/DX11/FS1R "operator waveform" idea, in a small curated set.
- **Keyboard scaling** — make existing timbres behave correctly across the
  range: modulators thin out and envelopes shorten up high (convincing EPs and
  bells), stay fat and long down low. Realism, not novelty, but essential for
  any bright patch to work across more than one octave.

Both are opt-in and default to no-op: an `fm` patch written before this change
produces **bit-identical** output after it.

Non-goals (unchanged from v1): non-fixed operator counts, per-op panning,
internal pitch EG / LFO (cable them), wavetable operators, the full DX7
breakpoint-with-two-curves-and-two-depths scaling model.

## 2. Component A — DSL bareword arrays (shared enabler)

Per-op waveform selection wants to read `waves: [sine, abs, sine]` in a patch.
The lexer, parser, and AST **already** handle this: `Value::value()` parses each
array element with the same recursive `value()` that accepts `Ident`, so
`[sine, abs]` already lowers to `Value{Array, items:[Ident,Ident]}`. The only
gap is in `graph::to_config_value`, which reads `item.num` for every array
element — so an ident array silently collapses to `[0, 0, …]`.

Change (additive, backward compatible — **no lexer/parser/AST edits**):

- `dsp::ConfigValue` gains `std::vector<std::string> strs;` alongside `arr`.
- `graph::to_config_value` lowers `Array` items **by kind**: `Num` → `arr`
  (exactly as today), `Ident`/`Str` → `strs`. A numeric array is unchanged; an
  ident array fills `strs` and leaves `arr` empty. (Mixed arrays fill both
  partially; modules read the one field they expect — documented as
  "arrays are homogeneous".)
- New reader in `dsp/module.{h,cpp}`, mirroring `cfg_arr`:
  `const std::vector<std::string>& cfg_str_arr(const Config&, string_view);`
  Returns a static empty vector when absent.

No DSL-level enum validation of array elements: consistent with the module's
existing stance that config arrays are opaque to `check` (routing is validated
at `init()`), individual modules validate their own array configs. This keeps
the shared change pure data-plumbing. The general capability (bareword arrays)
also opens a future path to per-op `curve`/`algo`; out of scope here.

## 3. Component B — Operator waveforms (`fm.cpp` only)

Config `waves` — a per-op bareword array, padded with `sine`, extras ignored
(the array-config convention). Values map at `init()`:
`sine`=0, `half`=1, `abs`=2, `quarter`=3, `fold`=4. **Any unknown name → `sine`**
(rule-6 silent fallback, documented — same spirit as a malformed `route` → `free`;
no `check` error). `waves` absent → all `sine` → bit-identical to v1.

Each operator's output shaping function replaces the hardwired sine. Let `A` be
the operator's existing total phase argument in radians
(`A = 2π·phase + kIndex·mod`) and `θ = frac(A / 2π) ∈ [0,1)`. Every wave is
**DC-centered** (its one-period mean subtracted) so rectified shapes never dump
DC onto a carrier output or add a constant phase offset when used as a
modulator — the subtracted term is inaudible; the harmonic character is intact.

| `waves` | shape (raw, before centering) | centered by | character |
|---|---|---|---|
| `sine` | `sin(2πθ)` | 0 | baseline (unchanged) |
| `half` | `sin(2πθ)` for θ<0.5, else 0 | −1/π | half-wave rectified; full harmonic series, hollow |
| `abs` | `\|sin(2πθ)\|` | −2/π | full-wave rectified; octave-up, strong evens |
| `quarter` | `sin(4πθ)` for θ<0.25, else 0 | −1/2π | short bright pulse; nasal/edgy, harmonic-dense |
| `fold` | triangle-wavefold of `2·sin(2πθ)` | 0 (odd-symmetric) | buzzy, aggressive; extra odd content |

`fold`'s folder is the reflective triangle transfer `foldtri(x) = (2/π)·asin(sin(πx/2))`
applied to a fixed drive of 2× (peaks reflect back through zero); odd-symmetric,
so its mean is already 0. It reuses the same reflect-and-fold idea as the
existing `fold` module; the exact drive constant (2.0) is fixed here.

**Aliasing:** these are harmonically rich, so as high-frequency carriers or
modulators they alias — kept **raw**, no bandlimiting, consistent with the v1
stance ("the carriers are exact functions; FM aliasing is part of the sound").
`fold` aliases hardest; that is its purpose.

Waveform is discrete and per-op, so it is a config only (no live CV input) —
same treatment as `curve`.

## 4. Component C — Keyboard scaling (`fm.cpp` only)

Three configs, all defaulting to the neutral value (→ no-op):

| Config | Kind | Default | Meaning |
|---|---|---|---|
| `key_break` | num scalar | 0 | the `v_oct` value at which scaling is neutral |
| `key_level` | num array ×6 | all 0 | per-op level octave-exponent |
| `key_rate` | num array ×6 | all 0 | per-op envelope-time octave-exponent |

**Latched at the gate rising edge** (DX-style): on the rising edge the module
captures `Δ = clampn(v_oct, −24, 24) − key_break` (same NaN-proof clamp as the
pitch path) and computes, per op `k`, held for the whole note:

```
level_scale[k] = clampd(exp2(key_level[k] · Δ),      0, 16)     # multiplies op level
time_scale[k]  = clampd(exp2(−key_rate[k]  · Δ),  1e-3, 1e3)    # multiplies env segment seconds
```

- Level: after summing base + `lvl` CV + velocity, the op level is multiplied by
  `level_scale[k]`, then clamped 0..1 as today. `key_level` < 0 thins modulators
  up high (the usual EP/bell move); > 0 brightens up high.
- Rate: each envelope segment's seconds are multiplied by `time_scale[k]` before
  `env_increment`. `key_rate` > 0 shortens (speeds) segments up high; the
  clamp keeps a runaway exponent from freezing or zeroing the envelope.

Latching (not continuous tracking) means a mid-note `v_oct` glide bends pitch
without warping the envelope or jumping the level — matching hardware and
avoiding per-sample scaling artifacts. Before the first note, both scales are 1
(neutral). `Δ = 0` (playing at `key_break`) yields scales of exactly 1.

## 5. DSP core changes (integrated per-sample sketch)

Only two lines of the v1 per-op loop change; everything else is identical.

```
# at gate rising edge (once):
Δ = clampn(v_oct, -24, 24) - key_break
for k: level_scale[k] = clampd(exp2(key_level[k]*Δ), 0, 16)
       time_scale[k]  = clampd(exp2(-key_rate[k]*Δ), 1e-3, 1e3)

# per op k, per sample:
L_k   = clamp(levels_k + lvlCV_k, 0, 1) · vel_term_k · level_scale[k]      # (+ scaling)
env_tick(env_k, times = env_cfg_k.t · time_scale[k], ...)                  # (+ scaling)
A     = 2π·phase_k + kIndex·mod_k
θ     = frac(A / 2π)
s_k   = wave[waves_k](θ)                                                    # (was sin(A))
post_k = env_k · L_k · s_k
```

`time_scale` multiplies the seconds handed to `env_increment` — no change to the
double-accumulation envelope contract. `wave[...]` is a branch on the per-op
waveform code (a tiny switch), pure and allocation-free.

## 6. Runtime integration & robustness

- **Rule 3**: wave functions are pure; scaling is computed at the gate edge
  inside `process()` (no allocation, no I/O). `init()` still owns all allocation
  (the `waves`/`key_*` arrays).
- **Hot reload** (`copy_state_from`): the new migratable state is
  `level_scale[6]`, `time_scale[6]` (the latched per-note scaling), copied
  alongside the v1 state (phases, envelopes, `prev_post`, `prev_gate`). Bounded
  copies, no allocation. A config edit rebuilds a fresh instance whose runtime
  state carries, so a waveform or scaling change mid-note bends rather than
  restarts.
- **Hostile inputs** (rule 6): `v_oct` NaN-clamps before the latch; unknown
  waveform names resolve to `sine`; both scales are clamped to finite bounds, so
  no config or CV can freeze or blow up the envelope. DC-centering keeps carrier
  outputs bounded and DC-free.
- **Backward compatibility**: `waves` absent and all `key_*` at default reproduce
  v1 output bit-for-bit (dedicated tests below).

## 7. Provenance, licensing & clean-room posture (CLAUDE.md rules)

Extends the existing **concept-inspired** `fm.cpp` provenance (Chowning 1973);
adds no dependency and copies no third-party code or data. What keeps this
MIT-clean is that everything added is either non-copyrightable mathematics or
original expression, written clean-room:

- **Operator waveforms are mathematics, not expression.** A half-/full-wave
  rectified or windowed sine is a mathematical function; copyright covers a
  particular *expression* of it (code, or a ROM wavetable), not the function.
  All five waves are written locally from their formulas (§3). Hard constraint
  for the plan: use **no Yamaha waveform ROM/tables, parameter data, or code**,
  and **no code, tables, or curve data lifted from GPL/other-licensed FM
  emulators or reverse-engineered ROM projects** (e.g. Dexed / MSFA and
  similar) — pulling from a GPL source would both contaminate this MIT project
  and copy protected expression. The curated five-wave set, the DC-centering,
  and the `fold` self-wavefold are original.
- **No trademarks in the product.** Config values are generic and descriptive
  (`sine`/`half`/`abs`/`quarter`/`fold`); no Yamaha model name or mark appears
  as a feature name or in any user-facing output. Model names (TX81Z, DX11,
  FS1R, DX7) appear only as factual prior-art references in this spec and
  `docs/attribution.md` — nominative reference (consistent with the v1 entry),
  and the honest record of what we consciously did *not* copy. Vague attribution
  would be worse, not safer.
- **Keyboard scaling** is a long-standing, general technique (level/rate vs.
  keyboard position, across many manufacturers). The octave-exponent form and
  its parameters are original; **no DX7 breakpoint/scaling tables or curve data
  are used**.
- **Patents (not a legal conclusion).** The underlying phase-modulation patent
  (Chowning, US 4,018,121) expired in the mid-1990s and grounds the v1 entry.
  The operator-waveform and keyboard-scaling techniques originate in 1980s–1990s
  hardware, so any implementation patents are *believed* long expired — but per
  the project rule ("never invent provenance; mark unresolved until verified"),
  this is recorded as **unverified** and left for human/legal confirmation
  before release if there is any doubt. This spec makes no patent claim about
  the new features.

Attribution work in the same diff: one line in `fm.cpp`'s header comment and an
expanded clause in the `docs/attribution.md` row (concept-inspired; math
implemented locally; no third-party code, tables, or trademarks used).
`THIRD_PARTY_NOTICES.md` is unchanged (nothing is vendored). `test_docs.cpp`
asserts presence, so its checks keep passing.

## 8. Docs in the same diff

- `docs/modules.md` `## fm`: document `waves` (the five names + the code
  mapping for readers who prefer it, and the sine-fallback), `key_break`,
  `key_level`, `key_rate`, and note the latch-at-gate-rise semantics.
- `docs/attribution.md`: the expanded provenance clause (§7).
- Two demo patches under `patches/fm-tests/` (not committed CI fixtures — the
  glob is `patches/*.patch`): one leaning on waveforms (e.g. `abs`/`fold`
  brightening a `dual` EP), one demonstrating keyboard scaling across a wide
  `v_oct` sweep.

## 9. Testing (doctest)

Direct module tests (`tests/test_fm.cpp`) plus a small DSL test.

- **DSL (Component A):** an ident array lowers to `strs` (not zeros); a numeric
  array still lowers to `arr` unchanged; `cfg_str_arr` returns the names;
  absent config → empty. (In `tests/test_graph.cpp` or `test_dsl.cpp`, wherever
  config lowering is covered.)
- **Waveforms:** each of the five, driven as a lone carrier at a low ratio,
  shows the expected spectrum via the existing `dft_mag` helper — `abs` puts
  energy at 2× the fundamental, `half` fills the odd+even series, `quarter`/
  `fold` are harmonically denser than `sine`; every wave's measured DC (bin 0 /
  mean) is ≈ 0 after centering. Unknown name (`waves: [wobble]`) → identical to
  `sine`. `waves` absent → bit-identical to a v1 render.
- **Keyboard scaling:** with `key_level < 0`, a modulator tap's level at
  `v_oct = +1` is `2^(key_level)` × its level at `v_oct = 0` (measured at the
  latched value); with `key_rate > 0`, the envelope segment length at
  `v_oct = +1` is halved for `key_rate = 1`; `key_break` shifts the neutral
  point; latch means a `v_oct` change *after* the gate rise does not alter the
  scaling that note; all `key_*` default → bit-identical to v1.
- **Migration:** `copy_state_from` mid-note (with non-default waves + scaling)
  continues sample-identically.
- **Full gates:** `mise run test`, `mise run test-san`, `mise run ci` green,
  including `test_docs.cpp` and the automatic patch-check ctests.

## 10. Done criteria

- `waves` selects any of five DC-centered operator waveforms per op, bareword
  in the patch, unknown → `sine`.
- `key_break`/`key_level`/`key_rate` scale per-op level and envelope rate against
  latched `v_oct`, defaulting to no-op.
- Bareword arrays lower correctly and leave every existing numeric-array patch
  unchanged.
- All defaults reproduce v1 output bit-for-bit; all three gates green; docs,
  attribution, and demo patches in the diff.
