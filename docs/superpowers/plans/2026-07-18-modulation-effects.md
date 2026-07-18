# Modulation effects Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax; a task is done only when its named tests pass.

**Goal:** Add a stereo `chorus` module, a mono `phaser` module, and a `flanger` recipe, per `docs/superpowers/specs/2026-07-18-modulation-effects-design.md`.

**Architecture:** Two ordinary registry modules (`src/dsp/modules/chorus.cpp`, `phaser.cpp`), each self-contained with an internal LFO and fixed-size state allocated in `init()`. Flanger is documentation only. Sources/tests are CMake-globbed — new files need no CMakeLists edits.

**Tech Stack:** C++20, doctest, CMake presets via mise.

## Global Constraints

- `process()`/`copy_state_from()` never allocate/lock/do I/O (rule 3); allocation only in `init()`.
- Signal conventions (rule 8): audio ±1, unipolar CV 0..1, `rate` Hz, `feedback` 0..<1. NaN-proof clamps (`!(x >= lo)`).
- `-Wall -Wextra -Wconversion`, `mise run ci` warnings-as-errors: cast `double`↔`float` explicitly; give any new struct/aggregate fields default initializers.
- Modules register via `extern const ModuleInfo <name>_info;` in `src/dsp/modules.h` and `&<name>_info` in `kRegistry` (`src/dsp/registry.cpp`).
- `docs/modules.md` needs a `## chorus` and `## phaser` heading once registered (`test_docs.cpp`). Every `patches/*.patch` gets an automatic `ratsnest check` ctest.
- Build/test: `mise run build`; one file `cmake --build build -j && ./build/mstests -sf="*test_chorus*"`; gates `mise run test`, `mise run test-san`, `mise run ci`.

---

### Task 1: `chorus` — stereo multi-voice modulated delay

**Files:** Create `src/dsp/modules/chorus.cpp`; modify `src/dsp/modules.h`, `src/dsp/registry.cpp`; test `tests/test_chorus.cpp`.

**Interface:** inputs `in_l,in_r,rate(0.5),depth(0.5),mix(0.5)`; configs `voices`(3), `delay`(12 ms), `swing`(6 ms), `shape`(`tri`/`sine`); outputs `out_l,out_r`.

- [ ] **Step 1: failing tests** — `tests/test_chorus.cpp`:

```cpp
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include "dsp/modules.h"
using namespace ms::dsp;
namespace {
ConfigValue cnum(double x){ConfigValue c;c.is_num=true;c.num=x;return c;}
struct Rig {
  std::unique_ptr<Module> m; float ins[5]={}; float outs[2]={};
  explicit Rig(double r,const Config& cfg={}){auto* i=find_module("chorus");REQUIRE(i);m=i->create();m->init(r,cfg);
    ins[2]=0.5f;ins[3]=0.5f;ins[4]=0.5f;}
  void tick(float l,float r){ins[0]=l;ins[1]=r;m->process(ins,outs);}
};
}
TEST_CASE("chorus: registered stereo surface"){
  auto* i=find_module("chorus"); REQUIRE(i);
  CHECK(i->inputs.size()==5); CHECK(i->outputs.size()==2);
  CHECK(i->input_index("in_l")==0); CHECK(i->input_index("in_r")==1);
  CHECK(i->input_index("mix")==4);
  CHECK(i->output_index("out_l")==0); CHECK(i->output_index("out_r")==1);
  CHECK(i->config("voices")!=nullptr); CHECK(i->config("delay")!=nullptr);
}
TEST_CASE("chorus: mix 0 passes dry through both channels"){
  Config cfg; Rig rig(48000.0,cfg); rig.ins[4]=0.0f;
  for(int i=0;i<2000;++i){ float x=std::sin(i*0.1f); rig.tick(x, -x);
    CHECK(rig.outs[0]==doctest::Approx(x).epsilon(1e-4));
    CHECK(rig.outs[1]==doctest::Approx(-x).epsilon(1e-4)); }
}
TEST_CASE("chorus: wet modulates a held input over time, L != R"){
  Rig rig(48000.0); rig.ins[4]=1.0f;                 // full wet
  float mn=1e9f,mx=-1e9f; bool differ=false;
  for(int i=0;i<48000;++i){ rig.tick(1.0f,1.0f);     // DC in -> wet is time-varying (pitch mod)
    mn=std::min(mn,rig.outs[0]); mx=std::max(mx,rig.outs[0]);
    if(std::abs(rig.outs[0]-rig.outs[1])>1e-4f) differ=true; }
  CHECK(mx-mn>0.05f);   // the chorus actually modulates
  CHECK(differ);        // stereo decorrelation
}
TEST_CASE("chorus: finite under hostile CV"){
  Rig rig(1000.0); const float nan=std::numeric_limits<float>::quiet_NaN();
  rig.ins[2]=nan;rig.ins[3]=nan;rig.ins[4]=nan;
  for(int i=0;i<1000;++i){ rig.tick(0.5f,0.5f);
    CHECK(std::isfinite(rig.outs[0])); CHECK(std::isfinite(rig.outs[1])); }
}
```

