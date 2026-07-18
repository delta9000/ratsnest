# FM test bench

A wide sweep across the `fm` module's surface — every named algorithm, custom
routing, feedback, fixed ops, envelope curves, and the live `vel`/`ratio`/`fbmod`
inputs. Audition one with:

    mise run play -- patches/fm-tests/03-dual-ep.patch     # live + hot-reload
    mise run render -- patches/fm-tests/03-dual-ep.patch -d 20s -o take.wav

Each patch is a self-contained voice-group (or held drone). Edit + save to
hot-reload; the header comment in each file says what it exercises.

| # | Patch | Exercises | Character |
|---|-------|-----------|-----------|
| 01 | `free-additive` | `algo: free`, 6 carriers, `out` sum | drawbar organ / harmonic stack |
| 02 | `triad-pairs` | `algo: triad`, 3 C:M pairs | chorused two-op stack |
| 03 | `dual-ep` | `algo: dual`, feedback tine, `vel_sens` + live `vel`, `curve: log` | Rhodes electric piano |
| 04 | `cascade-bright` | `algo: cascade` (deep stack), `curve: exp` | aggressive bright lead |
| 05 | `crown-bells` | `algo: crown`, inharmonic ratios, long tails | struck-metal bells |
| 06 | `weave-evolve` | `algo: weave`, feedback + routing, staggered envelopes | evolving pad |
| 07 | `bass-fm` | `custom` route, low register, short envelopes | punchy FM bass |
| 08 | `brass-swell` | envelope-opened index (slow modulator attacks) | brass swell |
| 09 | `metallic-perc` | `fb` array on 2 ops, extreme inharmonicity, percussive env | gongs / cymbals |
| 10 | `feedback-sweep` | live `fbmod` input swept by LFO | sine → saw → noise (drone) |
| 11 | `ratio-morph` | live `ratio2` input swept by LFO | morphing inharmonic spectrum (drone) |
| 12 | `fixed-click` | `fixed[]` pitch-pinned op, custom route | metallic attack transient |
| 13 | `custom-exotic` | `custom` route, two converging chains, Kahn topo order | dense two-path timbre |
| 14 | `sync-off-drift` | `sync: off` vs `on` (repeated strikes) | phase-drift transients |
| 15 | `fallback-free` | malformed cyclic `route` → graceful `free` fallback | proves rule 6 (never dies) |

## Suggested tweak points (hot-reload while playing)

- **Algorithm:** swap `algo:` between `free`/`triad`/`dual`/`cascade`/`crown`/`weave`.
- **Brightness:** raise a modulator's `levels[]` entry (drives the FM index up).
- **Inharmonicity:** nudge `ratios[]` off integers (e.g. `1.41`, `3.14`).
- **Feedback:** raise a `fb[]` entry (0..0.999) — small changes matter near the top.
- **Decay:** lengthen `t3`/`t4`; use `curve: log` for a natural percussive fall.
- **14/15:** flip `sync: off`→`on` (14) or fix the `route` (15) to hear the contrast.
