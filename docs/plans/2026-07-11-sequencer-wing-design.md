# Sequencer Wing Utilities — Design

Complete the low-hanging sequencer utilities around `euclid` with six small,
single-purpose modules.

- `bern`: inputs `gate`, `prob`; config `seed`; outputs `a`, `b`. A seeded
  decision on each rising edge routes the full input gate width exclusively.
- `logic`: inputs `a`, `b`, `threshold`; config `op`
  (`and|or|xor|not|cmp`); output `out`. Boolean modes threshold both inputs;
  `cmp` emits whether `a > b`.
- `switch`: inputs `in1..in8`, `clock`, `reset`, `addr`, `n`; config `mode`
  (`seq|addr`); output `out`. Sequential mode advances on clock edges;
  addressed mode rounds `addr`; `n` clamps to 1..8.
- `turing`: inputs `clock`, `write`, `clear`, `len`, `prob`; config `seed`;
  outputs `out`, `pulse`, `bit1..bit8`. A 16-bit shift register feeds back with
  seeded mutation probability; write forces the incoming bit high, clear
  forces it low. `out` is the low byte normalized to 0..1.
- `stepseq`: configs `steps`, `gates`, `dir` (`forward|reverse|pingpong|random`)
  and `seed`; inputs `clock`, `reset`, `len`, `transpose`; outputs `cv`, `trig`,
  `gate`, `eoc`. CV holds between steps, trigger/EOC are one sample, and gate
  follows clock width.
- `tempo`: inputs `bpm`, `beats`; outputs `seconds`, `hz`, using
  `seconds = 60 * beats / bpm`. Defensive clamping keeps outputs finite.

All clock/reset inputs use rising-edge semantics. Coincident reset and clock
select step zero. Stateful modules copy all runtime and edge/RNG state during
hot reload. Tests cover deterministic behavior, boundary values, output width,
reset, and migration. Registry/header order remains append-only.
