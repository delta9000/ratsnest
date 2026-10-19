# Dubstep / Neuro — one note, modulated until it becomes a sentence

## The feel

Almost no harmony and almost no melody: a single held bass note per bar, and
everything interesting happens to its *timbre* inside that bar. Half-time
drums leave enormous space (snare on 3, kick on 1 and the "a" of 3) and the
bass fills all of it. The design question is never "what note" — it is "what
does the note do between now and the next one".

Split the bass in two, always. A pure sine sub carries the weight and never
moves; the growl layer above 150 Hz does all the modulating. Mixing those two
jobs into one oscillator is the single most common failure.

## Clock

140 BPM for dubstep, 172 for neuro. Both feel half-time; the difference is
what the hats do.

```text
master  = clock { bpm: 560 }     # 140 BPM sixteenths
eighth  = div { by: 2 }
quarter = div { by: 2 }
half    = div { by: 2 }
master -> eighth.in
eighth -> quarter.in
quarter -> half.in
```

Sync the wobble to that tempo rather than typing an LFO frequency:

```text
wrate = tempo { bpm: 140, beats: 0.25 }    # sixteenth-note wobble
wob   = lfo { freq: 0hz, wave: tri }       # zero the base: cables SUM
wrate.hz -> wob.freq
```

`beats: 0.5` = eighths, `0.25` = sixteenths, `0.75` = dotted eighths (the
lurching one). `beats` is an input, not a config — patch a bar-clocked
`stepseq { steps: [.5, .25, .25, .125] }` into `wrate.beats` (with
`tempo { beats: 0 }` so the cable owns it) and the wobble changes subdivision
every bar for free.

## Palette

| Element | Modules | Starting values |
|---|---|---|
| note lane | `stepseq` | one pitch per bar; `gates` for the rhythm |
| sub | `vco{sine}` + `adsr` + `svf.lp` | 55 Hz base, LP 90 Hz, mono |
| wobble | `lfo` + `vca` + `func` + `ladder` | cutoff 260 base, depth 1400, res 0.62 |
| growl | `fm { algo: dual }` | ratios `[1,1,3.5,1,7,2]`, `fb` on ops 2 and 5 |
| formants | 3× `svf.bp` + 2× `xfade` | 700 / 1150 / 2400 Hz, `res: 0.85` |
| dirt | `overdrive { model: rat }` | `drive: 0.45, mix: 1`, then `dcblock` |
| drums | `lib/kit/*.sub` | snare on 3, big; hats sparse and wide |

## Signature moves

### 1. The delayed LFO — the wobble breathes in

A wobble that starts at full depth on note-on sounds mechanical. Ramp the
depth with a `func` fired by the same trigger: the note enters clean and the
modulation arrives ~400 ms later. Reset the LFO phase on the trigger too, or
each note starts mid-swing.

```text
note = stepseq { gates: [1,0,0,0, 0,0,1,0, 1,0,0,0, 0,1,0,0],
                 steps: [D1v, D1v, D1v, D1v, D1v, D1v, F1v, F1v,
                         D1v, D1v, D1v, D1v, C1v, C1v, C1v, C1v] }
master -> note.clock

wob = lfo { freq: 0hz, wave: tri }
wrate.hz -> wob.freq
note.trig -> wob.reset
depth = func { rise: 0.42, fall: 0.12, curve: exp }
note.trig -> depth.trig
wdepth = vca { gain: 0 }
wob   -> wdepth.in
depth -> wdepth.cv

saw1 = vco { wave: saw, freq: 220hz }   # 220 base: a `…v` lane lands on pitch
saw2 = vco { wave: saw, freq: 220hz }   # reaching for 55hz here is the classic
note.cv -> saw1.v_oct                   # silent-bass bug — D1v would render 9 Hz
note.cv -> saw2.v_oct
0.008 -> saw2.v_oct
wfilt = ladder { cutoff: 260, res: 0.62 }
saw1 -> wfilt.in : 0.5
saw2 -> wfilt.in : 0.5
wdepth -> wfilt.cutoff : 1400        # 260 is the floor, not the centre
```

