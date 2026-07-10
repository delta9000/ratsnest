# Research archive

Web research conducted 2026-07-10 during the design phase, preserved verbatim.
These reports are the evidence behind the design decisions in
`docs/superpowers/specs/2026-07-10-modsynth-design.md`. Consult them when
designing modules in later phases — they contain per-module I/O tables,
parameter semantics, and source links the spec only summarizes.

| Report | What it informed |
|---|---|
| [module-semantics.md](2026-07-10-module-semantics.md) | Deep I/O + parameter semantics of structurally unusual Eurorack modules (Maths, Plaits, Marbles, Beads, Pam's, Metropolix, Turing Machine, switches, quantizers, ochd). Drove: array params, no-normalling principle, seed convention, "expanders dissolve into cables." Primary reference for designing `func`, `quant`, `euclid`, `turing`, `random`, `stepseq`. |
| [popular-modules.md](2026-07-10-popular-modules.md) | ModularGrid top ~120 by ubiquity + trending list. Drove: which hardware idioms the palette must cover; DROID identified as prior art; confirmed multifunction modules are panel-economics workarounds. |
| [software-core-sets.md](2026-07-10-software-core-sets.md) | Core module sets of VCV Fundamental, Cardinal, Pd vanilla, Reaktor Blocks, Bitwig Grid, SuperCollider, Puckette pedagogy. Drove: the consensus-10, `math`/`xfade`/`sh`/`scope` additions, clock+quantizer-on-day-one rule, the polyphony model decision (per-voice instancing), signal conventions. |
| [minimal-rack-wisdom.md](2026-07-10-minimal-rack-wisdom.md) | Semi-modular voice architectures (0-Coast, Mother-32, Neutron, MiniBrute 2), utility-hierarchy forum wisdom, drums-without-drum-modules techniques, generative essentials, regret threads. Drove: west-coast trio (`fold`/`func`/`lpg`) in Phase 1, `bern`, utilities-outnumber-sources weighting, pingability-over-drum-modules, knob-per-function principle. |
| [droid-prior-art.md](2026-07-10-droid-prior-art.md) | The closest existing patch-as-text system: DROID's language, five years of user feedback, and vendor response pattern. Validates: named instances (fixes DROID's positional-state flaw), cable depths as the A·B+C idiom, no inline expressions. Adds: freeze-the-grammar/grow-the-vocabulary principle, `check` warnings for unreachable modules, introspection-is-the-product (runtime port watch in 1b), "CV-driven values are never state" for the GUI era. |
