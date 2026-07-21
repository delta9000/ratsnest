# Techno / acid — a filter being played, over a machine that never blinks

## The feel

Two ideas repeated until they become architecture: a drum machine that does not
swing and does not fill, and one voice — the 303 — whose *timbre* carries every
bit of the melody. Detroit adds chords and sorrow; Chicago adds squelch and
none. The patch is small; the automation is the composition.

## Clock

120–135 BPM. Clock sixteenths — the acid line needs them.

```text
# ---- clock tree: sixteenths at 130 BPM ----
master   = clock { bpm: 520 }
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
```

## Palette

| Element | Modules | Starting values |
|---|---|---|
| 303 voice | `vco { wave: saw }` → `ladder` | cutoff base 180, res 0.85, drive 0.8 |
| 303 accent | `stepseq` + `func` + `math { op: mult }` | fall 0.14, depth ×1.9 |
| 303 slide | `func` + `xfade { mix: 0 }` | rise/fall 0.045 |
| 909 kick | `vco` sine + 2× `func` + noise click + `drive` | 50 Hz, bend 2.0, fall 0.18 |
| 808 kick | `func` ping → `svf { res: 0.97 }.bp` | 55 Hz, res 0.97, slow cutoff drop |
| clap | `noise` → `svf.bp` 1800 + `func { fall: .13 }` | on 2 and 4 |
| 808 hat | `patches/lib/kit/hat_model.sub` | ring-modded squares under a wash |
| reese | 2× detuned `vco` saw + `math { op: sub }` notch | detune 0.006 v/oct |
| hoover | 2× `vco` pulse + PWM + pitch sweep + `chorus` | sweep 0.5 oct over 0.5 s |

## Signature moves

### 1. Accent widens the envelope, it does not add a bump

Roland called it the gimmick circuit. An accented step is louder *and* the
filter envelope reaches further — the whole "wow". Do not sum a DC offset onto
cutoff; multiply the envelope. `math`'s `b` base is 1, so an unaccented note
passes its envelope through untouched and an accented one scales it.

```text
# ---- 303: pitch / gate / slide / accent lanes ----
# gates and slide are complements: a slide step is exactly a step that
# does not restrike. Author them together or they drift apart.
acid_seq = stepseq { steps: [A1v, A1v, A2v, A1v, C2v, A1v, G2v, A1v,
                             A1v, E2v, A1v, A1v, C2v, D2v, A1v, G1v],
                     gates: [1,1,0,1, 1,1,1,0, 1,1,1,1, 1,0,1,1] }
acid_slide = stepseq { steps: [0,0,1,0, 0,0,0,1, 0,0,0,0, 0,1,0,0] }
# stepseq length comes from `steps:` alone — a short steps array silently
# truncates a long gates array. Pad it to sixteen zeros.
acid_acc = stepseq { steps: [0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0],
                     gates: [1,0,0,0, 0,0,1,0, 0,1,0,0, 1,0,0,0] }
master -> acid_seq.clock
master -> acid_slide.clock
master -> acid_acc.clock

acid_env = func { rise: 0.001, fall: 0.11, curve: exp }
acid_seq.gate -> acid_env.trig
acc_env = func { rise: 0.001, fall: 0.14, curve: exp }
acid_acc.gate -> acc_env.trig

acid_depth = math { op: mult }        # b base 1 -> unaccented = env x 1
acid_env -> acid_depth.a
acc_env -> acid_depth.b : 0.9         # accented    = env x 1.9
```

### 2. Slide is a portamento you can switch off per step

`func` with nothing on `trig` slews toward `in` — that is the portamento. The
303 only glides on flagged steps, so crossfade between the raw stepped pitch
and the slewed one under the slide lane. `xfade`'s `mix` base is 0.5; set it to
0 in the config or the lane cannot own it.

The retrigger suppression falls out for free: drive the envelope from
`acid_seq.gate` (which respects the `gates:` array) rather than `.trig` (which
fires every step). A slide step produces no rising edge, so the filter envelope
keeps falling through the pitch change. Bark into moan.

```text
acid_gl = func { rise: 0.045, fall: 0.045 }
acid_seq.cv -> acid_gl.in
acid_pitch = xfade { mix: 0 }         # zero the base; the lane owns it
acid_seq.cv -> acid_pitch.a
acid_gl -> acid_pitch.b
acid_slide.cv -> acid_pitch.mix

acid_osc = vco { freq: 220hz, wave: saw }
acid_pitch -> acid_osc.v_oct
acid_filt = ladder { cutoff: 180, res: 0.85, drive: 0.8 }
acid_osc -> acid_filt.in
acid_depth -> acid_filt.cutoff : 1500
acid_amp = vca { gain: 0 }
acid_filt -> acid_amp.in
acid_env -> acid_amp.cv : 0.72
acc_env -> acid_amp.cv : 0.18         # accent hits level AND depth
```

