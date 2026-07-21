---
name: ratsnest-debugging
description: Use when a ratsnest patch is silent, too quiet, clipping, distorting, out of tune, ghosting notes, drifting into clipping over a long render, wrong length, or simply does not sound like the header comment claims — before changing any value.
---

# Debugging a patch you cannot hear

`check` proves a patch is syntactically valid. It says nothing about whether
the signal actually arrives. Nearly every ratsnest bug is a **signal that never
reaches the output** or **a number that is wrong by a factor you can't see**,
and both are found the same way: measure one element at a time.

Do not adjust values until you have located the stage. Guessing at levels is
how a two-minute bug becomes an hour.

## The solo probe — the one technique

Comment out the patch's final output cables, append one cable from the element
you suspect, and render. Nothing else changes, so the element is measured in
its real context with its real modulation.

```bash
sed 's|^dc.out_l -> out.left|# &|; s|^dc.out_r -> out.right|# &|' x.patch > _probe.patch
printf '\nbass_amp -> out.in\n' >> _probe.patch
./build/ratsnest render _probe.patch -d 20s -o /tmp/p.wav --stats
```

Walk it down the chain — `bass_amp`, then `bass_f.lp`, then `bass` — and the
first stage that reads −240 dBFS is where the signal dies. Two or three probes
localizes any silence.

Keep probes in the same directory as the patch: `sub { file: }` paths resolve
relative to the file, so a probe copied to `/tmp` will fail to find its
subpatches and you will debug the wrong thing.

**Probe every element once before mixing.** A table of each voice's solo peak
and RMS turns mixing from guesswork into arithmetic, and it is how you discover
that one voice is 20 dB down before you spend an hour on its send level.

## Bisect, don't scan

For a chain `osc → filter → vca → send → mix → master`, probe the **middle**
first. Signal present at `vca` means the bug is downstream; absent means
upstream. Six stages resolve in three renders instead of six.

## Silent, or nearly

In rough order of how often each is the answer:

| Check | Why |
|---|---|
| `vca { gain: 0 }` with nothing on `cv` | the single most common cause |
| an envelope whose `trig`/`gate` is never cabled | `func` with no trig *slews toward `in`* instead — it isn't broken, it's a different module |
| a mixer channel never patched to `out` | |
| `logic { op: and }` fed an arrangement lane below 0.5 | thresholds at 0.5; a lane at 0.35 is digital silence |
| a note name on a `freq:` base that isn't 220 Hz | 55 Hz base = two octaves down, inaudible, and `--stats` reads *identical* |
| `follow.env` scaled by a depth near 1 | env off a reverb tail peaks near 0.12, not 1.0 |
| `grains` at the start of a render | the buffer starts zeroed and the read head trails the write head, so it is silent for **`position × secs`** seconds (measured: 0.5 × 4 s = 2 s). `position: 0` starts instantly |
| `midi` / `extin` under `render` | live-only. Offline they emit defaults/silence by design |
| a struck resonator that is never struck | `modal`/`string` do nothing without `strike` or audio on `in` |

## Too loud, clipping, distorting

- **Peak pinned at 0.0 dBFS** — something reaches `out` at unity. Every `vco`
  is already full-scale; see `ratsnest-composition/references/levels.md`.
- **Peak == RMS in a window** — bricked. Back off `drive`/clipper.
- **It got loud when I doubled a part** — correlated sources sum N×, not √N.
  Doubling at the same pitch is +6 dB.
- **`drive { amount: 0.5 }`** flattens crest from 4.7 dB to 0.9 dB. It is a
  limiter. Use `overdrive { mix: 0.5 }` or the master clipper for glue.
- **`bass.sub` clips at unity** (measured 0.0 dBFS). Stage it at 0.4–0.5.

## Clean now, clipping later

Feedback nets bloom long after the window you rendered. A real example from
`patches/etudes/feedback-garden.patch`: at feedback 0.68 it read −31 dB RMS at
80 s and hit 0.0 dBFS by 160 s.

**Render five minutes and read every window** before believing any patch whose
delay or reverb has a path back into itself. If the last window is hotter than
the first, the loop gain is above unity and time is the only variable.

## Out of tune

| Symptom | Cause |
|---|---|
| everything off by whole octaves | note names on a base that isn't 220 Hz |
| everything off by an odd interval | a `freq:` base that isn't an A at all |
| a note retunes during its own tail | no `sh` holding pitch after the switch/quantizer |
| an interval is wrong but the root is right | you wrote a `…v` note name where an interval fraction belongs |
| a filter sweep starts too bright | the `cutoff:` config is the *floor*; cables sum onto it |
| a delay is at the wrong subdivision | you didn't zero `time:` before cabling `tempo` |

## Wrong shape over time

`--stats` is a level meter. It cannot see timbre, so a drone that fails to
darken looks identical to one that works. Use the band-split check in
`docs/patch-craft.md` and compare head against tail.

Other structural checks:

- **A section boundary in the wrong window** — the div chain or the grid, not
  the mix. Count: 4-bar blocks are `240 / bpm` seconds.
- **The intro comes back** — the arrangement grid wrapped. Render shorter than
  `lane_steps × block_seconds`.
- **A note swells in from a silent section** — a free-ringing voice ghosting.
  Gate the *strike*, not the output.
- **A muted part is still audible in the reverb** — the send is tapped
  pre-mute. Tap it from the post-fader `vca`.
- **A window of flat, unchanging RMS** — nothing in the arrangement moved
  there. That is a composition bug reported as a number.

## Live

```bash
mise run play -- patches/x.patch --watch lead.f.cutoff
```

`scope { label: name }` is a passive tap — it captures last/peak for `watch`
without altering the signal, so you can leave them in permanently. Put one on
every value you have ever been unsure about.

Renaming a module resets its runtime state across hot reload, which is the
quickest way to clear a stuck feedback net without stopping playback.

## Before you claim it's fixed

Re-render and read the numbers. A patch that checks `ok` has proven nothing
about its sound, and "it should work now" is not a measurement.
