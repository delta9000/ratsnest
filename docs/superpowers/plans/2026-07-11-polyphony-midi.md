# Polyphony + MIDI Batch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship `alloc`, `voice` groups, and `midi` as one batch so both bank
producers are proven against the same convention before either is relied on.

**Architecture:** `alloc` and `midi` are ordinary registry modules exposing
indexed port banks (`gate1..8`, `voct1..8`, `vel1..8`). `voice` is an
AST→AST expansion pass that runs before graph compile and stamps member
modules once per voice — the engine never learns polyphony exists. MIDI
transport is a standalone PipeWire control-port object feeding a SPSC ring;
the runner drains it at block start into modules that opt in via a
`wants_midi` flag and an `on_midi()` hook.

**Tech Stack:** C++20, doctest, PipeWire (existing deps only — rule 7).

**Specs:** `docs/superpowers/specs/2026-07-11-voice-alloc-design.md`,
`docs/superpowers/specs/2026-07-11-midi-design.md`. Read both before any task.

## Global Constraints

- `Module::process()` and everything RT-reachable: no allocation, locks, or
  I/O (CLAUDE.md rule 3). `on_midi()` is RT-called and follows the same rule.
- Everything is one float signal; gates rise through 0.5; v/oct in octaves
  with MIDI note 57 = 0.0; unipolar 0..1 (rules 2, 8).
- No normalling, no hidden `1/n`: voice output sums are straight sums (rule 4).
- Errors report line:col and never kill the session (rule 6).
- Every module: static `InputInfo`/`ConfigInfo`/`OutputInfo` tables, `init`,
  `process`, `copy_state_from` (see `src/dsp/modules/stepseq.cpp` as the
  house pattern), registered in `src/dsp/registry.cpp` + `src/dsp/modules.h`,
  documented in `docs/modules.md` (test_docs fails otherwise).
- Gates before any commit: `mise run test`; before finishing: `mise run ci`
  and `mise run test-san`.
- Commit style: conventional prefixes, no AI attribution, docs in the same
  commit as the behavior they describe.

## File Structure

| File | Responsibility |
|---|---|
| `src/dsl/parser.cpp` | dotted idents in decl names and cable endpoints (segments joined; no resolution here) |
| `src/graph/graph.cpp` | longest-declared-prefix split of dotted endpoint into (module, port) |
| `src/graph/voices.h/.cpp` (new) | `expand_voices(PatchAst&, Diags&)` — the whole voice feature |
| `src/dsp/modules/alloc.cpp` (new) | allocator module |
| `src/dsp/modules/midi.cpp` (new) | midi module (event → CV; no I/O) |
| `src/dsp/module.h` | `Module::on_midi()` default no-op; `ModuleInfo::wants_midi` flag |
| `src/engine/midi_in.h/.cpp` (new) | PipeWire control port → SPSC ring (the only unrenderable sliver) |
| `src/engine/rt_runner.*`, `engine.*` | block-start ring drain → `on_midi` fan-out |
| `tests/bank_conformance.h` (new) | shared bank-convention suite run against alloc AND midi |
| `tests/test_alloc.cpp`, `tests/test_voice.cpp`, `tests/test_midi.cpp` (new) | per-feature tests |
| `patches/strum.patch`, `patches/keys.patch` (new) | demo/integration patches |
| `docs/modules.md`, main design spec | palette entries, DSL section, counts |

---

### Task 1: Dotted names in the DSL

**Files:**
- Modify: `src/dsl/parser.cpp` (decl name parsing; `endpoint()` at :129-142)
- Modify: `src/graph/graph.cpp` (endpoint resolution around :96-127)
- Test: `tests/test_parser.cpp`, `tests/test_graph.cpp`

**Interfaces:**
- Produces: `Endpoint.module` may now hold a full dotted chain (`"lead.osc"`,
  `"lead.osc.v_oct"`) with `Endpoint.port` empty; graph resolves by longest
  declared-name prefix. `ModuleDecl.name` may contain dots. AST types in
  `src/dsl/ast.h` are unchanged.

- [ ] **Step 1: Write failing parser tests**

