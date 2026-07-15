# Attribution and Licensing Design

Date: 2026-07-14  
Status: approved

## Goal

License ratsnest under the MIT License and make the work it builds on visible,
traceable, and legally reusable. The repository should acknowledge distinctive
algorithms, papers, upstream implementations, and dependencies close to the
code that uses them and in one central provenance map.

The governing principle is that ratsnest is a synthesis of others' work:
attribution is part of the implementation, not an optional historical note.

## Audit findings

The repository currently has no top-level project license or third-party notice.
Research documents cite many sources, but citations are inconsistent at the
implementation sites and there is no single map from source files to their
provenance.

The known implementation licenses are compatible with releasing ratsnest under
MIT:

- Mutable Instruments Rings and the STM32 code in `pichenettes/eurorack` are
  MIT-licensed. The `modal` and `string` implementations substantially derive
  their structure, constants, mappings, and DSP primitives from Rings and
  stmlib, so Emilie Gillet's MIT notices must be retained.
- The cached-index SPSC design is influenced by Cameron Desrochers'
  `readerwriterqueue`, which uses the BSD-2-Clause license. Its notice must be
  retained conservatively even though ratsnest's queue is a small independent
  implementation of the pattern.
- PipeWire is an MIT-licensed runtime dependency, and doctest is an MIT-licensed
  test/build dependency. They are fetched or linked rather than vendored, but
  their role and licenses should still be discoverable.
- The Dattorro reverb, Giannoulis compressor curve, RBJ filters, ADAA
  waveshaping, polyBLEP oscillator, TPT filters, Euclidean rhythms,
  Karplus-Strong synthesis, and xorshift generators are locally implemented
  from published descriptions or established algorithms. They require clear
  scholarly and technical attribution, not a claim that their papers license
  the ratsnest source.

No tracked audio, images, fonts, or other media assets require a separate
license notice. Existing untracked files are outside this audit and will not be
modified or added.

## Chosen approach

Use layered attribution rather than a bare license or full repository-wide
REUSE/SPDX conversion.

### Project license

Add a root `LICENSE` containing the standard MIT text for:

> Copyright (c) 2026 Ben Sandbrook

The project license covers original ratsnest work. Third-party portions remain
under their respective notices.

### Legal notices

Add `THIRD_PARTY_NOTICES.md` with:

- the exact Mutable Instruments/Rings and stmlib MIT copyright and permission
  notices;
- the exact BSD-2-Clause notice for readerwriterqueue;
- the MIT notices for PipeWire and doctest, labeled as external runtime and
  test dependencies rather than vendored code;
- a short mapping from every notice to the files or build target it concerns.

Keeping full notices in a root document makes them travel with source and binary
distributions. Source comments will point to this file where code is derived.

### Provenance map

Add `docs/attribution.md` as the human-readable source of truth. Each entry will
identify:

1. the ratsnest source files or modules;
2. whether the relationship is code-derived, paper-derived, concept-inspired,
   or an external dependency;
3. the upstream author/project or paper citation and stable URL;
4. the applicable software license when an upstream implementation informed
   the code;
5. the nature of ratsnest's adaptation.

The initial map will cover at least:

- Mutable Instruments Rings and stmlib;
- Karplus and Strong's plucked-string synthesis;
- Jon Dattorro's plate reverberator;
- Giannoulis, Massberg, and Reiss's compressor curve;
- Robert Bristow-Johnson's Audio EQ Cookbook;
- Parker, Zavalishin, and Le Bivic's ADAA method and Martin Vicanek's
  waveshaper curves;
- polyBLEP oscillator construction;
- topology-preserving-transform SVF and ladder filters;
- Toussaint's Euclidean-rhythm framing;
- George Marsaglia's xorshift generator;
- Cameron Desrochers' readerwriterqueue and PipeWire's ring-buffer memory
  ordering;
- PipeWire and doctest as external dependencies.

The two allpass half-band coefficients in `src/dsp/oversample.h` have no source
recorded in repository history and no matching publication or upstream code was
identified during the audit. The provenance map and source comment will state
that honestly. They will not be attributed speculatively, and DSP behavior will
not be changed as part of this documentation task.

### Source-level attribution

Add or strengthen concise comments at the implementation sites. Comments will
name the algorithm or upstream source, include a stable reference, classify the
relationship, and direct readers to `docs/attribution.md` and
`THIRD_PARTY_NOTICES.md` when a software license applies.

The most important sites are:

- `src/dsp/resonate.h`, `src/dsp/modules/modal.cpp`, and
  `src/dsp/modules/string.cpp`;
- `src/dsp/modules/reverb.cpp`;
- `src/dsp/modules/compressor.cpp` and `src/dsp/biquad.h`;
- `src/dsp/waveshape.h` and `src/dsp/oversample.h`;
- `src/dsp/modules/vco.cpp`, `svf.cpp`, `ladder.cpp`, and `euclid.cpp`;
- xorshift implementations in sequencing, noise, random, and grains modules;
- `src/rt/spsc.h`.

Comments should remain short. Full bibliographic information belongs in the
central map so it does not drift across duplicated blocks.

### Future contribution rule

Update `CLAUDE.md` with a provenance rule for future work:

- before adapting code or a distinctive algorithm, record its source and
  license;
- do not copy code whose license is absent, unclear, or incompatible;
- retain required copyright and permission notices;
- add a nearby source comment and an entry in `docs/attribution.md`;
- describe material modifications to upstream code;
- keep dependency versions and license classifications current when build
  inputs change.

## Verification

Extend `tests/test_docs.cpp` with an attribution compliance test. It will locate
the repository from `__FILE__` and verify that:

- `LICENSE`, `THIRD_PARTY_NOTICES.md`, and `docs/attribution.md` exist;
- the project license names Ben Sandbrook and the MIT grant;
- required third-party copyright holders and license identifiers are present;
- the provenance map contains entries for the critical algorithm families and
  dependencies;
- code-derived Rings files direct readers to the notices.

The test is deliberately a guard against accidental deletion or omission, not a
legal-compliance engine. Normal build, unit, sanitizer, and warnings-as-errors
gates will verify that comment and documentation changes did not affect runtime
behavior.

## Non-goals

- No DSP behavior, constants, APIs, patch files, or generated audio will change.
- No claim is made that academic citation alone resolves patents, trademarks,
  or every jurisdiction-specific legal question.
- No repository-wide SPDX header conversion, SBOM generator, or packaging
  system will be introduced.
- Existing research reports remain historical evidence; the new provenance map
  is the concise maintained index rather than a rewrite of those reports.

