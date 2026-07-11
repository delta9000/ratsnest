# `midi` — Design

2026-07-11. Status: designed, not implemented. Companion to
`2026-07-11-voice-alloc-design.md` (built in the same batch; midi is the
second producer of that spec's port-bank convention, and the two are tested
against the same conformance suite). Fulfills Phase 2's `midi` entry.

## 1. Module surface

| | |
|---|---|
| configs | `channel` 0 = omni (default) \| 1..16; `voices` 1..8 (default 1); `mode` (`last` \| `lowest`); `bend` semitones (default 2); `cc` array of up to 4 controller numbers (default `[]`) |
| inputs | none |
| outputs | banks `gate1..8`, `voct1..8`, `vel1..8`; `bend` (bipolar, post-range, also folded into every voct); `at` (channel pressure, 0..1); `cc1..cc4` (0..1) |

There are deliberately **no** mono convenience outputs named `gate`/`voct`/
`vel`: those bare spellings are reserved by the voice-group bank-resolution
rule ("shadows nothing" in the voice spec holds only if no bank producer has
real outputs by those names). A mono synth is `voices: 1` and `mid.gate1` —
explicit, per design rule 4.

- `voct` = `(note − 57) / 12` — A3 (MIDI 57) is 0.0 at 220 Hz, per spec §4.
  Pitch bend (scaled by `bend` semitones) is added into all voct outputs;
  the separate `bend` output exists for routing bend elsewhere.
- `cc<k>` carries the controller named by the k-th entry of the `cc` config
  (`cc: [74, 1]` → `cc1` is controller 74, `cc2` is mod wheel), scaled to
  0..1 and smoothed by a fixed ~3 ms one-pole to kill 7-bit zipper; longer
  lags are `func`'s job. Unmapped slots output 0.
- Sustain (CC 64) is honored: while the pedal is held, note-offs defer until
  pedal release. No config; keyboards without a pedal never send it.
- Out-of-range config values clamp in `init` (house pattern; compile-time
  range diagnostics for numeric configs are a possible later `check`
  warning, not part of this design): `cc` entries clamp to 0..127 and only
  the first 4 are read; `voices` 1..8; `channel` 0..16; `bend` 0..24.

## 2. Voice handling (real note identity)

Unlike `alloc`'s strum semantics, midi tracks which key each voice sounds:

- Note-on claims a free voice (round-robin over free voices); if none are
  free, **steal the oldest** sounding voice. Stealing or reclaiming inserts
  the same one-sample gate gap `alloc` guarantees, so envelopes retrigger.
- Note-off (or sustain-pedal release for deferred offs) drops that note's
  gate; voct and vel hold, so release tails keep pitch — same hold rule as
  `alloc`.
- `mode` applies at `voices: 1`: `last` = newest key wins, previous held
  keys restore on release (classic mono synth); `lowest` = lowest held key
  wins (bass pedal style).
- Velocity 0 note-ons are note-offs (running-status keyboards).
- All tracker state (held keys, order, pedal, cc values, bend) migrates
  across hot reload via `copy_state_from`.

Binding is the whole point: `keys = voice { notes: mid }` works because the
banks obey the exact convention `alloc` established. `lowest`/steal live
here and not in `alloc` because only midi knows note identity.

## 3. Transport and RT discipline

- Input arrives on a PipeWire **control port** added to the existing filter
  (`pipewire_out`), named `midi-in`; users wire a controller to it with any
  patchbay tool. No new dependency (design rule 7); ALSA raw/seq is not used.
- The PipeWire process callback parses the control buffer's pod sequence and
  pushes raw 3-byte events into a fixed-size SPSC ring (`rt/spsc.h`, the
  existing bridge idiom). Parse failures drop the event, never block.
  NOTE: the control payload is `SPA_CONTROL_Midi` (raw bytes) on older
  PipeWire and UMP packets on newer; the implementation targets whatever the
  build machine's headers provide and this is verified during
  implementation, not assumed.
- At each block start the runner drains the ring and hands events to midi
  modules through a narrow `on_midi(const uint8_t*, size)` hook — RT-safe,
  no locks, no allocation. v1 quantizes events to block boundaries
  (≤ ~5 ms at 256/48k); sample-accurate offsets are a later refinement and
  the hook signature does not preclude them.
- Offline `render` has no transport: midi modules output silence/defaults.
  Patches with midi remain renderable and `check`-able everywhere. A midi
  module in a render is not an error; it is a keyboard nobody is playing.

## 4. Hot reload

The PipeWire port belongs to the engine, not the module: editing a patch
never destroys the port or the controller connection. Reload migrates
tracker state (§2), so held notes survive a patch edit mid-performance —
the same "the drone never stops" contract as everything else.

## 5. Testing

Everything musical is testable without hardware by injecting events into
the module directly (the transport is the only untestable-offline sliver,
and it is kept dumb):

- note-on/off lifecycle, voct math (note 57 → 0.0, note 69 → +1.0),
  velocity scaling, vel-0-as-off;
- rr claim over free voices, oldest-steal at capacity, retrigger gap;
- `last` and `lowest` mono priority including held-key restore;
- sustain pedal deferral; bend range math; cc slot mapping + smoothing
  time-constant; channel filter (omni vs specific);
- bank-conformance suite shared with `alloc` (gates rise through 0.5, voct
  holds after release, gap on steal) run against both producers;
- state migration mid-held-chord.

## 6. Deferred

- Sample-accurate event offsets within a block.
- MIDI clock/transport sync (a future `midiclock` module; `clock` stays
  master otherwise), MPE, NRPN/14-bit CC, program change, MIDI out.
- MIDI-file input for offline renders.