```cpp
// tests/test_parser.cpp
TEST_CASE("dotted declaration names parse") {
  auto r = dsl::parse("lead.osc = vco { wave: saw }\n");
  REQUIRE(r.ok());
  CHECK(r.ast.modules[0].name == "lead.osc");
}

TEST_CASE("dotted cable endpoints keep the full chain") {
  auto r = dsl::parse("a.b = vca\nc = vca\na.b -> c.cv\nc -> a.b.cv\n");
  REQUIRE(r.ok());
  CHECK(r.ast.cables[0].src.module == "a.b");
  CHECK(r.ast.cables[0].src.port.empty());
  CHECK(r.ast.cables[1].dst.module == "a.b.cv");  // graph splits, not parser
}
```

- [ ] **Step 2: Run to verify failure**

Run: `mise run build && ./build/tests --test-case="dotted*"`
Expected: FAIL (parser errors on the dot in a decl name).

- [ ] **Step 3: Implement parser change**

In `endpoint()`, loop instead of the single optional dot, joining segments:

```cpp
bool endpoint(Endpoint& e) {
  const Token& m = next();
  e.module = m.text; e.line = m.line; e.col = m.col;
  while (at(Tok::Dot)) {
    next();
    if (!at(Tok::Ident)) {
      err(cur().line, cur().col, "expected name after '.'");
      sync();
      return false;
    }
    e.module += '.';
    e.module += next().text;
  }
  return true;  // e.port stays empty; graph resolves module vs port
}
```

In the decl path, accept the same `ident (. ident)*` chain for the name
before `=` (same loop, appending to the name string). Type names never dot.

- [ ] **Step 4: Update graph resolution**

In `graph.cpp`, replace direct `index_of.find(c.dst.module)` /
`c.dst.port` use with a longest-prefix split for both endpoints:

```cpp
// Longest declared name that is the chain or a dot-prefix of it wins;
// the remainder (at most one segment) is the port.
struct Resolved { int node = -1; std::string port; };
Resolved resolve(const std::unordered_map<std::string,int>& index_of,
                 const std::string& chain) {
  auto it = index_of.find(chain);
  if (it != index_of.end()) return {it->second, ""};
  auto pos = chain.rfind('.');
  if (pos != std::string::npos) {
    auto head = index_of.find(chain.substr(0, pos));
    if (head != index_of.end()) return {head->second, chain.substr(pos + 1)};
  }
  return {};
}
```

Unknown chain keeps the existing "unknown module '<chain>'" line:col error.
`lead.osc.v_oct` resolves because `lead.osc` is declared; `filter.cutoff`
resolves because `filter` is. Ambiguity is impossible: names are unique and
the longest match is preferred, so declaring both `a` and `a.b` still routes
`a.b` to the module `a.b` and `a.b.c` to its port `c`.

- [ ] **Step 5: Add graph test for the split**

```cpp
// tests/test_graph.cpp
TEST_CASE("dotted module names resolve by longest prefix") {
  auto r = go("a.b = vca\nc = vca\nc -> a.b.cv : 2\na.b -> c\n");
  REQUIRE(r.ok());
}
```

- [ ] **Step 6: Run full suite; fix any old test asserting single-dot behavior**

Run: `mise run test` — all existing patches still compile (`cli_check`).

- [ ] **Step 7: Commit**

`git commit -m "feat: dotted names in decls and cable endpoints"`

---

### Task 2: `alloc` module

**Files:**
- Create: `src/dsp/modules/alloc.cpp`
- Modify: `src/dsp/modules.h` (+`alloc_info`), `src/dsp/registry.cpp`
- Modify: `docs/modules.md` (entry + quick-palette row), main spec §5 count 35→36 and Phase 2 table row
- Test: `tests/test_alloc.cpp`

**Interfaces:**
- Produces: module type `"alloc"`; configs `n`(1..8, default 4), `mode`
  (`rr`|`unison`); inputs `gate` 0, `voct` 0, `vel` 1; outputs
  `gate1..8, voct1..8, vel1..8, spread1..8` in exactly that slot order
  (gate1=slot 0 … spread8=slot 31). Task 3/4 depend on this ordering.

- [ ] **Step 1: Write failing tests** (pattern: `tests/test_euclid.cpp`)

