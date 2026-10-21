# Percussion — drums are three parts, none of them a drum module

## When to reach for it

Any time you need a kick, snare, hat, clap, tom, rim, cowbell, or an
unnameable metallic hit. Also when a sampled drum won't sit: a synthesized
drum is tunable to the track's key and its decay is an automation target.

The substitution principle is the whole card. Every classic drum-machine
voice is:

| stage | modules |
|---|---|
| excitation — click, noise burst, burst train | `func` (0.2–2 ms fall), `noise` |
| resonance — something that rings | resonant `svf`/`ladder`, `modal`, `vco` bank |
| shaping — how fast it dies | `func` → `vca`, `modal.damping` |

Diagnose in that order. Thin attack = excitation. Wrong pitch or no body =
resonance. Sounds like a synth pad = shaping.

## Core patch

Three primitives, one groove: a **pinged filter** kick, a **noise+body**
snare, an **inharmonic stack** hat. No `modal`, no samples.

```text
master  = clock { bpm: 480 }        # sixteenths at 120
eighth  = div { by: 2 }
quarter = div { by: 2 }
master -> eighth.in
eighth -> quarter.in

# --- 1. PING: a click into a resonant filter. The filter is the drum. ---
kclick = func { rise: 0.0002, fall: 0.0015 }
quarter -> kclick.trig
kbody = svf { cutoff: 48hz, res: 0.99 }
kclick -> kbody.in : 1.6
kpitch = func { rise: 0.0004, fall: 0.14, curve: exp }
quarter -> kpitch.trig
kpitch -> kbody.cutoff : 130
kdecay = func { rise: 0.002, fall: 0.66, curve: exp }
quarter -> kdecay.trig
kamp = vca { gain: 0 }
kbody.lp -> kamp.in
kdecay -> kamp.cv
kick = drive { amount: 0.12, mode: soft }
kamp -> kick.in

# --- 2. NOISE + BODY: snare. ---
spat = euclid { steps: 4, hits: 2, rot: 1 }
quarter -> spat.clock
snz = noise { color: white, seed: 5 }
sbp = svf { cutoff: 1.9khz, res: 0.3 }
snz -> sbp.in
senv = func { rise: 0.0004, fall: 0.33, curve: exp }
spat.trig -> senv.trig
sbody = svf { cutoff: 185hz, res: 0.93 }
sclick = func { rise: 0.0002, fall: 0.002 }
spat.trig -> sclick.trig
sclick -> sbody.in : 5
samp = vca { gain: 0 }
sbp.bp -> samp.in : 0.9
sbody.bp -> samp.in : 1.4
senv -> samp.cv
snare = drive { amount: 0.08, mode: soft }
samp -> snare.in

# --- 3. INHARMONIC STACK: 808 hat, six squares, high-passed twice. ---
h1 = vco { wave: pulse, freq: 263hz }
h2 = vco { wave: pulse, freq: 400hz }
h3 = vco { wave: pulse, freq: 421hz }
h4 = vco { wave: pulse, freq: 474hz }
h5 = vco { wave: pulse, freq: 587hz }
h6 = vco { wave: pulse, freq: 845hz }
hstack = svf { cutoff: 7khz, res: 0.05 }
h1 -> hstack.in : 0.4
h2 -> hstack.in : 0.4
h3 -> hstack.in : 0.4
h4 -> hstack.in : 0.4
h5 -> hstack.in : 0.4
h6 -> hstack.in : 0.4
hstack2 = svf { cutoff: 7khz, res: 0.05 }
hstack.hp -> hstack2.in
hpat = euclid { steps: 8, hits: 5, rot: 2 }
eighth -> hpat.clock
henv = func { rise: 0.0002, fall: 0.15, curve: exp }
hpat.trig -> henv.trig
hamp = vca { gain: 0 }
hstack2.hp -> hamp.in
henv -> hamp.cv

m = mix { levels: [0.7, 0.4, 0.75], pans: [0, 0, 0.2] }
kick -> m.in1
snare -> m.in2
hamp -> m.in3
m.out_l -> out.left
m.out_r -> out.right
```

Two structural notes. The kick's pitch envelope goes on **`svf.cutoff`**, not
on an oscillator — the filter's own ringdown is the sine. And the hat is
high-passed *twice* (24 dB/oct): one 12 dB/oct stage leaves a low-mid
difference tone that reads as a wood block, which is the exact bug
`kit/hat_model.sub` documents in its header.

