# Polyphony: `alloc` + `voice` groups — Design

2026-07-11. Status: designed, not implemented. Refines §6 and §3.5 of
`2026-07-10-modsynth-design.md` (per-voice patch instancing, decided there;
poly cables rejected there). Built before `midi` so midi's port surface is
designed against a proven voice mechanism, not the other way around.

## 1. Shape of the solution

Two pieces, deliberately unequal:

- **`alloc`** is an ordinary module. It distributes a mono note stream
  (gate/voct/vel) across N indexed output banks — `gate1..8`, `voct1..8`,
  `vel1..8` — the same port-bank idiom `turing.bit1..8` already uses. It is
  fully usable without any language feature: polyphony from a sequencer works
  the day it lands.
- **`voice`** is compile-time expansion, not a runtime concept. A voice group
  stamps its member modules once per voice and rewrites cables. After
  expansion the graph contains only ordinary nodes; the engine, hot reload,
  `check`, and `scope` need no changes and no new rules.

Grammar cost: zero new statement types. Declared names may contain a dot
(`lead.osc = vco`); `lead.osc.out` parses by longest-match on declared module
names. Nothing else in the grammar changes.

## 2. `alloc`

| | |
|---|---|
| configs | `n` 1..8 (default 4), `mode` (`rr` \| `unison`) |
| inputs | `gate` 0, `voct` 0, `vel` 1 |
| outputs | `gate1..gate8`, `voct1..voct8`, `vel1..vel8`, `spread1..spread8` |

Banks above `n` output 0. `spread<i>` is a constant: voices evenly spaced
over `-1..+1` (`spread = 2(i-1)/(n-1) - 1`; 0 when `n` = 1). One cable turns
it into unison detune (`poly.spread -> osc.v_oct : 0.01`) or a stereo spread.

Claim semantics (`rr`):

- A rising edge on `gate` claims the next voice round-robin and copies
  `vel` at the moment of the claim.
- While the input gate is high and voice *i* is still the newest claim,
  `gate<i>` is high and `voct<i>` **tracks** the input — legato slides work.
- When superseded or released, `voct<i>` **holds** its last value, so release
  tails keep their pitch. `vel<i>` holds until the next claim of that voice.
- Reclaiming a voice whose gate is still high inserts a one-sample low gap so
  downstream envelopes retrigger (defensive for alloc's mono input, where a
  rising edge always implies the prior sample was low; observable in bank
  producers with note identity, see the midi design in §5).

A mono stream can only express one *held* note at a time, so this is strum
polyphony: each trigger takes a fresh voice and older voices ring out through
their releases. That is the honest semantic for sequencer sources; policies
that need note identity (`lowest`, oldest-steal) arrive with `midi` (§5).

`unison`: every active voice claims on every edge; all gates follow the input
gate; all vocts track. `spread` is the point.

State (claim order, held values, previous gate) migrates across hot reload
like any module.

## 3. `voice` groups

```text
poly = alloc { n: 4, mode: rr }
seq.trig -> poly.gate
seq.cv  -> poly.voct

lead = voice { notes: poly }
lead.osc = vco { wave: saw }
lead.env = adsr { attack: 5ms }
lead.amp = vca { gain: 0 }
poly.voct -> lead.osc.v_oct   # bound bank, bare name: per-instance
poly.gate -> lead.env.gate    # per-instance
vib -> lead.osc.v_oct : 0.01  # outside module: broadcast to all instances
lead.osc -> lead.amp.in       # inside -> inside: instance-local
lead.env -> lead.amp.cv
lead.amp -> mix.in1 : 0.4     # inside -> outside: straight sum of voices
```

- **Declaration.** `name = voice { notes: <module> }`. `notes:` must name a
  module exposing `gate<i>`/`voct<i>` banks. Voice count is inherited from
  the bound module's `n`; an explicit `n:` must match it or it is an error.
- **Members.** Every declaration named `name.<member>` belongs to the group.
  Expansion stamps each member once per voice as `name.<member>#i`. `#`
  starts a comment in source, so instance names can never collide with
  user names. `scope` members yield per-instance probes; watch sees
  `lead.osc#2` like any node.
- **Cable resolution**, by endpoint location:
  1. inside → inside (same group): instance-local, one cable per voice.
  2. bound module's **bare bank name** read inside (`poly.voct`,
     `poly.spread`): resolves to `<bank><i>` per instance, for every output
     family `<bank>1..<bank>N` the bound module exposes. This shadows
     nothing — `alloc` has no mono *output* named `voct`, so that spelling
     is an error everywhere else, today and after this change.
  3. outside → inside: broadcast; the same source feeds every instance.
  4. inside → outside: the outside port receives the **straight sum** of all
     instances. No hidden `1/n` normalization — modulation already sums onto
     ports everywhere else, and gain staging stays the author's job via
     cable depth.
  Explicit indexed reads (`poly.voct3`, `lead.amp#2` is *not* addressable —
  indexed bank ports yes, instance nodes no) remain legal anywhere and mean
  that single stream.
- **Hot reload.** Instance names are stable, so per-voice state migrates.
  Changing `n` (via the allocator) is a topology change: surviving instances
  migrate, dropped instances vanish, new instances start fresh — consistent
  with §3.3.
- **Errors** (all at compile, line:col): `notes:` module lacking the banks;
  `n:` mismatch; voice group nested in a voice group (a member may not
  itself be a `voice`); a group with zero members. A dotted declaration
  whose prefix is *not* a voice group is an ordinary module — dotted names
  are general (§1), so a typo'd group name compiles as plain modules; the
  planned `check` unreachable-module warning is the net for that.

## 4. Signal conventions

Banks obey the existing conventions: gates rise through 0.5, `voct` is
v/oct, `vel` unipolar 0..1. `spread` is bipolar CV. Nothing new.

## 5. `midi` interaction (next, separate design)

`midi` ships its own banks (`gate1..n`, `voct1..n`, `vel1..n`) plus mono
conveniences, and binds directly: `keys = voice { notes: midi1 }`. Because it
has real note identity it can implement `lowest` and oldest-steal properly.
`alloc` stays the sequencer-side allocator; the `voice` mechanism is
indifferent to which feeds it. Anything with banks qualifies —
`turing.bit1..8` gating eight voices is legal and encouraged.

## 6. Testing

- **alloc unit:** rr claim order; voct tracks while newest, holds after;
  vel latch; one-sample retrigger gap on reclaim; unison gates/vocts;
  spread values for n = 1, 4, 8; banks above n stay 0; state migration
  (mid-claim) via copy_state_from.
- **graph expansion:** node count = members × n; the four cable-resolution
  cases produce exactly the expected cable sets; bare-bank resolution only
  applies to the bound module; each error case reports line:col.
- **reload:** param tweak inside a group glides; n change migrates surviving
  instances (phase continuity test, as test_migrate does for mono modules).
- **integration:** a seeded strum patch (stepseq → alloc → 3-voice group)
  renders deterministically; render --stats confirms overlapping release
  tails (nonzero RMS across note boundaries).

## 7. Deferred

- `lowest` / steal modes (need note identity — arrive with `midi`).
- Dynamic voice count without topology reload.
- Auto pan-spread (patchable: `spread` + two VCAs into `mix`).
- Voice-group subpatches (§3.5 include/subpatch feature composes here later:
  a subpatch file instanced under a namespace).