```cpp
// tests/test_alloc.cpp — key cases; one process() call = one sample
TEST_CASE("rr claims voices in order and older voices hold pitch") {
  auto m = make_module("alloc", {{"n", num(3)}});
  // gate rise with voct=0.25 -> voice1 sounds and tracks
  step(m, {1, 0.25f, 1});  CHECK(out(m,"gate1") == 1);  CHECK(out(m,"voct1") == 0.25f);
  step(m, {1, 0.30f, 1});  CHECK(out(m,"voct1") == 0.30f);   // tracks while newest
  step(m, {0, 0.30f, 1});  CHECK(out(m,"gate1") == 0);
  step(m, {1, 0.90f, 1});  CHECK(out(m,"gate2") == 1);       // next voice
  CHECK(out(m,"voct1") == 0.30f);                             // held for release tail
}
TEST_CASE("reclaim inserts one-sample gate gap") { /* claim v1..v3, then 4th
  edge reclaims v1: assert gate1 == 0 for exactly the claim sample, 1 after */ }
TEST_CASE("unison: all gates follow, spread is centered") {
  // n=4 -> spread values -1, -1/3, +1/3, +1; n=1 -> 0
}
TEST_CASE("banks above n stay zero") { /* n=2: gate3..8 == 0 always */ }
TEST_CASE("alloc migrates claim state") { /* copy_state_from mid-claim,
  step both, outputs identical — pattern: tests/test_migrate.cpp */ }
```

- [ ] **Step 2: Run to verify failure** (`unknown module type 'alloc'`)

- [ ] **Step 3: Implement**

```cpp
// src/dsp/modules/alloc.cpp
#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
namespace ms::dsp {
namespace {
constexpr InputInfo kIns[] = {{"gate", 0.f}, {"voct", 0.f}, {"vel", 1.f}};
constexpr const char* kModes[] = {"rr", "unison", nullptr};
constexpr ConfigInfo kCfg[] = {{"n", nullptr}, {"mode", kModes}};
constexpr OutputInfo kOuts[] = {
  {"gate1"},{"gate2"},{"gate3"},{"gate4"},{"gate5"},{"gate6"},{"gate7"},{"gate8"},
  {"voct1"},{"voct2"},{"voct3"},{"voct4"},{"voct5"},{"voct6"},{"voct7"},{"voct8"},
  {"vel1"},{"vel2"},{"vel3"},{"vel4"},{"vel5"},{"vel6"},{"vel7"},{"vel8"},
  {"spread1"},{"spread2"},{"spread3"},{"spread4"},
  {"spread5"},{"spread6"},{"spread7"},{"spread8"}};

class Alloc final : public Module {
 public:
  void init(double, const Config& cfg) override {
    n_ = static_cast<int>(std::clamp(cfg_num(cfg, "n", 4.0), 1.0, 8.0));
    unison_ = cfg_str(cfg, "mode", "rr") == "unison";
  }
  void process(const float* ins, float* outs) override {
    const bool rising = ins[0] > 0.5f && prev_gate_ <= 0.5f;
    const bool high = ins[0] > 0.5f;
    prev_gate_ = ins[0];
    if (rising) {
      newest_ = unison_ ? -1 : (newest_ + 1) % n_;   // -1 = all (unison)
      for (int i = 0; i < n_; ++i) {
        const bool claimed = unison_ || i == newest_;
        if (claimed) {
          gap_[i] = gate_[i];          // was sounding -> one-sample low gap
          gate_[i] = true;
          vel_[i] = ins[2];
        }
      }
    }
    for (int i = 0; i < 8; ++i) {
      const bool active = i < n_;
      const bool newest = unison_ ? active : i == newest_;
      if (active && newest && high) voct_[i] = ins[1];       // track
      if (active && newest) gate_[i] = high; else if (!newest) gate_[i] = false;
      const bool g = active && gate_[i] && !gap_[i];
      outs[i] = g ? 1.f : 0.f;
      outs[8 + i] = voct_[i];
      outs[16 + i] = vel_[i];
      outs[24 + i] = !active ? 0.f
          : n_ == 1 ? 0.f
          : -1.f + 2.f * static_cast<float>(i) / static_cast<float>(n_ - 1);
      gap_[i] = false;
    }
  }
  void copy_state_from(const Module& o_) override {
    const auto& o = static_cast<const Alloc&>(o_);
    newest_ = o.newest_; prev_gate_ = o.prev_gate_;
    for (int i = 0; i < 8; ++i) { gate_[i]=o.gate_[i]; gap_[i]=o.gap_[i];
                                  voct_[i]=o.voct_[i]; vel_[i]=o.vel_[i]; }
  }
 private:
  int n_ = 4, newest_ = -1;
  bool unison_ = false, gate_[8] = {}, gap_[8] = {};
  float voct_[8] = {}, vel_[8] = {}, prev_gate_ = 0.f;
};
std::unique_ptr<Module> make() { return std::make_unique<Alloc>(); }
}  // namespace
const ModuleInfo alloc_info{"alloc", kIns, kCfg, kOuts, &make};
}  // namespace ms::dsp
```