## Variations

**808 kick** — a sine whose pitch collapses into a long ringdown. Longer amp
env than the pitch env by an order of magnitude; that ratio *is* the 808.

```text
k8_p = func { rise: 0.0005, fall: 0.09, curve: exp }
clk -> k8_p.trig
k8_osc = vco { wave: sine, freq: 48hz }
k8_p -> k8_osc.v_oct : 2.2
k8_env = func { rise: 0.002, fall: 1.95, curve: exp }
clk -> k8_env.trig
k8_amp = vca { gain: 0 }
k8_osc -> k8_amp.in
k8_env -> k8_amp.cv
k8 = drive { amount: 0.12, mode: soft }
k8_amp -> k8.in
```

**909 kick** — same skeleton, but the pitch env is 4x faster and deeper
(a snap, not a bend), the body is a third as long, and a separate noise
click layer sits on top. The click is what makes it cut on a laptop.

```text
k9_p = func { rise: 0.0003, fall: 0.036, curve: exp }
k9_p -> k9_osc.v_oct : 3.4              # vs 808's 2.2 over 90 ms
k9_env = func { rise: 0.001, fall: 0.57, curve: exp }
k9_n = noise { color: white, seed: 9 }
k9_hp = svf { cutoff: 2.2khz, res: 0.2 }
k9_n -> k9_hp.in
k9_cenv = func { rise: 0.0002, fall: 0.007 }
clk -> k9_cenv.trig
k9_clk = vca { gain: 0 }
k9_hp.hp -> k9_clk.in
k9_cenv -> k9_clk.cv
k9_m = mix { levels: [1.0, 0.45] }
k9_body -> k9_m.in1
k9_clk -> k9_m.in2
```

**808 cowbell** — two pulse `vco`s at ~800 and ~540 Hz, **mixed, not ring
modulated** (the original circuit sums them), through a bandpass. The
interval is a deliberately sour near-fifth; ring modulation here gives you a
clangy bell, not a cowbell.

```text
cb_a = vco { wave: pulse, freq: 800hz }
cb_b = vco { wave: pulse, freq: 540hz }
cb_bp = svf { cutoff: 2.64khz, res: 0.45 }
cb_a -> cb_bp.in : 0.5
cb_b -> cb_bp.in : 0.5
cb_env = func { rise: 0.0005, fall: 1.08, curve: exp }
eig -> cb_env.trig
cb = vca { gain: 0 }
cb_bp.bp -> cb.in
cb_env -> cb.cv : 0.8
```

**Clap** — one trigger widened into a 4 ms pulse, then two `delay` taps at
11 and 23 ms; all three retrigger a 6 ms burst envelope (cables sum onto
`func.trig`), and the last tap starts the tail.

```text
cl_pre = func { rise: 0.0001, fall: 0.004 }
clk -> cl_pre.trig
cl_d1 = delay { time: 0.011, feedback: 0, mix: 1, slew: 0 }
cl_d2 = delay { time: 0.023, feedback: 0, mix: 1, slew: 0 }
cl_pre -> cl_d1.in
cl_pre -> cl_d2.in
cl_b = func { rise: 0.0002, fall: 0.006 }
cl_pre -> cl_b.trig
cl_d1 -> cl_b.trig
cl_d2 -> cl_b.trig
cl_t = func { rise: 0.001, fall: 0.42, curve: exp }
cl_d2 -> cl_t.trig
```

**Ring-modulated metal** — `math { op: mult }` on two inharmonic pulses.
Sum-and-difference sidebands with no harmonic relationship: triangle, clave,
anvil. Spread the carriers; clustering them piles sidebands into a resonant
peak and it reads as a tone again.

```text
rm_a = vco { wave: pulse, freq: 1710hz }
rm_b = vco { wave: pulse, freq: 2350hz }
rm = math { op: mult }
rm_a -> rm.a
rm_b -> rm.b
rm_hp = svf { cutoff: 3khz, res: 0.08 }
rm -> rm_hp.in
```

**Pinged `ladder` tom** — the core kick with a four-pole filter, higher
pitch, gentler bend. `ladder` at `res: 0.96` rings longer and darker than
`svf` at 0.99; reach for it when the body should outlast the click.

```text
tm = ladder { cutoff: 150hz, res: 0.96, drive: 0.4 }
tm_click -> tm.in : 3
tm_p -> tm.cutoff : 220             # func { fall: 0.27, curve: exp }
```

