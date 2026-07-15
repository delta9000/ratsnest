# Attribution and Licensing Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Release modsynth under MIT while preserving third-party license notices and making every distinctive borrowed or paper-derived algorithm traceable from its implementation.

**Architecture:** A root project license and third-party notice file provide the legal layer; `docs/attribution.md` provides a maintained provenance map; concise source comments connect implementation sites to that map. A documentation regression test prevents the critical license and attribution records from silently disappearing, and `CLAUDE.md` makes provenance review part of future development.

**Tech Stack:** Markdown legal/provenance documents, C++20 source comments, doctest documentation tests, CMake/mise verification.

---

### Task 1: Add a failing attribution-compliance test

**Files:**
- Modify: `tests/test_docs.cpp`

**Step 1: Add reusable repository-file helpers**

Replace the one-off file loading in `tests/test_docs.cpp` with these helpers in
an anonymous namespace:

```cpp
namespace {
const auto kRoot = std::filesystem::path(__FILE__).parent_path().parent_path();

std::string read_repo_file(const std::filesystem::path& relative) {
  std::ifstream file(kRoot / relative);
  INFO("reading " << (kRoot / relative));
  REQUIRE(file.good());
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

void check_contains(const std::string& text, const std::string& needle) {
  CAPTURE(needle);
  CHECK(text.find(needle) != std::string::npos);
}
}  // namespace
```

Update the existing module-reference test to use:

```cpp
const std::string text = read_repo_file("docs/modules.md");
```

**Step 2: Add the failing compliance test**

Append:

```cpp
TEST_CASE("license and attribution records cover implementation provenance") {
  const std::string license = read_repo_file("LICENSE");
  check_contains(license, "Copyright (c) 2026 Ben Sandbrook");
  check_contains(license, "Permission is hereby granted");

  const std::string notices = read_repo_file("THIRD_PARTY_NOTICES.md");
  for (const char* required : {
           "Copyright 2015 Emilie Gillet",
           "Copyright 2012 Emilie Gillet",
           "BSD-2-Clause",
           "Copyright (c) 2013-2021, Cameron Desrochers",
           "Copyright © 2018 Wim Taymans",
           "Copyright (c) 2016-2023 Viktor Kirilov",
       }) {
    check_contains(notices, required);
  }

  const std::string attribution = read_repo_file("docs/attribution.md");
  for (const char* required : {
           "Mutable Instruments Rings", "Karplus", "Dattorro",
           "Giannoulis", "Bristow-Johnson", "Parker", "Vicanek",
           "polyBLEP", "Zavalishin", "Simper", "Toussaint",
           "Marsaglia", "readerwriterqueue", "PipeWire", "doctest",
       }) {
    check_contains(attribution, required);
  }

  for (const char* source : {
           "src/dsp/resonate.h",
           "src/dsp/modules/modal.cpp",
           "src/dsp/modules/string.cpp",
           "src/rt/spsc.h",
       }) {
    const std::string text = read_repo_file(source);
    check_contains(text, "docs/attribution.md");
    check_contains(text, "THIRD_PARTY_NOTICES.md");
  }
}
```

**Step 3: Build and run the focused test to verify it fails**

Run:

```bash
cmake --build build --target mstests
./build/mstests --test-case="license and attribution records cover implementation provenance"
```

Expected: FAIL because `LICENSE` does not exist yet.

Do not commit the deliberately failing state.

---

### Task 2: Add the project license and exact third-party notices

**Files:**
- Create: `LICENSE`
- Create: `THIRD_PARTY_NOTICES.md`
- Test: `tests/test_docs.cpp`

**Step 1: Add the modsynth MIT license**

Create `LICENSE` with the canonical MIT text and this copyright line:

```text
MIT License

Copyright (c) 2026 Ben Sandbrook

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

**Step 2: Add the third-party notice structure and mappings**

Create `THIRD_PARTY_NOTICES.md` with this structure:

```markdown
# Third-party notices

modsynth is licensed under the MIT License. Portions derive from or are
informed by the software below and remain subject to their original notices.
Paper and algorithm acknowledgements that do not involve copied software are
listed in `docs/attribution.md`.