Now automate `acid_filt.cutoff` and `acid_filt.res` from slow LFOs or an
arrangement lane. That knob-twiddle *is* the arrangement — DJ Pierre's whole
technique.

### 3. 909 kick: pitch sweep plus a click

The 909 reads punchier than the 808 because a separate filtered noise click is
summed under the attack. Three envelopes — pitch (fast), body (medium, `log`),
click (near-instant) — then saturation, so the peak anchors the mix.

```text
# ---- 909 kick ----
k9_penv = func { rise: 0.0004, fall: 0.035, curve: exp }
quarter -> k9_penv.trig
k9_aenv = func { rise: 0.0006, fall: 0.18, curve: log }
quarter -> k9_aenv.trig
k9_osc = vco { freq: 50hz, wave: sine }
quarter -> k9_osc.sync
k9_penv -> k9_osc.v_oct : 2.0
k9_amp = vca { gain: 0 }
k9_osc -> k9_amp.in
k9_aenv -> k9_amp.cv : 0.95

k9_cenv = func { rise: 0.0002, fall: 0.004, curve: exp }
quarter -> k9_cenv.trig
k9_noise = noise { color: white, seed: 3 }
k9_click = svf { cutoff: 3000, res: 0.2 }
k9_noise -> k9_click.in
k9_camp = vca { gain: 0 }
k9_click.hp -> k9_camp.in
k9_cenv -> k9_camp.cv : 0.5

k9_drive = drive { amount: 0.3, mode: soft }
k9_amp -> k9_drive.in : 0.9
k9_camp -> k9_drive.in : 0.35
```

### 4. 808 kick: ping a resonant filter and let it ring

Different topology entirely: no oscillator. A two-millisecond impulse excites a
filter at `res: 0.97` and the filter's own ringdown *is* the boom; a slow
envelope pulls the cutoff down for the long fall. Read `bp`, not `lp` — you
want the resonance, not the impulse.

```text
# ---- 808 kick: pinged twin-T substitute ----
k8_ping = func { rise: 0.0002, fall: 0.002 }
quarter -> k8_ping.trig
k8_body = svf { cutoff: 52, res: 0.97 }
k8_ping -> k8_body.in : 2
k8_drop = func { rise: 0.0005, fall: 0.12, curve: exp }
quarter -> k8_drop.trig
k8_drop -> k8_body.cutoff : 40
k8_amp = vca { gain: 0 }
k8_body.bp -> k8_amp.in
k8_drop -> k8_amp.cv : 0.85
```

The same trick with a noise burst instead of an impulse is the 808 snare; with
six inharmonic squares and a high-pass it is the 808 hat — already packaged as
`patches/lib/kit/hat_model.sub`, so instantiate that rather than rebuilding it.

### 5. Reese and hoover are two different ancestors

**Reese** (Saunderson, 1988): two saws detuned enough to beat audibly, then a
*notch*. `svf` has no notch output — subtract the bandpass from the input.
Sweep the notch centre slowly and you have the DnB "phaser" movement; leave it
static and you have the hollow, vowel-like liquid bass.

