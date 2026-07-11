# Timing and Function Modules Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add phase-locked clock division and click-free function envelopes, then use them in the progressive-house patch.

**Architecture:** Each module is a self-contained `dsp::Module` with fixed input/output metadata and migratable scalar state. A fast master clock drives a hierarchy of dividers; triggered function generators replace discontinuous gain ramps.

**Tech Stack:** C++20, doctest, modsynth patch DSL, CMake

---

### Task 1: Specify modules with failing tests

**Files:**
- Create: `tests/test_clock.cpp`
- Create: `tests/test_div.cpp`
- Create: `tests/test_func.cpp`
- Modify: `tests/test_migrate.cpp`

Write tests for exact clock periods, run/reset, integer division, divider reset,
function timing, completion pulses, curve shape, cycling, slew, and state
migration. Build and confirm failure because the module types are unregistered.

### Task 2: Implement modules

**Files:**
- Create: `src/dsp/modules/clock.cpp`
- Create: `src/dsp/modules/div.cpp`
- Create: `src/dsp/modules/func.cpp`
- Modify: `src/dsp/modules.h`
- Modify: `src/dsp/registry.cpp`

Implement the approved semantics without allocation, locks, or I/O in
`process()`. Register all module metadata and run the complete unit suite.

### Task 3: Integrate the active patch

**Files:**
- Modify: `patches/dark-ambient.patch`

Replace independent rhythmic LFO clocks with a phase-locked clock/divider tree
and replace transient saw envelopes with `func`. Validate the patch and listen
through hot reload.

### Task 4: Verify

Run the normal tests, sanitizer tests, warnings-as-errors build, patch checker,
and live PipeWire player. Confirm stable output and zero xruns.
