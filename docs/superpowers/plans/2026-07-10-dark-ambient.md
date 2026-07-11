# Dark Ambient Patch Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Create and play a slowly evolving, self-running dark ambient patch.

**Architecture:** Compose the implemented oscillators, LFOs, and VCAs into a low drone with independent slow modulation. Route voices across the stereo output at conservative levels.

**Tech Stack:** modsynth patch DSL, C++20 `modsynth` validator/player

---

### Task 1: Create and validate the patch

**Files:**
- Create: `patches/dark-ambient.patch`

**Step 1:** Write the layered drone patch using only Phase 1 modules.

**Step 2:** Run `mise run check -- patches/dark-ambient.patch` and expect a successful validation.

**Step 3:** Run `mise run play -- patches/dark-ambient.patch` and confirm the live engine starts.