NOTE the release rule: in `rr`, a non-newest voice's gate drops the moment
it is superseded (spec: gate high while input high AND still newest). The
loop above enforces it (`else if (!newest) gate_[i] = false;`). Write the
test for supersede-drops-gate before trusting this code.

- [ ] **Step 4: Register + document** — `modules.h`, `registry.cpp`
  (append `&alloc_info`), `docs/modules.md` entry:

```markdown
## `alloc`

`alloc { n: 4, mode: rr }` — inputs `gate`, `voct`, `vel` 1; output banks
`gate1..8`, `voct1..8`, `vel1..8`, `spread1..8` (bipolar constants spread
over -1..+1; 0 when n = 1). Each rising gate claims the next voice (`rr`)
or all voices (`unison`); a voice tracks `voct` while it is the newest
claim and holds it after, so release tails keep pitch. Reclaims insert a
one-sample gap. Strum polyphony from any mono sequencer; see `voice`.
```

- [ ] **Step 5: `mise run test` green (test_docs now passes only with the doc entry)**

- [ ] **Step 6: Commit** — `feat: alloc voice allocator with indexed port banks`

---

### Task 3: Bank-conformance suite (shared with midi)

**Files:**
- Create: `tests/bank_conformance.h`
- Modify: `tests/test_alloc.cpp` (instantiate suite)

**Interfaces:**
- Produces: `void check_bank_conformance(ms::dsp::Module&, const ms::dsp::ModuleInfo&, int n, FeedFn feed)` where `FeedFn` = `std::function<void(int step)>`
  drives the producer (sets alloc inputs / injects midi events). Task 8
  reuses this against `midi` verbatim — that is the batch's coupling payoff.

- [ ] **Step 1: Write the suite** — asserts, for any bank producer:
  gates are 0/1 and rise through 0.5; `voct<i>` never changes while
  `gate<i>` is low-after-release (hold rule); a reclaim shows a 1-sample
  gap; banks above `n` are silent; `gate<i>`/`voct<i>`/`vel<i>` slot names
  exist for i = 1..8. Concrete: iterate `info.outputs`, locate banks by
  name, run `feed` for 64 steps recording transitions.

- [ ] **Step 2: Apply to alloc in test_alloc.cpp; `mise run test`; commit**
  `test: shared bank-conformance suite, applied to alloc`

---

### Task 4: `voice` expansion pass

**Files:**
- Create: `src/graph/voices.h`, `src/graph/voices.cpp`
- Modify: `src/graph/graph.cpp` `compile()` — call `expand_voices(ast, r.errors)` first; also reject `'#'`-containing user names is unnecessary (lexer cannot produce them — `#` opens a comment)
- Test: `tests/test_voice.cpp`

**Interfaces:**
- Produces: `void expand_voices(dsl::PatchAst& ast, Diags& errors)` —
  removes `voice` decls, stamps members as `<member>#<i>` (0-based),
  rewrites cables per the four cases. Pure AST→AST; deterministic order
  (member decl order × ascending i) so reload's `same_structure` works.

- [ ] **Step 1: Write failing tests**

```cpp
// tests/test_voice.cpp
static dsl::PatchAst go(const char* src) {
  auto p = dsl::parse(src); REQUIRE(p.ok());
  Diags d; graph::expand_voices(p.ast, d); REQUIRE(d.empty());
  return std::move(p.ast);
}
const char* kPatch =
  "poly = alloc { n: 2 }\n"
  "lead = voice { notes: poly }\n"
  "lead.osc = vco\n"
  "lead.amp = vca\n"
  "poly.voct -> lead.osc.v_oct\n"   // case 2: bare bank
  "lfo1 = lfo\n"
  "lfo1 -> lead.osc.fm\n"           // case 3: broadcast
  "lead.osc -> lead.amp.in\n"       // case 1: instance-local
  "lead.amp -> out.in\n";           // case 4: sum

TEST_CASE("members are stamped n times with #i names") {
  auto ast = go(kPatch);
  // decls: poly, lfo1, lead.osc#0, lead.osc#1, lead.amp#0, lead.amp#1
  CHECK(ast.modules.size() == 6);
}
TEST_CASE("four cable cases expand correctly") {
  auto ast = go(kPatch);
  // bare bank:    poly.voct1 -> lead.osc#0.v_oct ; poly.voct2 -> lead.osc#1.v_oct
  // broadcast:    lfo1 -> both instances
  // local:        lead.osc#i -> lead.amp#i (no cross terms)
  // sum:          lead.amp#0 -> out.in ; lead.amp#1 -> out.in
  CHECK(ast.cables.size() == 2 + 2 + 2 + 2);
}
TEST_CASE("errors: member without group, n mismatch, nested voice, no members,"
          " notes target lacks banks") { /* each -> one diag with line:col */ }
```