(Add `#include <limits>` and `#include <memory>` as needed.)

- [ ] **Step 2: run, verify fail** — `cmake --build build -j && ./build/mstests -sf="*test_chorus*"` (compile/link fail: no `chorus_info`).

- [ ] **Step 3: implement** — Create `src/dsp/modules/chorus.cpp` following the spec §2. Structure (fill from spec, mirror `delay.cpp` for the ring buffer + linear-interpolated fractional read):
  - `kIns = {in_l, in_r, rate .5, depth .5, mix .5}`, `kCfg = {voices, delay, swing, shape}`, `kOuts = {out_l, out_r}`.
  - `init`: read configs (`voices` clamp 1..4, `delay`/`swing` ms → clamp; `shape`), size two ring buffers to `ceil((delay+swing)ms) + guard` (power of two), zero them, reset LFO phase and write index.
  - `process`: advance LFO phase by `clampn(rate + base?, 0, ...) / rate_` (rate is an input; base 0). For each voice `i`: `ph_i = frac(lfo_phase + i/voices)`; `lfo_i = tri/sine(ph_i) ∈ [-1,1]`; `d_i = (delay_ms + clampn(depth,0,1)*swing_ms*lfo_i) * rate_/1000` samples; read fractional tap from a channel buffer (voices split across L/R). `out_l = (1-mix)*in_l + mix*Σ_Lwet`; `out_r` similarly. Write `in_l`/`in_r` into their buffers, advance write index. NaN-clamp `rate`/`depth`/`mix`.
  - `copy_state_from`: copy the two buffers, write index, LFO phase.
  - `const ModuleInfo chorus_info{"chorus", kIns, kCfg, kOuts, &make};`
  Register in `modules.h` + `registry.cpp`.

- [ ] **Step 4: run, verify pass** — all `chorus:` cases pass. (Adjust numeric thresholds to real output if slightly off; keep the dry-through and finiteness assertions.)

- [ ] **Step 5: commit** — `git add src/dsp/modules/chorus.cpp src/dsp/modules.h src/dsp/registry.cpp tests/test_chorus.cpp && git commit -m "feat: chorus stereo multi-voice modulation effect"`

---

### Task 2: `phaser` — mono swept allpass cascade

**Files:** Create `src/dsp/modules/phaser.cpp`; modify `modules.h`, `registry.cpp`; test `tests/test_phaser.cpp`.

**Interface:** inputs `in,rate(0.3),depth(0.6),feedback(0.3),mix(0.5)`; configs `stages`(6), `spread`(0), `shape`; output `out`.

- [ ] **Step 1: failing tests** — `tests/test_phaser.cpp`:

```cpp
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/modules.h"
using namespace ms::dsp;
namespace {
double dft_mag(const std::vector<float>& x,double f,double r){double re=0,im=0;
  for(size_t n=0;n<x.size();++n){double a=2.0*M_PI*f*double(n)/r;re+=x[n]*std::cos(a);im-=x[n]*std::sin(a);}
  return 2.0/double(x.size())*std::sqrt(re*re+im*im);}
struct Rig{ std::unique_ptr<Module> m; float ins[5]={}; float outs[1]={};
  explicit Rig(double r,const Config& cfg={}){auto* i=find_module("phaser");REQUIRE(i);m=i->create();m->init(r,cfg);
    ins[1]=0.3f;ins[2]=0.6f;ins[3]=0.3f;ins[4]=0.5f;}
  float tick(float x){ins[0]=x;m->process(ins,outs);return outs[0];} };
}
TEST_CASE("phaser: registered surface"){
  auto* i=find_module("phaser"); REQUIRE(i);
  CHECK(i->inputs.size()==5); CHECK(i->outputs.size()==1);
  CHECK(i->input_index("in")==0); CHECK(i->input_index("feedback")==3); CHECK(i->input_index("mix")==4);
  CHECK(i->config("stages")!=nullptr); CHECK(i->config("spread")!=nullptr);
}
TEST_CASE("phaser: mix 0 passes dry through"){
  Rig rig(48000.0); rig.ins[4]=0.0f;
  for(int i=0;i<2000;++i){ float x=std::sin(i*0.05f); CHECK(rig.tick(x)==doctest::Approx(x).epsilon(1e-4)); }
}
TEST_CASE("phaser: produces a moving spectral notch and stays bounded"){
  Config cfg; cfg.push_back({"stages",[]{ConfigValue c;c.is_num=true;c.num=6;return c;}()});
  Rig rig(48000.0,cfg); rig.ins[3]=0.0f;             // no feedback for a clean notch
  // white-ish input: sum of many partials
  std::vector<float> y(48000);
  float peak=0;
  for(int i=0;i<48000;++i){ float x=0; for(int k=1;k<=20;++k) x+=0.05f*std::sin(2.0f*float(M_PI)*220.0f*k*i/48000.0f);
    y[static_cast<size_t>(i)]=rig.tick(x); peak=std::max(peak,std::abs(y[static_cast<size_t>(i)])); }
  CHECK(std::isfinite(peak)); CHECK(peak<8.0f);       // bounded
  // energy is not flat: at least one partial is notched well below the mean
  double sum=0; for(int k=1;k<=20;++k) sum+=dft_mag(y,220.0*k,48000.0);
  double mean=sum/20.0, lo=1e9;
  for(int k=1;k<=20;++k) lo=std::min(lo,dft_mag(y,220.0*k,48000.0));
  CHECK(lo < 0.5*mean);
}
TEST_CASE("phaser: feedback stays bounded at 0.95"){
  Config cfg; Rig rig(48000.0,cfg); rig.ins[3]=0.95f;
  for(int i=0;i<48000;++i){ float o=rig.tick(std::sin(i*0.03f)); CHECK(std::isfinite(o)); CHECK(std::abs(o)<8.0f); }
}
TEST_CASE("phaser: finite under hostile CV"){
  Rig rig(1000.0); const float nan=std::numeric_limits<float>::quiet_NaN();
  rig.ins[1]=nan;rig.ins[2]=nan;rig.ins[3]=nan;rig.ins[4]=nan;
  for(int i=0;i<1000;++i){ float o=rig.tick(0.5f); CHECK(std::isfinite(o)); }
}
```