## The knob that matters

**The ratio of pitch-envelope fall to amp-envelope fall.** Everything else is
taste. With `curve: exp` (stage time = the −60 dB time; the sweep is mostly
over by a third of it), 90 ms pitch into 1.95 s amp is an 808 and 36 ms into
570 ms is a 909; `lib/kick.sub`'s linear 6 ms into 95 ms is its
click-and-thud. Equal falls give you a tom.
Longer pitch than amp gives you a laser.

For pinged filters the second knob is resonance: below ~0.9 there is no
ringdown and you just hear the click.

## Modulating it

- Accent → filter/`modal` brightness *and* level, latched per hit with `sh`.
  Level alone reads as a volume automation; brightness is what sells dynamics.
- `euclid.hits` from an arrangement lane thickens a hat pattern into a drop.
- A glacial `lfo` on `euclid.rot` rotates the pattern over minutes.
- `func.fall` is an input: a lane into hat decay opens closed hats gradually.
- Detune the whole kit by cabling `v_oct` on the `modal` voices — `tune` is a
  compile-time `param`, but `body.v_oct` on the stamped internals is not.

## The kit voice contract (`patches/lib/kit/*.sub`)

Eight voices — `kick`, `snare`, `tom`, `rim`, `hat`, `hat_model`, `clap`
(plus `lib/kick.sub` and `lib/hat.sub`, the older non-kit pair) — share one
interface. Match it and your voice drops into `kit.patch` unchanged.

**Ports.** `trig` (input), `accent` (input, `default: 0.7`), `main` (output,
declared first so a bare instance name reads it). Hats add `choke`.

**Params.** `tune` — fundamental, Hz on the modal voices, v/oct on
`hat_model`. `decay` — **0..1 ring amount onto `modal.damping` on the modal
voices, but seconds on `clap.sub` and `hat_model.sub`.** That inconsistency is
real and documented in the files; check before you set it. Voice-specific
extras: `bend` (kick, tom), `snap` (snare), `tone` (hats).

**The accent idiom**, identical in all seven:

```text
acc = sh {}
accent -> acc.in
trig -> acc.trig            # latch accent at the hit, hold it through the tail
...
body = modal { ..., brightness: 0.19 }
acc -> body.brightness : 0.3    # base chosen so acc 0.7 lands on the target
lvl = vca { gain: 0.5 }
acc -> lvl.cv : 0.5             # level = 0.5 + 0.5 * acc
lvl -> main.in : 1.8            # per-voice makeup: all eight land in a ballpark
```

Two consequences. The brightness base is offset *downward* so the 0.7 default
is neutral (kick 0.19 + 0.3·0.7 = 0.40). And **cabling `accent` replaces the
default**, per rule 4 — `kit.patch` rides a `0.7 -> kick.accent` constant and
sums the LFO on top of it.

**Also required to be a good citizen:** a `demo.*` section so the file
auditions itself (`mise run play -- patches/lib/kit/yours.sub`), and a header
comment recording where the ratios came from (`docs/attribution.md`).

## Where it shows up

Every four-to-the-floor genre, obviously — but the pinged filter specifically
is techno (`dub-techno.patch`'s kick is the minimal version: `vco` synced to
the clock with an env on `v_oct`), the noise-burst clap is house and trance
(`mainstage.patch`), and the inharmonic stack is 808-lineage hip-hop and
electro. `kit.patch` is the physical-modeling counterpart: same contract,
`modal` in the resonance slot. `kick-solo.patch` is the audition rig.

## Push it further

1. Put a `follow` on the kick body and drive the snare's noise VCA from it —
   `kit/snare.sub` already does this; the wires respond to how hard the head
   is still moving, which is why it reads as a snare and not noise + tone.
2. Ping one filter from *another drum's* output instead of a click. A
   bandpassed hat exciting a low resonant `ladder` is a kick that inherits
   the hat's rhythm micro-texture.
3. Feed a pinged filter's own output back into its input through a `vca` at
   0.1 and a `dcblock`. It stops being a drum and becomes a resonator you
   have to tame.
4. Detune the whole kit with the harmony: one `stepseq` chord lane into every
   voice's `v_oct` makes the toms and cowbell chase the chord changes.
5. Replace the hat's six `vco`s with `noise -> vca -> modal.in` (a *driven*
   resonator, never a clean ring — the discovery behind `kit/hat.sub`) and
   compare spectra with `--stats`.