Bank indices are 1-based on ports (`voct1`) while instances are 0-based
(`#0`) — assert exactly that; off-by-one here is the likeliest bug.

- [ ] **Step 2: Implement** — sketch of the core:

```cpp
// voices.cpp core (data gathering elided: groups = name -> {notes, n, line, col})
// 1. find voice decls (decl.type == "voice"); n = bound decl's cfg n/voices
//    (look up bound decl by name, then its params; alloc default 4, midi
//    voices default 1); validate via dsp::find_module(bound.type):
//    output_index("gate1") >= 0 else error.
// 2. members: decls whose name starts with group + '.'
// 3. bank families of the bound module: every output name ending in '1'
//    with the stem also present as stem+"2".. collect stems {gate,voct,vel,spread}.
// 4. rewrite: for each member m, for i in 0..n-1 emit copy named m.name+"#"+i.
// 5. cables: classify each endpoint: MEMBER(g), BOUND_BARE(g) (module==notes
//    && port is a bank stem), OTHER. Emit per the case table; a cable
//    between members of two different groups g1,g2 expands to n1*n2 cables.
// 6. delete voice decls; leave everything else untouched.
```

Endpoint port at this stage: cables still carry the *joined* chain from
Task 1 (`lead.osc.v_oct`). Expansion must therefore split with the same
longest-prefix rule against the pre-expansion decl set — factor Task 1's
`resolve()` into a shared helper `split_endpoint(decls, chain)` in
`voices.h` and have `graph.cpp` use it too, so there is exactly one
implementation of the rule.

- [ ] **Step 3: Tests green; run `mise run test` (all existing patches unaffected — no voice decls in them)**

- [ ] **Step 4: Commit** — `feat: voice groups — compile-time per-voice expansion`

---

### Task 5: Voice groups through compile + reload

**Files:**
- Modify: `tests/test_voice.cpp` (full-compile cases), `tests/test_migrate.cpp` or `tests/test_reload.cpp`

- [ ] **Step 1: Full-compile test** — `graph::compile` on `kPatch` from
  Task 4 succeeds; node count and `in_cables` shapes match; a `scope`
  member yields per-instance nodes.

- [ ] **Step 2: Reload tests** — build graph A (n=2 group, run 100 samples),
  edit param inside a member, rebuild, `migrate` (pattern at
  `tests/test_reload.cpp`), assert per-instance state carried (e.g. vco
  phase continuity per instance). Then change allocator `n` 2→3: instances
  #0/#1 migrate, #2 fresh.

- [ ] **Step 3: Commit** — `test: voice groups migrate per instance across reload`

---

### Task 6: Strum demo patch + integration

**Files:**
- Create: `patches/strum.patch`
- Modify: `docs/modules.md` (DSL essentials: dotted names + voice/alloc recipe), main spec §3.5 (`voice` no longer deferred — implemented, pointer to spec)

- [ ] **Step 1: Write the patch** — seeded, renderable:

```text
# strum -- three overlapping plucked voices from one mono sequencer line.
clk = clock { bpm: 180 }
wander = random { rate: 2.1, smooth: 0, seed: 14 }
notes = sh { mode: sample }
wander -> notes.in
clk -> notes.trig
key = quant { scale: [0, 3, 5, 7, 10] }
notes -> key.in : 0.4

poly = alloc { n: 3 }
clk -> poly.gate
key -> poly.voct

harp = voice { notes: poly }
harp.osc = vco { freq: 220hz, wave: tri }
harp.pluck = lpg { damp: 0.5, decay: 0.9 }
poly.voct -> harp.osc.v_oct
poly.spread -> harp.osc.v_oct : 0.004
harp.osc -> harp.pluck.in
poly.gate -> harp.pluck.strike
harp.pluck -> out.in : 0.45
-0.5 -> out.level
```

