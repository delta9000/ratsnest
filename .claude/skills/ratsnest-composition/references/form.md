# Form: block tables, grids, transitions

## The block table

Write it in the header comment before patching. It is the score.

```text
# Arrangement (32 four-bar blocks, 7.27 s each):
#   0-1   intro    gated pad alone, filtered down
#   2-4   acid     the line fades in dark
#   5-6   build    hats + gate pad rise; riser at 6
#   7-12  DROP 1   kick + clap + acid, full squelch
#   13-15 break    drums gone, gate pad blooms wide; riser 15
#   16-23 DROP 2   everything + supersaw hook
#   24-26 groove   hook out, acid carries
#   27-29 fade     master fade
#   30-31 tail     silent guard
```

Block seconds at 4-bar blocks = `240 / bpm`. The table gives you every lane
length and the render duration in one shot.

**Guard steps are mandatory.** The grid wraps at
`lane_steps × block_seconds`. Choose

    render_duration = fade_end + ~3 s cushion   and   < wrap_time

or the intro walks back in over your outro.

## The arrangement grid recipe

One `stepseq` lane per element, all clocked from the same bar/block divider,
each step a 0..1 level, each slewed by a `func` with nothing on `trig`.

```text
master   = clock { bpm: 528 }          # sixteenths at 132
eighth   = div { by: 2 }
quarter  = div { by: 2 }
half     = div { by: 2 }
bar      = div { by: 2 }
four_bar = div { by: 4 }
master -> eighth.in
eighth -> quarter.in
quarter -> half.in
half -> bar.in
bar -> four_bar.in

arr_pad = stepseq { steps: [.3,.4, .6,.7,.7, 1,1, 0,0,0, .8] }
four_bar -> arr_pad.clock
pad_lvl = func { rise: 2, fall: 3 }     # no trig -> slew toward .in
arr_pad.cv -> pad_lvl.in

pad_amp = vca { gain: 0 }
pad_voice -> pad_amp.in
pad_lvl -> pad_amp.cv
```

Step 0 plays at t=0 — there is no first-edge skip. Rise/fall are the *feel*
of each transition: 2 s/3 s is a section fade, 0.05 s/0.05 s is a hard cut.

### Lanes are not only levels

The same lane CV drives anything:

```text
arr_dense.cv -> hats.hits    : 7      # euclid thickens into the drop
arr_open.cv  -> hat_env.fall : 0.03   # hat decays open
arr_dark.cv  -> bass_filt.cutoff : 900
arr_wide.cv  -> field.width  : 0.5
arr_wet.cv   -> send.lvl3    : 0.4
```

One lane, one gesture. Naming them `arr_*` keeps the score readable.

## Phrasing

Electronic form is powers of two: 4-bar blocks, 8-bar phrases, 16-bar
sections, 32-bar macro-form. The listener counts even when they don't.

- **1 bar** — the loop. Everything must survive repetition here.
- **4 bars** — the block. One lane step. The smallest unit anything changes on.
- **8 bars** — the phrase. Where a variation belongs (a fill, a rotation, a
  dropped hat).
- **16 bars** — the section. Where an element enters or leaves.
- **32 bars** — the paragraph. Intro / build / drop / break / drop / outro.

Break the grid *once*, deliberately: a 6-bar breakdown, a bar of silence
before the drop, a 3-beat stutter. Twice and it reads as a mistake.

## Transitions — the four devices

**1. Riser.** A `func` with slow rise and instant fall on anything that has a
range: a filter, a pitch, a noise level, a reverb send.

```text
riser_env = func { rise: 12, fall: 0.4, curve: quad }
arr_riser.cv -> riser_env.trig      # steps to 1 at the build block
riser_env -> riser_filt.cutoff : 6000
riser_env -> riser_amp.cv : 0.7
```

**2. Drop-out.** Kill everything for the last beat before the drop. A lane
step of 0 on every element, one block long, is more effective than any riser.

**3. Impact.** A single low sine hit with a long decay under the first beat of
the drop, plus a reversed crash (a `func` with slow rise into noise).

**4. Filter sweep on the whole bus.** Take the master mix through an `svf`
whose cutoff is a lane. Everything darkens together — the cheapest and most
reliable build in electronic music.

## The infinite scene: form without a grid

For drones and DJ tools, delete the grid. Variation comes from four or five
**incommensurate glacial LFOs** whose periods share no small common multiple:

```text
d1 = lfo { wave: sine, freq: 0.0189hz }    #  53 s
d2 = lfo { wave: tri,  freq: 0.0114hz }    #  88 s
d3 = lfo { wave: sine, freq: 0.0071hz }    # 141 s
d4 = lfo { wave: tri,  freq: 0.0033hz }    # 303 s
```

53/88/141/303 realign only after ~6 days. Route them to filter cutoffs, FM
operator levels (`lvl1..lvl6` are inputs — timbre is modulatable), echo
feedback and tone, reverb decay, grain position and density. Add a
low-probability `turing` for harmonic drift.

Do the fade in post, not in the patch:

```bash
ffmpeg -i take.wav -af "afade=t=in:st=0:d=4,afade=t=out:st=291:d=9" out.wav
```

## Tension curve

Every section should sit somewhere on a deliberate arc. A serviceable default,
in `--stats` RMS terms:

| Section | RMS vs. drop | What changes |
|---|---|---|
| intro | −13 dB | one element, filtered, wide |
| build | −6 dB | density rises, brightness rises, space closes |
| drop | 0 dB | full spectrum, narrow low end, dry |
| break | −7 dB | fewest elements, widest, wettest |
| drop 2 | 0 dB | drop plus one new element |
| outro | −∞ | subtract in reverse order of entry |

The break is the widest and wettest point of the track; the drop is the
narrowest and driest. That contrast is the whole illusion of "big".

## Endings

Four honest endings:

- **Fade lane** — `-1 -> out.level` plus `main_lvl -> out.level : 1.0`.
- **Subtract to one voice** — remove in reverse order of entry, leave the
  element that opened the track alone for eight bars.
- **Ring-out** — cut every source, let the reverb tail and the free-ringing
  `string`/`modal` voices decay. Render long enough to catch it.
- **Hard stop** — one bar of full silence after a downbeat. Rare, and only
  earned by a track that never stopped before.