The `ladder` cutoff base is where the wobble *bottoms out*. Set it to the
darkest sound you want and let the modulation climb from there.

### 2. Mono sub, untouched

```text
sub_osc = vco { wave: sine, freq: 220hz }   # 220, not 55 — see rule 2
note.cv -> sub_osc.v_oct                    # the sub is the easiest one to
                                            # lose an octave in and not hear
sub_env = adsr { attack: 0.004, decay: 0.05, sustain: 0.95, release: 0.06 }
note.gate -> sub_env.gate
sub_vca = vca { gain: 0 }
sub_osc -> sub_vca.in
sub_env -> sub_vca.cv
sub_lp  = svf { cutoff: 90, res: 0 }
sub_vca -> sub_lp.in                  # read .lp
```

No distortion, no width, no filter movement. `adsr` (not `func`) because the
sub should hold for the length of the gate. Duck it against the kick and
never let a second element into 30–100 Hz.

### 3. FM growl: modulate an operator level near audio rate

`fm`'s per-op `lvl1..lvl6` are *inputs*, so the FM index is modulatable at any
rate. An LFO in the 40–90 Hz range on a modulator level creates sidebands
around the sidebands — the vocal, talking quality. A slower LFO on a second
operator gives the growl its long arc.

```text
growl = fm { algo: dual, ratios: [1, 1, 3.5, 1, 7, 2],
             levels: [0.9, 0.35, 0.2, 0.6, 0.15, 0.25],
             fb: [0, 0.3, 0, 0, 0.45, 0], curve: log,
             t4: [0.05, 0.05, 0.05, 0.05, 0.05, 0.05] }
note.gate -> growl.gate
note.cv   -> growl.v_oct
idx  = lfo { freq: 62hz, wave: tri }
idx  -> growl.lvl3 : 0.35            # audio-rate index modulation
idx2 = lfo { freq: 3.1hz }
idx2 -> growl.lvl5 : 0.12
```

Non-integer ratios (3.5, 7, 2) put the sidebands off the harmonic series,
which is what separates a growl from a bell. `fb` on ops 2 and 5 adds the
noise floor that reads as grit. Detune `idx.freq` a few Hz per section and the
"vowel" changes.

### 4. Formant morphing from parallel bandpasses

Three resonant `svf.bp` at vowel-ish frequencies, crossfaded by one LFO
through a chain of two `xfade { law: equal_power }`s (the default law is now
linear). Equal-power means both bands sit −3 dB at the midpoint, which is exactly the vowel-in-between you want.

```text
f1 = svf { cutoff: 700,  res: 0.85 }
f2 = svf { cutoff: 1150, res: 0.85 }
f3 = svf { cutoff: 2400, res: 0.8 }
growl -> f1.in
growl -> f2.in
growl -> f3.in
vow = lfo { freq: 0.6hz, wave: tri }
x1 = xfade { mix: 0.5, law: equal_power }
f1.bp -> x1.a
f2.bp -> x1.b
vow -> x1.mix : 0.5
x2 = xfade { mix: 0.35, law: equal_power }
x1    -> x2.a
f3.bp -> x2.b
vow -> x2.mix : 0.25
```

Vowel centres worth stealing: "oo" 300/870, "oh" 500/1000, "ah" 700/1150,
"eh" 550/1900, "ee" 300/2300. Put the pairs on `f1`/`f2` and step them from a
bar-clocked `stepseq` for a bass that pronounces words.

### 5. Dirt last, DC-block after it

```text
grind = overdrive { model: rat, drive: 0.45, tone: 0.55, mix: 1 }
x2 -> grind.in
gdc = dcblock { cutoff: 25 }
grind -> gdc.in_l                      # read .out_l
```

`rat` is hard-clip, huge gain, dark — the neuro default. `muff` for a fuzzier
dubstep growl, `klon` when the modulation is already doing the work. Never on
the sub bus.