**Hoover** (Persing's Alpha Juno "What The", by way of Mentasm): pulse
oscillators an octave apart under heavy PWM, a downward pitch sweep on every
note, thick chorus. It is a stab, not a bass.

```text
# ---- reese: detuned saws through a swept notch ----
re_a = vco { freq: 220hz, wave: saw }   # 220 base, so A1v really is 55 Hz;
re_b = vco { freq: 220hz, wave: saw }   # on a 110 base it lands at 27.5 and
A1v -> re_a.v_oct                       # the notch has nothing to bite on
A1v -> re_b.v_oct
0.006 -> re_b.v_oct
re_sum = mix { levels: [0.5, 0.5] }
re_a -> re_sum.in1
re_b -> re_sum.in2
re_bp = svf { cutoff: 420, res: 0.75 }
re_sum.out_l -> re_bp.in
re_notch = math { op: sub, b: 0 }     # in - bp = notch; b base is 1, zero it
                                      # or you ship a -1 DC offset
re_sum.out_l -> re_notch.a
re_bp.bp -> re_notch.b
re_sweep = lfo { wave: tri, freq: 0.09hz }
re_sweep -> re_bp.cutoff : 260

# ---- hoover: PWM pulses + pitch dive + chorus ----
hv_pwm = lfo { freq: 0.9hz, wave: tri }
hv_a = vco { freq: 110hz, wave: pulse, pw: 0.5 }
hv_b = vco { freq: 220hz, wave: pulse, pw: 0.5 }
hv_c = vco { freq: 220hz, wave: saw }
hv_pwm -> hv_a.pwm : 0.35
hv_pwm -> hv_b.pwm : -0.3
hv_dive = func { rise: 0.002, fall: 0.5, curve: exp }
bar -> hv_dive.trig
hv_dive -> hv_a.v_oct : 0.5
hv_dive -> hv_b.v_oct : 0.5
hv_dive -> hv_c.v_oct : 0.5
0.003 -> hv_b.v_oct
hv_f = svf { cutoff: 1800, res: 0.2 }
hv_a -> hv_f.in : 0.3
hv_b -> hv_f.in : 0.3
hv_c -> hv_f.in : 0.25
hv_ch = chorus { voices: 3, rate: 0.6hz, depth: 0.8, mix: 0.6 }
hv_f.lp -> hv_ch.in_l
hv_f.lp -> hv_ch.in_r
hv_dc = dcblock { cutoff: 20 }        # PWM pulses carry DC
hv_ch.out_l -> hv_dc.in_l
hv_ch.out_r -> hv_dc.in_r
```

```text
# ---- mix ---- (both kicks are wired so the block stands alone; in a track
# you pick one, or layer the 808 at 0.25 under the 909 for the low tail)
band = mix { levels: [0.85, 0.25, 0.6, 0.5, 0.35],
             pans: [0, 0, 0.1, 0, 0.2] }
k9_drive -> band.in1
k8_amp -> band.in2
acid_amp -> band.in3
re_notch -> band.in4
hv_dc.out_l -> band.in5
k9_aenv -> band.lvl4 : -0.28
k9_aenv -> band.lvl3 : -0.1
band.out_l -> out.left
band.out_r -> out.right
```

## Arrangement

```text
# Arrangement (32 four-bar blocks, 7.38 s each at 130 BPM):
#   0-1   tools    kick + hat only, no melody
#   2-4   acid-in  303 fades up dark; cutoff lane still low
#   5-6   build    hats thicken (euclid hits lane), riser at 6
#   7-12  DROP 1   full groove; res and cutoff ride slow LFOs
#   13-15 strip    kick out, 303 alone through the delay; riser 15
#   16-23 DROP 2   drop 1 + reese, accent lane doubled
#   24-26 groove   303 carries; peel the reese
#   27-29 fade
#   30-31 tail     silent guard
```

## Mix targets

- Drop RMS ≈ **−12 dBFS**, crest ~11 dB. Dense and hot.
- Kick channel 0.85, saturated to ~±1 — the peak anchor. The 909 owns 50–90 Hz;
  the 808 layered under it owns 35–55. Never both at full at once.
- The 303 lives 200 Hz–3 kHz and gets *no* sub. High-pass it at 150 if it fights
  the kick, and duck it only lightly (−0.1) — the squelch should survive.
- Reese: mono below 150, width only in its 250–700 Hz character band.
- Hats above 6 k, ducked −0.13 off the quarter clock so they breathe.
- Three ducking layers per `ratsnest-composition/references/mixing.md`.

## In this repo

- `patches/ion.patch` — a full peak-time acid track: accent lane onto both
  cutoff and level, 16-step interval riff added to the chord root, `sh` holding
  pitch through the decay. Steal its arrangement grid verbatim.
- `patches/lib/kit/` — kick, snare, clap, rim, tom, two hats behind one
  `trig`/`accent`/`tune`/`decay` contract. Cheaper than rebuilding drums.
- `patches/halcyon.patch` — the reese-plus-clean-sub split and the
  chord-tracking `switch` arp.
- `patches/kit.patch` — the accent contract driven from `bern`.

## How to break it

1. **Clock the acid line at 3/16.** Feed `acid_seq.clock` from a `div { by: 3 }`
   off the sixteenth clock. A 16-step pattern against a 4/4 bar realigns every
   three bars — one lane, endless variation, still locked.
2. **Put the slide lane on a `turing`.** `turing.bit1 -> acid_pitch.mix` makes
   the glides mutate slowly while the notes stay fixed.
3. **Two 303s at different resonances**, one panned ±0.4, both from the same
   sequencer but one clocked through a `div { by: 2 }`. Instant Hardfloor.
4. **Overdrive the acid, not the kick.** `overdrive { model: rat, drive: 0.7 }`
   after the ladder. The genre's "acid tracks are just distortion" school.
5. **Take the kick out and leave the duck in.** Keep `k9_aenv` driving the mix
   ducks with the kick channel at 0. Sixteen bars of pumping nothing.

## Listening

- **Phuture — "Acid Tracks"** — the 303 as lead instrument for twelve minutes.
  Listen for how rarely the *notes* change and how constantly the filter does.
- **Model 500 — "No UFO's"** — 909 anatomy in the open: click, body, sweep.
- **Hardfloor — "Acperience 1"** — two 303s interleaved; different resonance
  settings are what separates them, not different notes.
- **Robert Hood — *Minimal Nation*** — subtraction as arrangement. Count the
  simultaneous voices; it is rarely more than four.
- **Second Phase — "Mentasm"** — the hoover in its original habitat; note the
  pitch dive on *every* stab, not just the first.
- **Plastikman — *Sheet One*** — 808 kick ringdown with almost nothing on top;
  the pinged-filter recipe at full length.
