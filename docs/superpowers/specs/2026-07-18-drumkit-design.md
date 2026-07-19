# Physical-modeling drumkit — Design

2026-07-18. Status: designed. This is the spec promised by
`2026-07-18-subpatch-design.md` ("the drumkit ships as includable subpatch
files"); subpatches (`sub`/`param`/`input`/`output`) and the resonators
(`2026-07-14-resonator-design.md`) are its prerequisites, both landed.

## 1. Goal and decisions

A seven-voice **physical electronic kit**: unmistakably physically-modeled
(membrane bodies, coupled snare wires, ratio-cluster metal) but tuned as an
electronic kit for the techno/kosmische patches — playable and mixable first,
realistic second.

Decisions fixed during design:

- Voices: kick, snare, closed hat, open hat, tom, rim, clap (six `.sub`
  files — one hat file covers both hats).
- Ships as a **new family alongside** `lib/kick.sub`/`lib/hat.sub`, at
  `patches/lib/kit/*.sub`. Existing patches are untouched.
- Every voice exposes an **accent** input (per-hit dynamics, latched at trig).
- One small DSP addition: an optional **`ratios:` config on `modal`** (§2).
  Everything else is patch-only. Rejected alternatives: pure-patch kit on
  stretched harmonics (hats/rim read as filtered bells — the stretched-
  harmonic walk cannot produce inharmonic ratio clusters), and a dedicated
  Plaits-style C++ drum voice (preset box; stays on the main spec's out-of-
  scope list).

## 2. `modal { ratios: [...] }`

Optional config: a list of positive floats, each a multiple of the effective
fundamental `f0` (freq/v_oct-derived as today).

- **Placement**: mode *i* sits at `f0 * ratios[i] ^ w` (warp `w` below).
  Mode count = list length (1..128 entries). List order defines mode index:
  the `position` comb weights and the `odd`/`even` output split follow list
  order exactly as they follow the harmonic walk today (index 0 always gets
  comb weight 1 — list the fundamental first; ascending order recommended,
  not required). Modes above 0.49 normalized still drop.
- **Mutually exclusive with `modes:`** — specifying both is a compile error at
  the declaration's line:col (session keeps the last good graph, rule 6).
- **Validation is graph-compile-time**: an empty list or any non-numeric, NaN, or
  ≤ 0 entry is a compile error reported at the module declaration's line:col
  (`ModuleInfo::validate`, called by the graph compiler); entries above 64 clamp to 64.
  Config is text, so hostility is a compile-time concern, never an RT one.
- **`structure` reinterprets as a warp** on the listed ratios:
  `w = 2^((structure - 0.25) * 2)`, recomputed at block rate like all mode
  placement. At the default `structure = 0.25`, `w = 1` — exactly the listed
  ratios, mirroring how 0.25 already means "neutral" in both resonator
  modules. Below 0.25 the ratios compress toward the fundamental (head
  tightening); above they stretch bell-ward. Still a modulatable input with
  the existing 0..0.9995 clamp.
- **Unchanged**: `brightness`, `damping`, `position`, exciter path, per-mode
  Q laws, and outputs all operate on mode index and mode frequency, not on
  how the frequency was chosen — they compose with ratio placement untouched.
- **Hot reload**: editing the `ratios:` list migrates the overlapping mode
  prefix, same rule as a `modes:` change.

Ratio sets used by the kit (constants in the `.sub` files, not baked into
C++):

- Membrane (circular-membrane Bessel modes): `[1, 1.594, 2.136, 2.296,
  2.653, 2.918]` — Fletcher & Rossing, *The Physics of Musical Instruments*;
  paper-derived entry in `docs/attribution.md`.
- Metal cluster (TR-808 hat oscillator bank, normalized ~205.3/304.4/369.6/
  522.7/540/800 Hz): `[1, 1.483, 1.800, 2.546, 2.630, 3.897]` —
  concept-inspired entry.

## 3. Shared voice interface

Every kit voice is a `.sub` with the same contract:

- **Inputs**: `trig` (rising edge fires the voice); `accent`
  (`input { default: 0.7 }`, 0..1). An internal `sh` clocked by `trig`
  latches accent at the hit, so slow LFOs / `bern` / sequencer CV give
  per-hit dynamics with no zipper. The latched value drives:
  - level: `amp = 0.5 + 0.5 * accent` (accent 0 is a half-level ghost note,
    not silence);
  - excitation brightness: `(accent - 0.7) * 0.3` summed onto the voice's
    `modal.brightness` base — harder hits ring brighter (voices may tune the
    depth).
  Unpatched, accent reads 0.7 and a bare trig gives a consistent hit.
- **Params**: `tune` and `decay` on every voice, plus at most one or two
  voice-specific extras (`bend`, `snap`, `tone`). On modal voices `decay` is
  **0..1 ring amount mapped onto `modal.damping`** (physical bodies decay
  per-mode; a seconds-valued VCA chop would fight the model). The clap — the
  one non-modal voice — takes seconds like the existing lib subs. Documented
  per voice.
- **Outputs**: mono `main`, gain-staged so a full-accent hit peaks ≈ ±0.8
  (lib-sub convention: kit voices mix at unity without surprises). `hat.sub`
  additionally takes a `choke` input (§4).
- **Audition**: each file ends with the standard `demo.*` block —
  `mise run play -- patches/lib/kit/<voice>.sub` works standalone.

Accent costs ~3 modules per voice against the ~10-module budget, and means no
voice grows new inputs later for velocity.

## 4. Voices

**`kit/kick.sub`** — `modal` on membrane ratios, `tune: 48hz`, fired via
`strike` (the energy-normalized internal mallet is the beater click). Fast
`func` into `v_oct` for the pitch collapse; `bend` param sets depth, default
~2.5 oct (electronic thump, dialable toward acoustic). ~9 modules.

**`kit/tom.sub`** — same topology at `tune: 140hz`: longer ring, `bend`
default ~0.3 oct (skin-tension bloop), strike `position` further off-center.
Multi-tom kits are multiple instances at different `tune`s.

**`kit/snare.sub`** — two coupled layers. Body: membrane `modal` at
`tune: 190hz`, short ring. Wires: white noise → `svf.hp` ≈ 5 kHz → VCA whose
CV is `follow` of the body output — buzz driven by how hard the head is still
vibrating (the coupling that makes snares read as snares). A separate ~10 ms
noise burst is the attack snap. `snap` balances wires+snap against body.
~12 modules, the biggest voice.

**`kit/rim.sub`** — sparse 3-mode bank, ratios ≈ `[1, 2.66, 5.4]`,
`tune: 480hz`, very low damping base, hard bright strike through a fast VCA
crack. The woody clonk.

**`kit/hat.sub`** — one file, both hats. `modal` on the metal cluster at
`tune: 2.6khz` (bank spans ≈ 2.6–10 kHz), excited by a short noise burst
through `in` (noise through the bank is the sizzle; a bare impulse rings too
cleanly). Post `svf.hp` with a `tone` param. `choke` input: rising edge fires
a fast cut envelope multiplied into the amp CV. Closed vs. open is
patch-side: two instances with different `decay`, and `chat.trig` also cabled
to `ohat.choke`.

**`kit/clap.sub`** — trig through two `delay` taps (≈ 11 and 23 ms); original
plus taps retrigger one fast `func` (the three-burst flam), summed with a
longer tail envelope, driving bandpassed noise ≈ 1.1 kHz. Not physical
modeling; the roster needs it.

**Demo**: `patches/kit.patch` — full-kit groove (~126 BPM), euclid patterns
whose periods divide the bar, `bern`-driven accents, hat choke wired.

## 5. Testing

C++ (doctests beside `modal`'s existing tests):

- Goertzel confirms a mode at `ratio * f0` for a listed ratio.
- `structure: 0.25` with `ratios:` is bit-identical to placement at the
  listed ratios (warp neutrality); warp is monotonic either side.
- `modes:` + `ratios:` together → parse error with position; empty/NaN/≤ 0
  entries → parse errors.
- Hot reload across a `ratios:` edit migrates the overlapping prefix.
- Suite passes under `mise run test-san`.

Patch-level:

- `cli_check` test per `.sub` and for `kit.patch` (pattern:
  `cli_check_kick_sub`).
- Registry-coverage test forces the `docs/modules.md` `modal` entry update.

Audition verification (render `kit.patch`):

- Loop locks — pattern periods divide the bar (autocorrelation at bar lags).
- Per-voice spectra match roles: kick centered sub-100 Hz, snare body
  ≈ 190 Hz with buzz above 5 kHz, hats above 2.5 kHz.
- No clipping at full accent; choke audibly cuts `ohat` when `chat` fires.

## 6. Docs in the same diffs

- `docs/modules.md`: `ratios:` in the `modal` entry + a kit recipe under
  Recipes.
- `docs/attribution.md`: Fletcher & Rossing membrane ratios (paper-derived);
  808 hat cluster (concept-inspired).
- `2026-07-14-resonator-design.md` gains a pointer to §2;
  `2026-07-18-subpatch-design.md`'s "spec to follow" note points here.
