#pragma once
// Shared assertions for "bank producer" modules — modules that spread a
// note stream across indexed output families gate<i>/voct<i>/vel<i> for
// i = 1..8 (docs/superpowers/specs/2026-07-11-voice-alloc-design.md §2).
// `alloc` is the first producer; `midi` (task 8) reuses this verbatim, so
// nothing here may depend on alloc-specific behavior (mode, rr claim
// order, etc.) — only the bank convention itself.
//
// Seam: the CALLER owns the input buffer. Build `ins` sized to
// `info.inputs.size()` (pre-populate any inputs the module always reads,
// e.g. a `vel` default), and hand `check_bank_conformance` a `FeedFn` that
// captures `ins` **by reference** and mutates it in place for a given
// step; the harness calls `feed(step)` and then
// `module.process(ins.data(), outs.data())` immediately after, once per
// sample, and records the resulting bank outputs. This keeps the harness
// ignorant of what a "step" means for any given producer (alloc drives a
// mono gate; midi will inject note-on/off events) while still letting it
// drive `process()` itself.
//
// Properties asserted, for every step over the run:
//   1. Every gate<i> output name for i = 1..8 exists in `info.outputs`
//      (bank slots exist regardless of `n`).
//   2. gate<i> is always exactly 0.0f or 1.0f (never an intermediate
//      value) — "gates rise through 0.5" per spec §4 signal conventions.
//   3. Hold rule: once gate<i> drops to 0 (a release), voct<i> does not
//      change again until the next rising edge (reclaim) of gate<i>.
//   4. Reclaim gap: within a single continuous run of gate<i> == 1,
//      vel<i> never changes — because vel latches at claim and only ever
//      changes on the NEXT claim, and any next claim must first force
//      gate<i> low for at least one sample (the "reclaim gap", spec §2).
//      This is only a meaningful assertion for producers that can attempt
//      to reclaim a voice while it is still sounding (multi-note-identity
//      producers like midi); for a producer driven by a single mono
//      `gate` input (alloc) a claim can only occur on a rising edge, which
//      by definition already requires the previous sample to be low, so
//      gate<i> can never stay continuously high across a reclaim in the
//      first place — the property would hold trivially and prove nothing.
//      Pass `can_reclaim_while_sounding = false` for such producers to
//      SKIP this check explicitly (never silently assert-and-pass it).
//   4b. Exact reclaim gap: when can_reclaim_while_sounding is true, a low
//      run of gate<i> that separates two high runs and is shorter than the
//      caller-declared `min_release_samples` is BY DEFINITION a reclaim
//      gap (not a genuine release), and per spec §2 must be exactly one
//      sample long. The harness cannot otherwise distinguish "producer
//      reclaimed a still-sounding voice" from "producer released and then
//      quickly re-claimed" — `min_release_samples` is the caller's
//      declaration of the shortest gap its feed can produce via a genuine
//      release, so anything shorter is unambiguously a reclaim. Pass 0 (or
//      leave the default) when this distinction does not apply — no low
//      run then qualifies as "shorter than the minimum" and the check is a
//      no-op, not a silent pass of a real property.
//   5. Banks with index > n stay 0 for gate/voct/vel, every step.
#include <functional>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include "dsp/module.h"