- [ ] **Step 2: Verify** — `mise run check -- patches/strum.patch`;
  `./build/modsynth render patches/strum.patch -d 12s --stats 2s -o /tmp/strum.wav`:
  RMS must be nonzero in every window (overlapping decay-0.9 tails across
  330 ms steps prove voices genuinely overlap — the mono equivalent would
  choke each pluck).

- [ ] **Step 3: Commit** — `feat: strum patch — first polyphonic patch (docs: voice DSL section)`

---

### Task 7: MIDI plumbing (hook + ring + runner drain)

**Files:**
- Modify: `src/dsp/module.h` — `virtual void on_midi(const uint8_t* data, size_t len) {}` on `Module`; `bool wants_midi = false;` last field of `ModuleInfo` (aggregate default — existing initializers unaffected)
- Modify: `src/engine/engine.h/.cpp`, `src/engine/rt_runner.h/.cpp` — engine owns `rt::SpscQueue<MidiEvent, 256>` (the existing type in `src/rt/spsc.h`; `struct MidiEvent { uint8_t data[4]; uint8_t len; }`); graph build collects indices of nodes whose `info->wants_midi`; runner drains ring at block start calling `on_midi` on each
- Test: `tests/test_runner.cpp`

- [ ] **Step 1: Failing test** — a stub module registered only in the test
  (pattern: `tests/test_module.cpp` builds modules directly) records
  `on_midi` calls; push 3 events into the engine ring, run one block,
  expect all 3 delivered before sample 0 of the block, ring empty after.

- [ ] **Step 2: Implement; `mise run test`; commit**
  `feat: midi event ring and block-start delivery to wants_midi modules`

---

### Task 8: `midi` module (no I/O — pure event → CV)

**Files:**
- Create: `src/dsp/modules/midi.cpp`
- Modify: `src/dsp/modules.h`, `src/dsp/registry.cpp`, `docs/modules.md` (+ palette count 36→37 is wrong — midi was already counted in Phase 2; do NOT bump)
- Test: `tests/test_midi.cpp`

**Interfaces:**
- Consumes: `on_midi` hook (Task 7), bank-conformance suite (Task 3).
- Produces: type `"midi"`, `wants_midi = true`; configs `channel` 0..16,
  `voices` 1..8 (default 1), `mode` (`last`|`lowest`), `bend` semitones,
  `cc` array (≤4 entries, 0..127); outputs `gate1..8, voct1..8, vel1..8,
  bend, at, cc1..cc4` — NO mono gate/voct/vel outputs (reserved names,
  midi spec §1).

- [ ] **Step 1: Failing tests** — all by injecting bytes, no hardware:

```cpp
// tests/test_midi.cpp — representative set (write all from midi spec §5)
TEST_CASE("note on/off, voct math, velocity") {
  auto m = make_module("midi", {{"voices", num(2)}});
  noteon(m, /*ch*/0, /*note*/57, /*vel*/127); step(m);
  CHECK(out(m,"gate1") == 1); CHECK(out(m,"voct1") == doctest::Approx(0.f));
  noteon(m, 0, 69, 64); step(m);
  CHECK(out(m,"voct2") == doctest::Approx(1.f));
  noteoff(m, 0, 57); step(m);
  CHECK(out(m,"gate1") == 0); CHECK(out(m,"voct1") == doctest::Approx(0.f)); // holds
}
TEST_CASE("oldest steal at capacity inserts gate gap") { /* voices:2, 3 notes */ }
TEST_CASE("mono last: held key restores on release") { /* 57 down, 60 down,
  60 up -> voct1 back to 57's pitch, gate stays high (no retrigger) */ }
TEST_CASE("mono lowest wins") { /* 60 down, 57 down -> 57; 57 up -> 60 */ }
TEST_CASE("sustain defers note-off until pedal release") { /* CC64 */ }
TEST_CASE("bend folds into voct and bend output") { /* bend: 2 default */ }
TEST_CASE("cc slots map, scale, smooth") { /* cc: [74,1]; assert one-pole
  ~3ms: after 3ms of steps, within 1/e of target */ }
TEST_CASE("channel filter") { /* channel: 2 ignores ch1 events; 0 = omni */ }
TEST_CASE("vel 0 note-on is note-off") {}
TEST_CASE("midi state migrates mid-chord") {}
TEST_CASE("midi passes bank conformance") { /* Task 3 suite; feed = inject */ }
```

