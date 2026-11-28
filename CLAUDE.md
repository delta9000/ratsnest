This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# ratsnest

A software modular synthesizer for Linux where **the patch is a plain-text file**.
Hand-written C++20 DSP, PipeWire out, hot-reload on save. Personal instrument;
single author. Non-goals: DAW, plugin GUIs or plugin state, livecoding language,
marketplace. (CLAP plugins load headless as the `plugin` module; docs/plugin.md.)
The GUI (node editor) is shelved; current focus is correctness and performance
of the DSP core (spec §10).

Authoritative design: `docs/superpowers/specs/2026-07-10-ratsnest-design.md`.
Implemented module palette and patch syntax: `docs/modules.md` (read this before
authoring or reviewing patches; its registry-coverage test prevents omissions).
Implementation plans: `docs/superpowers/plans/` (checkbox tasks = status; a task
is done only when its named tests pass — never hand-edit status).

## Making music with it

`.claude/skills/` is a project-scoped skill library for authoring patches.
Invoke `ratsnest-patching` for any patch work; it routes to the rest:

| Skill | For |
|---|---|
| `ratsnest-patching` | the authoring loop, signal conventions, the gotchas that break patches |
| `ratsnest-synthesis` | voices: subtractive, west coast, FM, physical, granular, percussion, bass, noise, space |
| `ratsnest-generative` | self-playing patches: incommensurate periods, locked-vs-fresh randomness, drift |
| `ratsnest-genres` | twelve genre cards, each a departure point with a "how to break it" section |
| `ratsnest-composition` | form, harmony, mixing, mastering — with the calibration numbers |
| `ratsnest-inspiration` | provocations, artist lenses, seed sketches, starting from nothing |
| `ratsnest-instrument` | playing it: MIDI, polyphony, live hot-reload, capture, writing subpatches |
| `ratsnest-surge` | Surge XT through `plugin`: set lists, finding params, tracker p-locks, its quirks, when native is better |
| `ratsnest-debugging` | silent, clipping, out of tune, or not what the header claims — the solo-probe procedure |

Worked, verified examples live in `patches/etudes/`. The evidence behind the
skills is in `docs/research/2026-07-20-*.md`. `docs/patch-craft.md` remains the
authoritative list of hard-won gotchas; the skills cite it rather than
duplicating it.

Validate every patch in the repo with `mise run check-all`.

## Build / test

Prefer mise tasks (see `mise tasks`):

    mise run build        # cmake --preset dev && cmake --build build
    mise run test         # ctest --test-dir build --output-on-failure
    mise run test-san     # same tests under ASan+UBSan (build-san/)
    mise run ci           # warnings-as-errors gate (build-ci/)
    mise run render -- patches/first.patch -d 10s -o take.wav
    mise run play -- patches/first.patch          # live PipeWire + hot-reload
    mise run complexity   # score for src/; fails past docs/complexity.json
    mise run baseline     # rewrite tests/data/render-baseline.txt (deliberate sound change)
    mise run syx -- bank.syx -o patches/dx/bank   # 6-op SysEx voices -> .sub (docs/fm-syx.md)
    ./build/ratsnest plugins [file]   # installed CLAP plugins, ids, params
    mise run plugin-install   # ratsnest.clap -> ~/.clap; patches in ~/.config/ratsnest/plugins (docs/plugin.md)

## Render baseline

The `render_baseline` test renders every native patch (8 s, 48 kHz) and
compares levels and brightness per second against
`tests/data/render-baseline.txt`, to 0.2 dB. A change that moves any patch's
sound fails it. If the move is intended, run `mise run baseline` and commit
the rewritten file in the same commit, saying in the message which patches
moved and why. A new patch joins the baseline in the commit that adds it.

## Complexity budget

`tools/complexity.py` scores `src/` as decisions + 10×hotspots + loc/50 +
5×modules (hotspot: cyclomatic > 15 or body > 80 lines). Every change should
hold the score or lower it. A change may grow it up to 2% past
`docs/complexity.json`; past that, simplify, or raise the baseline with
`--update` in the same commit and say why in the message.

## Non-negotiable design rules

Violating these is a design bug, not a style preference.

1. The `.patch` file is the source of truth. Any future GUI/TUI round-trips
   through it. Canvas layout lives in a sidecar, never in the patch.
2. Everything is one signal: floats at audio rate. No control rate, no
   separate event type.
3. `Module::process()` never allocates, locks, or does I/O. Anything reachable
   from the audio thread follows the same rule. The only RT↔non-RT bridges are
   the SPSC queue, ring buffers, and the graph pointer swap (Phase 1b).
4. No normalling. Modulation sums onto declared param bases. Unpatched inputs
   read their default. Nothing implicit.
5. Modules are small and single-purpose; compose with cables.
6. Errors never kill the session: bad patch → report line:col, keep the last
   good graph playing.
7. External deps: PipeWire, doctest, and the header-only CLAP ABI (the
   `plugin` module and `ratsnest.clap`; docs/plugin.md). Adding another is a
   design decision, not a convenience.
8. Signal conventions (spec §4): audio/bipolar ±1, unipolar 0..1, v_oct in
   octaves, gates rise through 0.5. Every port respects them.

## Commit conventions

Conventional prefixes (feat/fix/test/chore/docs). Tool-agnostic messages —
no AI attribution, no session trailers. Docs are part of the diff: update
spec/CLAUDE.md in the same commit as behavior they describe.

## Attribution and licensing

This project is a synthesis of others' work; provenance is part of the code.
Before adapting a distinctive algorithm or implementation:

1. Record its primary source and license. Do not copy code whose license is
   absent, unclear, or incompatible with this MIT-licensed project.
2. Preserve every required copyright, permission, and disclaimer in
   `THIRD_PARTY_NOTICES.md` and describe material adaptations.
3. Add a concise comment at the implementation site and a full entry in
   `docs/attribution.md`, classified as code-derived, paper-derived,
   concept-inspired, or an external dependency.
4. When dependencies or borrowed implementations change, update their version,
   mapping, and notice in the same commit.

Never invent provenance. Mark an origin unresolved until a primary source can
be verified.