- [ ] **Step 2: run, verify fail**.

- [ ] **Step 3: implement** — Create `src/dsp/modules/phaser.cpp` per spec §3:
  - `kIns={in,rate .3,depth .6,feedback .3,mix .5}`, `kCfg={stages,spread,shape}`, `kOuts={out}`.
  - `init`: `stages` clamp 2..12, `spread` clamp 0..1, `shape`; reset `std::array<float,12> ap_state_{}`, `fb_=0`, `phase_=0`.
  - `process`: LFO `m=tri/sine(phase)`; `phase += clampn(rate,0,..)/rate_`. `fc = 100*pow(40, (clampn(depth,0,1)*m+1)/2)` (100→4000), clamp `< 0.45*rate_`. Input `x = in + clampn(feedback,0,0.95)*fb_`. For k in stages: `fck = fc*exp2(spread*(k-(stages-1)/2)/stages)` (clamp); `a=(tan(pi*fck/rate_)-1)/(tan(pi*fck/rate_)+1)`; `y=a*x+s_k; s_k=x-a*y; x=y`. `fb_=x` (denormal-guard). `wet=x`; `out=(1-mix)*in + mix*wet`. NaN-clamp CVs.
  - `copy_state_from`: `ap_state_`, `fb_`, `phase_`.
  - `const ModuleInfo phaser_info{...}`. Register.

- [ ] **Step 4: run, verify pass**.

- [ ] **Step 5: commit** — `git add src/dsp/modules/phaser.cpp src/dsp/modules.h src/dsp/registry.cpp tests/test_phaser.cpp && git commit -m "feat: phaser mono swept-allpass modulation effect"`

---

### Task 3: docs, flanger recipe, demo patch, gates

**Files:** modify `docs/modules.md`, `docs/attribution.md`; create `patches/modfx.patch`.

- [ ] **Step 1: docs** — In `docs/modules.md`: add `chorus`/`phaser` to the Effects/I-O palette row; add a `## chorus` section (stereo, voices/delay/swing/shape, rate/depth/mix) and a `## phaser` section (mono, stages/spread/shape, rate/depth/feedback/mix, spread = coincident↔staggered notches); add the flanger recipe to `## Recipes`:

```text
**Flanger:** `lfo -> delay.time` at a short base with feedback:
`fl = delay { time: 0.003, feedback: 0.55, mix: 0.5, slew: 0.0005 }`,
`lfo -> fl.time : 0.0025`. Keep `slew` near 0 or the delay smoother smears
the sweep; a single positive-feedback line has no through-zero or hollow
negative-feedback flange (use a dedicated setup for those).
```

  In `docs/attribution.md`, add rows: `chorus.cpp` (Concept-generic — textbook modulated-delay chorus, implemented locally, no third-party code) and `phaser.cpp` (standard swept first-order allpass; coefficient from the project's existing bilinear/TPT filter lineage per the Zavalishin/Bristow-Johnson rows; implemented locally). Note both are decades-old effects with expired patents (factual, not legal advice) and no trademarks.

- [ ] **Step 2: demo patch** — Create `patches/modfx.patch` (a source → phaser → chorus → out chain), verify `build/ratsnest check patches/modfx.patch` clean and it renders non-silent/bounded with `--stats`.

- [ ] **Step 3: full gates** — `mise run test && mise run test-san && mise run ci` all green.

- [ ] **Step 4: commit** — `git add docs/modules.md docs/attribution.md patches/modfx.patch && git commit -m "docs: chorus/phaser reference, flanger recipe, demo patch"`

---

## Done criteria

- `chorus` (stereo) and `phaser` (mono, `stages`+`spread`) registered, tested, documented; `flanger` recipe present.
- Demo patch renders non-silent and bounded; `mise run test`/`test-san`/`ci` green; docs + attribution in the diff.