## Arrangement

```text
# Arrangement (28 four-bar blocks at 140 BPM, 6.86 s each):
#   0-1   intro    atmos + filtered growl, no drums
#   2-3   build    half-time drums enter; snare roll accelerates at 3
#   4-11  DROP 1   sub + wobble at sixteenths; formant LFO slow
#   12-13 break    bass gone, pad and plate only
#   14-15 rebuild  growl fades up filtered; wobble subdivision doubles
#   16-23 DROP 2   FM growl replaces the wobble; formants stepped per bar
#   24-25 half     everything at half density; sub alone for two bars
#   26-27 fade     guard
```

The drop is a *timbre* change, not a level change. Automate `growl.lvl3`,
`wrate.beats`, and `overdrive.drive` from the lanes; the parts stay the same.

## Mix targets

- 30–100 Hz: sub only, mono, ducked against the kick. Check `--stats` DC ≈ 0
  after the `dcblock` or the sub eats headroom for nothing.
- 100–250 Hz: growl body. This is where the mix gets muddy — high-pass the
  growl layer at 120 Hz once the sub is present.
- 250–700 Hz: the formant/character band; modest width here only.
- Drop RMS ≈ −13 dBFS, crest ~14 dB — transient punch and sub translation
  over loudness (`ratsnest-composition/references/mixing.md`).
- Compress the growl bus hard (`ratio: 6, attack: 5ms, release: 80ms,
  mix: 0.7`) and leave the sub out of it entirely.
- Mono below 150 Hz is non-negotiable: `stereo { mono: 1 }` on the sub bus if
  anything upstream has widened it.

## In this repo

- `patches/breakcore_synth.patch` — the sub-plus-drums split at 180 BPM, and
  a bus where the sub bypasses the glue compressor into the limiter. That
  routing is the thing to steal.
- `patches/fm.patch` and `patches/fm-tests/` — the `fm` module's parameter
  space; find a growl there before designing one.
- `patches/lib/bass.sub` — a packaged bass voice with `pitch`/`decay` params.
- `patches/west-coast.patch` — `fold` and `lpg` timbral modulation, which is
  the same "modulate the timbre, not the level" thesis in a different accent.

## How to break it

1. **Put the wobble on the *resonance*, not the cutoff.** `wdepth ->
   wfilt.res : 0.3` with a static cutoff. It stops sweeping and starts
   snarling in one place — much nastier, rarely done.
2. **Make the sub the only bass for a whole drop.** Eight bars of pure sine
   under busy drums. The next drop's growl will sound twice as large.
3. **Ring-modulate the growl against the wobble LFO.** `math { op: mult }`
   with `idx` on `b` — inharmonic, metallic, and completely uncontrolled.
4. **Trigger the formant morph from `bern` instead of an LFO.** Random vowel
   per note. It reads as a performance rather than a modulation.
5. **Half-time the wobble against the drums.** `beats: 1` (quarter notes)
   under 172 BPM neuro drums — the bass drags the track backwards while the
   drums sprint. This is the neurofunk feel in one parameter.

## Listening

- Skream, *Midnight Request Line* — sub, space, and almost nothing else.
  Study the negative space before you study the bass design.
- Benga & Coki, *Night* — the wobble as the entire arrangement; a single
  detuned filtered note carries a track.
- Rusko, *Cockney Thug* — the delayed-LFO breathe on every note, obviously
  audible. The reference implementation of move 1.
- Noisia, *Diplodocus* — FM index modulation as a formant; the growl changes
  vowel four times per bar.
- Black Sun Empire, *Arrakis* — reese underneath, FM movement above, the
  layering that neuro is actually built from.
- Ivy Lab, *Sunday Crunk* — half-time at 172 with almost no drum density.
  The bass does the rhythm.
- Burial, *Archangel* — the same tempo family with none of the aggression;
  proof that the space matters more than the distortion.
- The Bug, *Skeng* — sub pressure, mono, no width, ruinously loud. A lesson
  in what you can leave out.
