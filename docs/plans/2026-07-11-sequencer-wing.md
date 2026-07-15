# Sequencer Wing Utilities Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add Bernoulli routing, gate logic, signal switching, Turing generation, step sequencing, and tempo conversion.

**Architecture:** Each module is a fixed-port audio-rate DSP unit with defensive live-input clamping. Clocked modules use rising-edge state machines, seeded local xorshift RNG where needed, and complete RT-safe state migration.

**Tech Stack:** C++20, doctest, ratsnest patch DSL, CMake/mise

---

### Task 1: Failing behavior tests

Create `tests/test_sequencers.cpp` covering registration and the exact APIs in
the design, deterministic representative behavior, reset, bounds, trigger/gate
width, and finite tempo outputs. Build and confirm failure on unregistered
modules.

### Task 2: Stateless and routing utilities

Create `src/dsp/modules/bern.cpp`, `logic.cpp`, `switch.cpp`, and `tempo.cpp`.
Append declarations and registry entries. Run focused tests until green.

### Task 3: Generators

Create `src/dsp/modules/turing.cpp` and `stepseq.cpp`. Use fixed scalar/array
state only in `process`; config arrays may allocate in `init`. Run focused tests.

### Task 4: Migration, spec, and demos

Add load-bearing migration tests to `tests/test_migrate.cpp`, expand Phase 3 in
the authoritative spec, and integrate at least Bernoulli accents and Turing CV
into `patches/dub-techno.patch`. Check and render the patch.

### Task 5: Verification

Run focused tests, full tests, sanitizer tests, warnings-as-errors CI,
`git diff --check`, and a finite/peak/RMS render check. Preserve the unrelated
user edit in `patches/pluck.patch`.