namespace ms::dsp::test {

using FeedFn = std::function<void(int step)>;

inline void check_bank_conformance(Module& module, const ModuleInfo& info,
                                    int n, std::vector<float>& ins,
                                    const FeedFn& feed,
                                    bool can_reclaim_while_sounding,
                                    int steps = 64,
                                    int min_release_samples = 0) {
  REQUIRE(ins.size() == info.inputs.size());

  // Property 1: named bank slots exist for i = 1..8, independent of n.
  int gate_idx[9] = {}, voct_idx[9] = {}, vel_idx[9] = {};
  for (int i = 1; i <= 8; ++i) {
    gate_idx[i] = info.output_index("gate" + std::to_string(i));
    voct_idx[i] = info.output_index("voct" + std::to_string(i));
    vel_idx[i] = info.output_index("vel" + std::to_string(i));
    CAPTURE(i);
    // Fatal: gate_idx/voct_idx/vel_idx are used unconditionally below as
    // static_cast<size_t>(idx) into `outs`. A missing bank output (idx ==
    // -1) would otherwise UB-crash the whole test binary instead of
    // failing this test case cleanly — matches AllocRig::out()'s
    // REQUIRE(idx >= 0) convention in tests/test_alloc.cpp.
    REQUIRE(gate_idx[i] >= 0);
    REQUIRE(voct_idx[i] >= 0);
    REQUIRE(vel_idx[i] >= 0);
  }
  REQUIRE(n >= 1);
  REQUIRE(n <= 8);

  std::vector<float> outs(info.outputs.size(), 0.f);

  // Per-bank running state used to check properties incrementally.
  bool prev_gate_high[9] = {};   // gate<i> at the previous step
  bool have_prev[9] = {};        // whether a previous step exists yet
  bool holding[9] = {};          // in the "released, must not move" phase
  float held_voct[9] = {};       // voct<i> value latched at release
  float run_vel[9] = {};         // vel<i> value at the start of the current
                                  // continuous gate-high run
  bool in_run[9] = {};           // currently inside a continuous high run
  bool had_high[9] = {};         // gate<i> has been high at least once, so a
                                  // subsequent low run is "between two high
                                  // runs" (property 4b) rather than the
                                  // initial idle-low state before first claim
  int low_run_len[9] = {};       // length of the low run currently in
                                  // progress since the last high run ended
                                  // (only meaningful while had_high[i])

  for (int step = 0; step < steps; ++step) {
    feed(step);
    REQUIRE(ins.size() == info.inputs.size());
    module.process(ins.data(), outs.data());

    for (int i = 1; i <= n; ++i) {
      CAPTURE(step);
      CAPTURE(i);
      const float gate = outs[static_cast<size_t>(gate_idx[i])];
      const float voct = outs[static_cast<size_t>(voct_idx[i])];
      const float vel = outs[static_cast<size_t>(vel_idx[i])];

      // Property 2: gate is exactly binary.
      const bool gate_high = gate == 1.0f;
      const bool gate_low = gate == 0.0f;
      CHECK((gate_high || gate_low));

      const bool rising = gate_high && (!have_prev[i] || !prev_gate_high[i]);

      // Property 3: hold rule. While released (post gate-low, pre next
      // claim), voct must not move. A rising edge clears the hold BEFORE
      // the check runs: the claim sample itself is exactly where tracking
      // legitimately resumes and voct is expected to jump to the new
      // value on that very sample (spec §2) — checking the stale hold
      // value against it would be a false positive, not a real property.
      if (rising) holding[i] = false;
      if (holding[i]) {
        CHECK(voct == doctest::Approx(held_voct[i]));
      }
      if (gate_low) {
        holding[i] = true;
        held_voct[i] = voct;
      }

      // Property 4 / 4b: reclaim gap (conditional — see header comment).
      if (can_reclaim_while_sounding) {
        if (gate_high) {
          if (!in_run[i]) {
            // Rising edge: the low run that just ended (if any, and if it
            // followed a prior high run) is evaluated here, before it's
            // reset — see property 4b in the header comment.
            if (had_high[i] && low_run_len[i] < min_release_samples) {
              CHECK(low_run_len[i] == 1);
            }
            in_run[i] = true;
            run_vel[i] = vel;
          } else {
            CHECK(vel == doctest::Approx(run_vel[i]));
          }
          had_high[i] = true;
          low_run_len[i] = 0;
        } else {
          low_run_len[i] = in_run[i] ? 1 : (had_high[i] ? low_run_len[i] + 1 : 0);
          in_run[i] = false;
        }
      }

      prev_gate_high[i] = gate_high;
      have_prev[i] = true;
    }

    // Property 5: banks above n stay silent.
    for (int i = n + 1; i <= 8; ++i) {
      CAPTURE(step);
      CAPTURE(i);
      CHECK(outs[static_cast<size_t>(gate_idx[i])] == doctest::Approx(0.0f));
      CHECK(outs[static_cast<size_t>(voct_idx[i])] == doctest::Approx(0.0f));
      CHECK(outs[static_cast<size_t>(vel_idx[i])] == doctest::Approx(0.0f));
    }
  }
  // When can_reclaim_while_sounding is false, property 4 is intentionally
  // never evaluated above (the `if (can_reclaim_while_sounding)` guard at
  // its call site) rather than asserted-and-trivially-passing — see the
  // header comment for why that distinction matters.
}

}  // namespace ms::dsp::test
