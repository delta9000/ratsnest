# Jungle / DnB / Breakcore — a fast top half over a slow bottom half

## The feel

Two tempos at once. The drums run at 160–180 and the bass moves at half that,
so the track sprints and lurches simultaneously. The break is a *melody of
transients*, not a loop: you re-order the slices. Everything below 150 Hz is
programmed clean and mono underneath it, because a 1969 Winstons recording has
no modern low end and never will.

Breakcore is the same patch with the chop resolution taken to 32nds, the
gate lanes rewritten every bar, and no obligation to stay in 4/4.

## Clock

172 BPM. Sixteenths at 4×; the half-time feel is a placement decision, not a
tempo change — kick on 1, snare on 3, hats still referencing full-tempo
jungle.

```text
master  = clock { bpm: 688 }     # 172 * 4 = sixteenths
eighth  = div { by: 2 }
quarter = div { by: 2 }
bar     = div { by: 4 }
master -> eighth.in
eighth -> quarter.in
quarter -> bar.in
```

For breakcore, clock a second `clock { bpm: 2752 }` (16× tempo) and gate it
into a voice for ratchets — see move 4.

## Palette

| Element | Modules | Starting values |
|---|---|---|
| break | `sampler` + `stepseq` | `loop: off`, per-slice `trig`, `pitch` from `cv` |
| kit resynth | `lib/kit/*.sub` | kick `tune: 44hz`, snare `snap: 0.7`, hat `decay: 0.1` |
| smear | `grains` | `size: 0.09`, `density: 26`, `jitter: 0.25` |
| reese | 3× `vco{saw}` + `svf.bp` subtract | detune ±0.006 v/oct, notch 420 Hz `res: 0.72` |
| sub | `vco{sine}` + `func` | 55 Hz, `fall: 0.22`, `curve: exp` |
| grit | `overdrive` | `model: klon, drive: 0.35, mix: 0.5` on the reese only |
| glue | `compressor` | `ratio: 3, attack: 8ms, release: 120ms, mix: 0.6` |

## Signature moves

### 1. Chop the break with a gate lane, repitch with the CV lane

One `stepseq` does both jobs: `gates` decides *where* a slice restarts,
`steps` carries the repitch for that slice in v/oct. This is the whole
technique — the sample plays from its head on every trigger, so a dense gate
lane stutters the front of the break and a sparse one lets phrases run.

```text
brk   = sampler { file: "samples/amen.wav", loop: off }
slice = stepseq { gates: [1,0,0,1, 0,0,1,0, 1,0,0,0, 0,1,0,0],
                  steps: [0, 0, 0, .0833, 0, 0, -.0833, 0,
                          0, 0, 0, 0, 0, .1667, 0, 0] }
master -> slice.clock
slice.trig -> brk.trig
slice.cv   -> brk.pitch
chop_hp = svf { cutoff: 180, res: 0.1 }
brk -> chop_hp.in            # read .hp — the break owns nothing below 180
```

`.0833` is a semitone, `.1667` a whole tone, `-.0833` down one. The file
string **must be quoted** and resolves against the process working directory.
No file = silence, not a crash; `check` still says ok, so confirm the path
yourself.

### 2. Resynthesise the kit underneath (or instead)

`patches/lib/kit/*.sub` gives you the weight the sample lacks: one contract,
`trig` + `accent` (0..1, latched) + `tune`/`decay`. Layer these under the
chop, or drop the sampler entirely — `patches/amen.patch` plays the whole
Amen groove on modelled voices.

```text
kick  = sub { file: "lib/kit/kick.sub", tune: 44hz, decay: 0.3 }
snare = sub { file: "lib/kit/snare.sub", snap: 0.7 }
hat   = sub { file: "lib/kit/hat.sub", decay: 0.1 }

kick_pat  = stepseq { gates: [1,0,0,0, 0,0,0,0, 0,0,1,0, 0,0,0,0] }
snare_pat = stepseq { gates: [0,0,0,0, 0,0,0,0, 1,0,0,0, 0,0,0,0] }
master -> kick_pat.clock
master -> snare_pat.clock
kick_pat.trig  -> kick.trig
snare_pat.trig -> snare.trig

hat_pat = euclid { steps: 16, hits: 11, rot: 2 }
coin    = bern { prob: 0.75, seed: 5 }
master -> hat_pat.clock
hat_pat.trig -> coin.gate
coin.a -> hat.trig            # coin.b is the ghost lane, or an open hat
```

Kick on step 0 and step 10, snare on step 8: that is the half-time skeleton.
The hats keep sixteenth density on top.

### 3. Reese: detune, then carve the notch by subtraction

There is no notch output on `svf`. Build one: take the dry sum, subtract a
resonant bandpass from it, and sweep the bandpass. `math` inputs default
to 0, so the bandpass cable into `b` owns the subtraction outright.

```text
r1 = vco { wave: saw, freq: 55hz }
r2 = vco { wave: saw, freq: 55hz }
r3 = vco { wave: saw, freq: 55hz }
 0.006 -> r2.v_oct            # ~10 cents each way; detune sets the beat rate
-0.006 -> r3.v_oct
rbus = vca { gain: 0.33 }
r1 -> rbus.in
r2 -> rbus.in
r3 -> rbus.in

rbp   = svf { cutoff: 420, res: 0.72 }
sweep = lfo { freq: 0.11hz }
rbus  -> rbp.in
sweep -> rbp.cutoff : 260
notch = math { op: sub, b: 0 }
rbus    -> notch.a
rbp.bp  -> notch.b : 0.9      # depth of the notch, not its width
rgrind = overdrive { model: klon, drive: 0.35, mix: 0.5 }
notch -> rgrind.in
```

