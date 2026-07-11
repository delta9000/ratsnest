This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# modsynth

A software modular synthesizer for Linux where **the patch is a plain-text file**.
Hand-written C++20 DSP, PipeWire out, hot-reload on save. Personal instrument;
single author. Non-goals: DAW, plugin host, livecoding language, marketplace.

Authoritative design: `docs/superpowers/specs/2026-07-10-modsynth-design.md`.
Implementation plans: `docs/superpowers/plans/` (checkbox tasks = status; a task
is done only when its named tests pass — never hand-edit status).

## Build / test

Prefer mise tasks (see `mise tasks`):

    mise run build        # cmake --preset dev && cmake --build build
    mise run test         # ctest --test-dir build --output-on-failure
    mise run test-san     # same tests under ASan+UBSan (build-san/)
    mise run ci           # warnings-as-errors gate (build-ci/)
    mise run render -- patches/first.patch -d 10s -o take.wav
    mise run play -- patches/first.patch          # live PipeWire + hot-reload

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
7. External deps: PipeWire and doctest. Adding a third is a design decision,
   not a convenience.
8. Signal conventions (spec §4): audio/bipolar ±1, unipolar 0..1, v_oct in
   octaves, gates rise through 0.5. Every port respects them.

## Commit conventions

Conventional prefixes (feat/fix/test/chore/docs). Tool-agnostic messages —
no AI attribution, no session trailers. Docs are part of the diff: update
spec/CLAUDE.md in the same commit as behavior they describe.