## Mutable Instruments Rings and stmlib — MIT

Applies to `src/dsp/resonate.h`, `src/dsp/modules/modal.cpp`, and
`src/dsp/modules/string.cpp`.

[Rings' exact 2015 Emilie Gillet MIT notice]

[stmlib's exact 2012 Emilie Gillet MIT notice]

## readerwriterqueue — BSD-2-Clause

The cached-index optimization in `src/rt/spsc.h` is informed by Cameron
Desrochers' readerwriterqueue.

[readerwriterqueue's exact BSD-2-Clause notice]

## PipeWire — MIT

PipeWire is an external runtime/link dependency and is not vendored.

[PipeWire's exact 2018 Wim Taymans MIT notice]

## doctest — MIT

doctest v2.4.11 is fetched at test-build time and is not vendored.

[doctest's exact 2016-2023 Viktor Kirilov MIT notice]
```

Copy the notices verbatim from these audited primary/local sources:

- Rings source headers:
  `https://github.com/pichenettes/eurorack/blob/master/rings/dsp/resonator.cc`
- stmlib license:
  `https://github.com/pichenettes/stmlib/blob/e3bd7c9cc00e4364166f9905c0509b6ffd0535ec/LICENSE`
- readerwriterqueue license:
  `https://github.com/cameron314/readerwriterqueue/blob/master/LICENSE.md`
- installed PipeWire license: `/usr/share/licenses/pipewire/COPYING`
- fetched doctest v2.4.11 license: `build/_deps/doctest-src/LICENSE.txt`

Do not paraphrase the legal text. Preserve each copyright line, grant,
conditions, and disclaimer.

**Step 3: Run the focused test**

Run:

```bash
cmake --build build --target mstests
./build/mstests --test-case="license and attribution records cover implementation provenance"
```

Expected: FAIL because `docs/attribution.md` does not exist yet. The license and
notice assertions before that point pass.

Do not commit the still-failing state.

---

### Task 3: Add the central provenance map

**Files:**
- Create: `docs/attribution.md`
- Test: `tests/test_docs.cpp`

**Step 1: Create the classification and maintenance policy**

Start `docs/attribution.md` with:

```markdown
# Attribution and implementation provenance

modsynth stands on the work of many researchers and open-source developers.
This document maps distinctive implementations to their sources and describes
how they were adapted. Legal notices for software-derived portions are retained
in [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md); modsynth's project
license is in [`LICENSE`](../LICENSE).

Relationships are classified as:

- **Code-derived:** structure, constants, or implementation details were
  adapted from upstream source; its software license and notice apply.
- **Paper-derived:** implemented locally from a published technical
  description; cite the authors, but do not imply their paper licenses this
  source code.
- **Concept-inspired:** product or project semantics informed the design, while
  the implementation was written locally.
- **External dependency:** linked or fetched software that is not vendored.
```

**Step 2: Add the implementation provenance table**

Add a table with columns `modsynth code`, `relationship`, `source`, and
`adaptation`. Include these entries and references:

- `src/dsp/resonate.h`, `modal.cpp`, `string.cpp`: **Code-derived** from Emilie
  Gillet's Mutable Instruments Rings and stmlib, MIT; identify modal resonator,
  string dispersion/damping, plucker, SVF, cosine recurrence, and delay/allpass
  primitives; link the official Rings repository and relevant source paths.
- `string.cpp`: **Paper-derived in ancestry and code-derived through Rings**
  from Kevin Karplus and Alex Strong, “Digital Synthesis of Plucked-String and
  Drum Timbres,” *Computer Music Journal* 7(2), 1983.
- `reverb.cpp`: **Paper-derived** from Jon Dattorro, “Effect Design Part 1:
  Reverberator and Other Filters,” *JAES* 45(9), 1997; note that topology,
  lengths, coefficients, and taps are transcribed and sample-rate scaled.
- `compressor.cpp`: **Paper-derived** from Dimitrios Giannoulis, Michael
  Massberg, and Joshua D. Reiss, “Digital Dynamic Range Compressor Design—A
  Tutorial and Analysis,” *JAES* 60(6), 2012; name the log-domain soft-knee
  curve and ballistics.
- `biquad.h` and `eq.cpp`: **Paper/technical-note-derived** from Robert
  Bristow-Johnson's Audio EQ Cookbook, using the maintained W3C Audio EQ
  Cookbook URL.
- `waveshape.h`: **Paper-derived** ADAA from Julian D. Parker, Vadim
  Zavalishin, and Efflam Le Bivic, DAFx-16; curves after Martin Vicanek's
  “Antiderivative Antialiasing for Memoryless Nonlinearities.”
- `vco.cpp`: **Paper-derived family** for the two-sample polyBLEP correction;
  cite Vesa Välimäki, Jussi Pekonen, and Juhan Nam, JASA 131(1), 2012,
  DOI `10.1121/1.3651227`; explicitly say the compact formula is locally
  written and no source implementation was copied.
- `svf.cpp`, `ladder.cpp`, and the SVF in `resonate.h`:
  **Paper/technical-note-derived** from Vadim Zavalishin's *The Art of VA
  Filter Design* and Andrew Simper's Cytomic trapezoidal SVF notes.
- `euclid.cpp`: **Concept/paper-derived** from Godfried Toussaint, “The
  Euclidean Algorithm Generates Traditional Musical Rhythms,” 2005; explain
  that modsynth uses a constant-time modular bucket/Bresenham-style test rather
  than copying Bjorklund code.
- xorshift sites (`noise.cpp`, `random.cpp`, `bern.cpp`, `turing.cpp`,
  `stepseq.cpp`, `grains.cpp`, and `resonate.h`): **Paper-derived** from George
  Marsaglia, “Xorshift RNGs,” *Journal of Statistical Software* 8(14), 2003.
- `src/rt/spsc.h`: **Implementation-influenced** by Cameron Desrochers'
  readerwriterqueue cached peer indices (BSD-2-Clause) and PipeWire's
  acquire/release ring-buffer protocol (MIT); distinguish influence from a
  verbatim port.
- `bern.cpp`: **Concept-inspired** by Mutable Instruments Branches' Bernoulli
  gate routing; no Branches firmware code copied.
- `turing.cpp`: **Concept-inspired** by Tom Whitwell's Music Thing Modular
  Turing Machine; no firmware code copied.
- PipeWire: **External dependency**, version discovered through pkg-config,
  linked through `PkgConfig::PIPEWIRE`, MIT.
- doctest v2.4.11: **External dependency**, fetched by CMake for `mstests`, MIT.

Use stable primary-source links where available:

- `https://github.com/pichenettes/eurorack`
- `https://github.com/pichenettes/stmlib`
- `https://www.w3.org/TR/audio-eq-cookbook/`
- `https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf`
- `https://research.aalto.fi/en/publications/perceptually-informed-synthesis-of-bandlimited-classical-waveform/`
- `https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.0.0a.pdf`
- `https://www.cytomic.com/files/dsp/SvfLinearTrapOptimised.pdf`
- `https://www.jstatsoft.org/article/view/v008i14`
- `https://github.com/cameron314/readerwriterqueue`
- `https://docs.pipewire.org/`
- `https://github.com/doctest/doctest/tree/2.4.11`

Retain the existing direct paper URLs in
`docs/research/2026-07-11-mastering-dsp.md` for Dattorro, Giannoulis, and the
ADAA references when a publisher landing page is unavailable.

**Step 3: Record unresolved oversampler coefficient provenance**

Add a separate “Unresolved provenance” section:

```markdown
## Unresolved provenance

`src/dsp/oversample.h` contains the two first-order allpass coefficients
`0.117565365538` and `0.541450201656`. Repository history records them at their
initial introduction but does not name a design tool, paper, or upstream
implementation, and the attribution audit found no exact source match. They
are therefore documented as **source unknown**, not attributed speculatively.
Future replacement or regeneration should record its design method and license
before changing these values.
```

**Step 4: Run the focused test**

Run:

```bash
cmake --build build --target mstests
./build/mstests --test-case="license and attribution records cover implementation provenance"
```

Expected: FAIL because the four code-derived source files do not yet contain
both documentation pointers.

Do not commit the still-failing state.

---

### Task 4: Connect source implementations to the provenance records

**Files:**
- Modify: `src/dsp/resonate.h`
- Modify: `src/dsp/modules/modal.cpp`
- Modify: `src/dsp/modules/string.cpp`
- Modify: `src/dsp/modules/reverb.cpp`
- Modify: `src/dsp/modules/compressor.cpp`
- Modify: `src/dsp/biquad.h`
- Modify: `src/dsp/waveshape.h`
- Modify: `src/dsp/oversample.h`
- Modify: `src/dsp/modules/vco.cpp`
- Modify: `src/dsp/modules/svf.cpp`
- Modify: `src/dsp/modules/ladder.cpp`
- Modify: `src/dsp/modules/euclid.cpp`
- Modify: `src/dsp/modules/bern.cpp`
- Modify: `src/dsp/modules/turing.cpp`
- Modify: `src/dsp/modules/noise.cpp`
- Modify: `src/dsp/modules/random.cpp`
- Modify: `src/dsp/modules/stepseq.cpp`
- Modify: `src/dsp/modules/grains.cpp`
- Modify: `src/rt/spsc.h`
- Test: `tests/test_docs.cpp`

**Step 1: Strengthen the Mutable Instruments code-derived headers**

Use this four-line header in `src/dsp/resonate.h`, before includes:

```cpp
// Shared physical-resonator DSP core for the modal/string modules.
// Code-derived from Mutable Instruments Rings and stmlib (MIT), substantially
// adapted for modsynth. See docs/attribution.md and THIRD_PARTY_NOTICES.md.
// Upstream: https://github.com/pichenettes/eurorack/tree/master/rings/dsp
```

Add this near the top of both `modal.cpp` and `string.cpp`:

```cpp
// Code-derived from Mutable Instruments Rings (MIT), substantially adapted.
// See docs/attribution.md and THIRD_PARTY_NOTICES.md.
```

Keep existing detailed Rings comments; remove only redundant wording.

**Step 2: Add concise paper/technical-note pointers**

Add or update comments so each file names the implementation relationship and
ends with `See docs/attribution.md.`:

- `reverb.cpp`: Dattorro 1997 topology/constants/taps, paper-derived.
- `compressor.cpp`: Giannoulis/Massberg/Reiss log-domain soft-knee curve,
  paper-derived.
- `biquad.h`: Bristow-Johnson/W3C Audio EQ Cookbook coefficients.
- `waveshape.h`: Parker/Zavalishin/Le Bivic ADAA and Vicanek curves.
- `vco.cpp`: Välimäki/Pekonen/Nam polyBLEP family, locally written formula.
- `svf.cpp`: Zavalishin/Simper trapezoidal-integrator TPT SVF.
- `ladder.cpp`: Zavalishin topology-preserving four-stage ladder with
  analytically solved linear feedback.
- `euclid.cpp`: Toussaint Euclidean-rhythm framing; modular bucket test, not a
  Bjorklund code port.

Do not imply that a paper carries a software license.

**Step 3: Document the unresolved half-band coefficients in source**

Immediately above `Halfband2x` in `src/dsp/oversample.h`, add:

```cpp
// The design provenance of the two coefficients below was not recorded when
// this file was introduced and could not be recovered during the 2026-07-14
// attribution audit. Do not attribute them speculatively; see the unresolved
// entry in docs/attribution.md before replacing or regenerating them.
```

**Step 4: Add implementation-influence and concept-inspiration pointers**

At the top of `src/rt/spsc.h`, add:

```cpp
// The cached peer-index optimization is implementation-influenced by Cameron
// Desrochers' readerwriterqueue (BSD-2-Clause), and the acquire/release protocol
// follows PipeWire's MIT-licensed ring buffer. This is not a verbatim port.
// See docs/attribution.md and THIRD_PARTY_NOTICES.md.
```

Add concise comments to `bern.cpp` and `turing.cpp` identifying Mutable
Instruments Branches and Music Thing Modular Turing Machine as concept
inspirations, explicitly stating that no firmware code was copied.

**Step 5: Mark the xorshift implementation family**

Add one short Marsaglia citation beside each distinct xorshift implementation
in `noise.cpp`, `random.cpp`, `bern.cpp`, `turing.cpp`, `stepseq.cpp`,
`grains.cpp`, and `resonate.h`:

```cpp
// Marsaglia xorshift32; see docs/attribution.md.
```

Do not duplicate the full citation.

**Step 6: Run the focused and complete unit tests**

Run:

```bash
cmake --build build --target mstests
./build/mstests --test-case="license and attribution records cover implementation provenance"
ctest --test-dir build --output-on-failure
```

Expected: focused test PASS; complete unit/CLI suite PASS.

**Step 7: Commit the legal records, provenance map, regression test, and source comments**

```bash
git add LICENSE THIRD_PARTY_NOTICES.md docs/attribution.md tests/test_docs.cpp \
  src/dsp/resonate.h src/dsp/biquad.h src/dsp/waveshape.h \
  src/dsp/oversample.h src/dsp/modules src/rt/spsc.h
git commit -m "docs: license project and preserve implementation provenance"
```

Before committing, inspect `git diff --cached --stat` and confirm no patch,
audio, build, or unrelated user file is staged.

---

### Task 5: Make provenance a future development invariant

**Files:**
- Modify: `CLAUDE.md`
- Test: `tests/test_docs.cpp`

**Step 1: Extend the documentation test for the policy**

In the attribution test, add:

```cpp
const std::string guidance = read_repo_file("CLAUDE.md");
check_contains(guidance, "## Attribution and licensing");
check_contains(guidance, "docs/attribution.md");
check_contains(guidance, "THIRD_PARTY_NOTICES.md");
check_contains(guidance, "unclear or incompatible");
```

**Step 2: Run the focused test to verify it fails**

Run:

```bash
cmake --build build --target mstests
./build/mstests --test-case="license and attribution records cover implementation provenance"
```

Expected: FAIL because `CLAUDE.md` lacks the new section.

**Step 3: Add the repository policy**

Append this section to `CLAUDE.md`:

```markdown
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
```

**Step 4: Run the focused test**

Run:

```bash
cmake --build build --target mstests
./build/mstests --test-case="license and attribution records cover implementation provenance"
```

Expected: PASS.

**Step 5: Commit the policy and its regression check**

```bash
git add CLAUDE.md tests/test_docs.cpp
git commit -m "docs: require provenance for borrowed implementations"
```

---

### Task 6: Verify licensing scope and repository integrity

**Files:**
- Verify only; no planned modifications

**Step 1: Check whitespace and the complete diff**

Run:

```bash
git diff --check HEAD~2
git diff --stat HEAD~2
git status --short
```

Expected: no whitespace errors; only the planned legal, documentation, test,
and source-comment files are changed across the implementation commits. The
pre-existing untracked `f5ble-dj-mix-master.flac`,
`patches/power-metal1.patch`, and `test` remain untracked and untouched.

**Step 2: Verify no executable code changed**

Run:

```bash
git diff --word-diff=porcelain HEAD~2 -- 'src/**/*.cpp' 'src/**/*.h'
```

Expected: changes are comments only. Review every non-comment token if the
output shows one.

**Step 3: Run the normal test gate**

Run:

```bash
mise run test
```

Expected: build succeeds and all ctest cases pass.

**Step 4: Run sanitizer and warnings-as-errors gates**

Run:

```bash
mise run test-san
mise run ci
```

Expected: sanitizer suite passes; warnings-as-errors build and all tests pass.

**Step 5: Inspect the committed legal records**

Run:

```bash
git show --check --stat HEAD~1..HEAD
git log -3 --oneline
```

Expected: the design commit and two implementation commits are present, with
no unrelated tracked content.

