---
name: ratsnest-instrument
description: Use when a ratsnest patch must be played rather than rendered — MIDI keyboard input, polyphony, chords, voice allocation, live performance with hot-reload, capturing external audio, recording a take, or watching values at runtime.
---

# Playing it

Most of the skill library assumes offline render: you write, you `--stats`, you
iterate. This one covers the other half — the patch as an instrument you play,
extend and perform. `mise run play` instead of `mise run render`.

Load `ratsnest-patching` first.

## Polyphony

Two declarations. `alloc` distributes one mono gate/pitch stream across N
voices; `voice` stamps a block of modules once per voice.

```text
clk = clock { bpm: 200 }
wander = random { rate: 3.3hz, smooth: 0, seed: 7 }
hold = sh { mode: sample }
wander -> hold.in
clk -> hold.trig
q = quant { scale: [0, 3, 5, 7, 10] }
hold -> q.in : 0.6

poly = alloc { n: 3 }
clk -> poly.gate
q.out -> poly.voct

harp = voice { notes: poly }
harp.osc   = vco { freq: 220hz, wave: tri }
harp.pluck = lpg { damp: 0.45, decay: 1.1 }
poly.voct -> harp.osc.v_oct          # bank read: per-instance
harp.osc  -> harp.pluck.in           # inside -> inside: instance-local
poly.gate -> harp.pluck.strike
harp.pluck -> out.in : 0.4           # inside -> outside: straight sum
```

Verified: checks, renders at −2.4 dBFS peak / −12.7 RMS.

**The four cable rules** — which one applies depends on where each end lives:

| Cable | Resolves to |
|---|---|
| inside → inside | instance-local, one cable per voice |
| bound module's bare bank name, read inside (`poly.voct`) | `voct<i>` per instance |
| outside → inside | broadcast; same source to every voice |
| inside → outside | the **straight sum** of all voices — no `1/n`, gain-stage it yourself |

Things that bite:

- **`n` is inherited from the module bound by `notes:`.** An explicit `n:` on
  the `voice` block must match or it is a compile error.
- **There is no mono `gate`/`voct` output on `alloc` or `midi`.** Those bare
  spellings are reserved for bank resolution. A mono synth is `voices: 1` and
  `mid.gate1`.
- **A mono gate stream gives you strum polyphony, not chords.** Each trigger
  claims a fresh voice and older voices ring out through their own releases.
  For actual simultaneous notes you need `midi` or separate gate sources.
- **Instances are named `harp.osc#2`** — `#` starts a comment in source, so
  they can never collide with your names. `watch` shows them that way.

## MIDI

```text
mid = midi { channel: 0, voices: 4, bend: 2, cc: [74, 1] }
keys = voice { notes: mid }
keys.v   = vco { freq: 220hz, wave: saw }
keys.f   = ladder { cutoff: 400, res: 0.3 }
keys.env = adsr { attack: 0.01, decay: 0.2, sustain: 0.6, release: 0.4 }
keys.amp = vca { gain: 0 }
mid.voct -> keys.v.v_oct
keys.v -> keys.f.in
mid.gate -> keys.env.gate
keys.f -> keys.amp.in
keys.env -> keys.amp.cv
keys.amp -> out.in : 0.3
mid.cc1 -> keys.f.cutoff : 3000       # mod destination from CC 74
```

- `voct<i>` = `(note − 57) / 12`, so **A3 / MIDI 57 = 0.0** — the same 220 Hz
  reference as note names. Oscillator `freq:` bases should be 220.
- Pitch bend is already summed into every `voct`; the `bend` output carries the
  same value separately if you want it elsewhere.
- `mode` applies only at `voices: 1`: `last` is legato (restore does **not**
  retrigger), `lowest` is bass-pedal.
- At capacity, note-on steals the **oldest** voice with a one-sample gate gap
  so envelopes retrigger.
- Sustain (CC 64) defers note-offs. `channel: 0` is omni.
- **`midi` outputs defaults under `render`.** Offline has no transport, so a
  MIDI patch renders as a keyboard nobody is playing. Test these live only.

## Live: hot-reload and watch

```bash
mise run play -- patches/x.patch
mise run play -- patches/x.patch --watch lead.f.cutoff
```

Save the file and the graph swaps under the running audio. **Runtime state
survives hot reload when a module's name and type are unchanged** — delay
lines, reverb tanks, filter states, sequencer positions, `turing` registers,
MIDI tracker state. Rename a module and you reset it; that is the cheap way to
clear a stuck feedback net without stopping.

Consequences worth exploiting:

- **Perform by editing.** A patch is a score you can rewrite mid-play. Changing
  a `stepseq` array or a `quant` scale takes effect on the next clock without a
  gap.
- **Errors never kill the session.** A bad patch reports `line:col` and the
  last good graph keeps playing. Save freely.
- **`rec` restarts on reload** — the outgoing take is written, the new one
  overwrites on teardown. Don't edit mid-take unless you mean it.
- `scope { label: name }` is a passive tap: it captures last/peak for `watch`
  without altering the signal. Put them on anything you're debugging; they cost
  nothing and change nothing.

## Capturing and recording

```text
ext = extin                    # PipeWire capture; wire it in your patchbay
ext.out -> filter.in

r = rec { file: "take.wav", secs: 120 }
mix.out_l -> r.in_l
mix.out_r -> r.in_r
```

- `extin` and `midi` are both **live-only**: under `render` they output
  silence/defaults, exactly like an unplugged cable. A patch that depends on
  either cannot be verified offline — build and test the rest of it first.
- `rec` writes on graph teardown (session end or hot reload), buffer
  preallocated at `secs` (1..600). With `gate` unpatched it records
  continuously to the cap.
- For an offline stem, prefer `render --tap NAME[=node[.port]]`: it writes
  `<out>.NAME.wav` beside the mix and prints its level table without
  touching the patch. Keep `rec` for live takes under `play`.

## Turning a render patch into an instrument

1. Replace the clock-driven gate source with `midi { voices: 4 }` and bind a
   `voice` group to it.
2. Delete the arrangement grid. Lanes are the studio version of your hands.
3. Map two or three `cc` destinations — a filter cutoff, a send level, an FM
   operator level. Three knobs is a playable instrument; ten is a menu.
4. Keep `seed:` values fixed so the generative parts are reproducible between
   takes.
5. Add a `rec` and leave it running.

## Common mistakes

| Symptom | Cause |
|---|---|
| MIDI patch is silent under `render` | expected — `midi` has no offline transport |
| Only one note sounds | `alloc`/`voice` not bound, or `n: 1` |
| Voices all play the same pitch | you broadcast `q.out` instead of reading the bank `poly.voct` |
| Polyphonic patch is 3× too loud | inside → outside is a straight sum; no `1/n` |
| `voice` block won't compile | explicit `n:` disagrees with the bound module's |
| State resets on every save | you renamed a module; name+type must be stable |
| Chords don't work from a sequencer | mono gate stream = strum polyphony only |