- [ ] **Step 2: Implement** — held-note table `struct Held { uint8_t note;
  uint32_t order; }` per voice + a pressed-keys stack (16 deep) for mono
  restore; parse in `on_midi` (status 0x80/0x90/0xB0/0xE0/0xD0 after
  channel filter); outputs computed in `process()` from tracker state.
  Same file pattern as alloc. `const ModuleInfo midi_info{"midi", kIns,
  kCfg, kOuts, &make, /*wants_midi=*/true};`

- [ ] **Step 3: docs/modules.md entry** (banks-only note included verbatim
  from spec §1); `mise run test` green.

- [ ] **Step 4: Commit** — `feat: midi module — note tracking, banks, bend, cc slots`

---

### Task 9: PipeWire MIDI transport

**Files:**
- Create: `src/engine/midi_in.h`, `src/engine/midi_in.cpp`
- Modify: `src/engine/controller.cpp` (own a MidiIn in play mode; push into engine ring), `src/main.cpp` (play only; render never constructs one)

- [ ] **Step 1: VERIFY THE PAYLOAD FORMAT FIRST.** The repo's PipeWire
  research doc does not cover control ports, and the wire format changed
  across PipeWire versions (`SPA_CONTROL_Midi` raw bytes vs UMP). Check the
  local headers before writing code:
  `grep -rn "SPA_CONTROL_UMP\|SPA_CONTROL_Midi" /usr/include/spa/` and read
  `/usr/share/doc/pipewire` examples if present. Target what the build
  machine has; note the choice in a comment. If UMP: convert 32-bit UMP MIDI1
  channel-voice packets to 3-byte events (status = byte1, data = bytes 2-3).

- [ ] **Step 2: Implement `MidiIn`** — its own `pw_filter` with one control
  input port (`"midi-in"`), process callback: dequeue buffer, iterate
  `spa_pod_sequence` controls, for each MIDI/UMP control push a `MidiEvent`
  into the engine's ring (constructor takes `rt::Spsc<MidiEvent,256>&`).
  Drop on ring-full — never block (rule 3). Own the port for the process
  lifetime: hot reload must not touch this object (midi spec §4).

- [ ] **Step 3: Manual verification protocol** (no CI hardware): run
  `mise run play -- patches/keys.patch`, connect a controller or
  `pw-cli`/`wpctl` a virtual keyboard to the `modsynth midi-in` port,
  confirm notes sound and `--watch mid.gate1` shows gates. Record the exact
  commands used in the commit message body.

- [ ] **Step 4: Commit** — `feat: pipewire midi input port feeding the event ring`

---

### Task 10: keys patch, spec sync, final gates

**Files:**
- Create: `patches/keys-poly.patch` (midi → 4-voice subtractive poly: banks →
  voice group with adsr + ladder per voice, cc1 → cutoff, sustain-friendly
  release; mirrors strum.patch structure with `notes: mid`)
- Modify: main design spec — §5 Phase 2 table midi row updated to implemented
  surface; §10 phasing note that polyphony landed before midi by design
- Modify: `CLAUDE.md` only if a workflow changed (likely nothing)

- [ ] **Step 1: Write patch; `mise run check`; `render --stats` (silent —
  no events offline — but must compile and render without error)**
- [ ] **Step 2: `mise run test && mise run ci && mise run test-san` — all green**
- [ ] **Step 3: Commit** — `feat: keys-poly patch; docs: sync spec to implemented polyphony+midi`

---

## Self-review notes (kept — they are part of the plan's contract)

- Spec coverage: voice spec §2 (claim semantics) → Task 2; §3 (all four
  cable cases + errors + reload) → Tasks 4-5; §6 testing list → Tasks 2-5;
  midi spec §1-2 → Task 8; §3 → Tasks 7, 9; §4 → Task 9 step 2; §5 → Task 8;
  banks-conformance-on-both → Tasks 3 and 8 step 1 (last case).
- Type consistency: `rt::SpscQueue<MidiEvent,256>`, `MidiEvent{data[4],len}` (Tasks 7, 9);
  `expand_voices(PatchAst&, Diags&)` (Tasks 4, 5); bank slot order
  gate/voct/vel/spread × 8 (Tasks 2, 3, 4 step 2 bank-stem detection).
- Known risk, owned: Task 9 payload format is machine-dependent — that is
  why verification is step 1 of the task, not an afterthought.