The cheap version is `phaser { stages: 8, spread: 0.4, rate: 0.09hz,
feedback: 0.55, mix: 0.5 }` across the saw sum — several notches instead of
one, more Photek than Noisia. Verified working; use it when the sweep matters
more than the exact frequency.

### 4. Ratchets: gate a fast clock, don't write faster lanes

```text
fast        = clock { bpm: 2752 }        # 16x tempo
ratchet_win = euclid { steps: 16, hits: 3, rot: 11 }
rgate       = logic { op: and }
master -> ratchet_win.clock
fast              -> rgate.a
ratchet_win.gate  -> rgate.b
rgate -> snare.trig                      # bursts only inside the window
```

Move `rot` with a glacial `lfo` and the fills relocate themselves forever.

### 5. Sub discipline

The sub is its own oscillator, mono, low-passed, and does not track the
reese's detune — that is what makes the low end read as solid while the mid
churns.

```text
sub_osc = vco { wave: sine, freq: 55hz }
sub_env = func { rise: 0.004, fall: 0.22, curve: exp }
quarter -> sub_env.trig
sub_vca = vca { gain: 0 }
sub_osc -> sub_vca.in
sub_env -> sub_vca.cv
```

## Arrangement

```text
# Arrangement (32 four-bar blocks at 172 BPM, 5.58 s each):
#   0-1   intro    break only, high-passed hard, no sub
#   2-3   build    sub enters on quarters; hat density rises
#   4-11  DROP 1   full chop + reese + sub; notch sweeping slowly
#   12-13 switch   drums drop to half-time only, reese carries
#   14-19 DROP 2   chop resolution doubles, ratchet windows open
#   20-21 break    everything but the plate gone; break reversed by grains
#   22-29 DROP 3   as drop 2 + kit layer under the chop
#   30-31 fade     guard
```

Automate `euclid.hits` and `func.fall` from the lanes rather than adding new
voices (`docs/patch-craft.md`, "Euclid density is an automation target").
Full block-table mechanics: `ratsnest-composition/references/form.md`.

## Mix targets

- Sub 30–100 Hz, mono, alone. The kick's fundamental and the sine sub are the
  same slot — duck one against the other or tune them apart.
- High-pass the chopped break at 180 Hz and mean it. That is where the sample
  and the programmed low end fight.
- Reese owns 100–700 Hz. Width above 250 Hz only (`stereo { width: 1.6 }` on
  the reese, never on the sub).
- Drop-window RMS ≈ −13 dBFS, crest ~14 dB — punch over density
  (`ratsnest-composition/references/mixing.md`).
- Glue with `compressor { ratio: 3, attack: 8ms, release: 120ms, mix: 0.6 }`;
  parallel keeps the transients that make the break read as drums.

## In this repo

- `patches/amen.patch` — the Amen groove on the modelled kit, four bars of
  16ths, `steps` carrying accent. Steal the whole velocity-as-`accent` idiom.
- `patches/breakcore_synth.patch` — 180 BPM, 256-step lanes, `alloc { n: 3 }`
  fanning one hat lane into three instances so fast rolls don't choke
  themselves. Steal the polyphonic-hat trick.
- `patches/kit.patch` — the compact euclid+bern version of the same kit.
- `patches/lib/kit/*.sub` — the voices. Read `hat.sub`'s `choke` input.

## How to break it

1. **Repitch the break per bar, not per slice.** Put a bar-clocked `stepseq`
   of ±2 semitones on `sampler.pitch` and let a whole phrase sit sharp. The
   S1000 did it by accident; do it on purpose.
2. **Send the break through `grains` with `reverse` latched high** for one
   block. Not a fill — an entire section of the break running backwards at
   `size: 0.09`.
3. **Un-tune the sub from the reese.** Hold the sub on a pedal tone while the
   reese moves through the progression. Half of liquid DnB is a bass that
   refuses to follow the harmony.
4. **Make the notch audio-rate.** `lfo { freq: 90hz } -> rbp.cutoff : 400`
   stops being a sweep and becomes a sideband generator.
5. **Delete the kick for eight bars.** The break already contains one. Jungle
   at its best trusts the sample.

## Listening

- The Winstons, *Amen, Brother* — the source. Study where the ghost snares
  sit; that syncopation is what your gate lane is imitating.
- Remarc, *Ricky* — chop resolution beyond legibility; slices shorter than
  their own attack.
- Shy FX, *Original Nuttah* — one sub note per bar under maximum drum
  activity. The economy is the lesson.
- Photek, *Ni-Ten-Ichi-Ryu* — negative space; the reese is a *pad*, notch
  sweeping over 30+ seconds.
- Source Direct, *The Cult* — low-passed break plus a separate crack layer,
  which is exactly the sampler+kit layering above.
- Noisia, *Machine Gun* — FM movement on top of a reese base; take the
  `fm.lvl3` modulation from `references/bass-music.md` and put it here.
- Venetian Snares, *Szamár Madár* — 7/4 breakcore; proof the grid is optional
  if the harmony holds it together.
- Squarepusher, *Come On My Selector* — programmed jazz-hat detail at
  full tempo over a half-time bass. The two-tempo thesis, stated loudly.
