# Hi-hat synthesis — recovered deep-research report

> Recovered from workflow `w7glsxl2r` (deep-research), run 2026-07-19. The workflow
> completed but the prior session hit its usage limit before the report was read
> back. This is the mined material verbatim from the workflow output.

**Run:** 101 agents, 316 tool calls, 1329756 tokens.  
**Coverage:** 5 angles → 19 sources → 90 claims → 25 verified (6 confirmed, 2 refuted, 17 unverified).

## Question

How to synthesize a convincing hi-hat (closed and open) — established recipes and the acoustics behind them. Context: building a hi-hat voice for a modular software synth whose palette includes: a modal resonator bank with arbitrary per-mode frequency ratios (up to 128 bandpass modes, per-mode Q ladder, noise or impulse excitation), white/pink noise, state-variable and ladder filters, envelopes, VCAs, ring modulation (signal multiply), square/saw/sine/tri oscillators with FM, waveshaping/overdrive, and Karplus-Strong strings. Three failed attempts so far: (1) six sine modes at the 808 oscillator ratios high-passed at 4 kHz — read as glass clink; (2) same cluster lower with a bandpassed-noise sizzle layer — read as struck metal rod; (3) 40-mode dense odd-harmonic hash (808 bases × odd multiples) bandpassed at 8 kHz — better metrics but still not a hat by ear. Want: (a) how real hi-hats produce their sound physically (cymbal acoustics: mode density, nonlinear mode coupling, noise-like regime); (b) exactly how classic drum machines did it (TR-808/606 six-square + bandpass + envelope details incl. the two-stage decay and metallic beating; TR-909 sample+filter; CR-78, Simmons, etc.); (c) modern software/Eurorack approaches (Plaits/Peaks hat models, ring-mod recipes, FM hats, velvet noise, waveshaped noise); (d) what perceptually separates 'hat' from 'glass/rod/bell' (transient shape, spectral tilt, tonality, two-stage envelope) and which of these our palette can express. Deliver: concrete, numeric recipes (frequencies, ratios, filter types/cutoffs/Q, envelope times, mix ratios) ranked by likely fit to our palette.

## One-line summary

Synthesis step was skipped or failed — returning 6 verified claims unmerged.

## ✅ Confirmed claims

### 1. Cymbals driven at increasing amplitude pass through two bifurcations — periodic, then quasiperiodic, then chaotic — and the chaotic regime of a pure-tone-driven cymbal sounds like a normally struck cymbal; i.e., the characteristic 'cymbal' sound IS the chaotic broadband regime, not the discrete mode spectrum.
*Adversarial vote:* 3-0

> Two transitions are commonly observed: the first bifurcation can be explained by energy exchange between normal modes which are strongly coupled by an internal resonance relationship. After the second transition, the velocity signal exhibits a chaotic behavior. Although the excitation signal is a pure tone, it is important to notice that the chaotic regime sounds similar to real instruments normally excited by the strong impact of a mallet.

*Source:* https://www.researchgate.net/publication/49946650_Nonlinear_vibrations_and_chaos_in_gongs_and_cymbals

### 2. Cymbals and gongs are characterized by strongly nonlinear vibration; their sound is not that of a fixed linear mode set but of a wave-turbulence regime in which struck energy cascades across a wide band of modes, producing the broadband, noise-like spectrum.
*Adversarial vote:* 3-0

> The targeted application is the sound synthesis of cymbals and gong-like musical instruments, which are known for displaying a strongly nonlinear vibrating behaviour. This behaviour is typical of a wave turbulence regime, in which the wide-band spectrum of excited modes is observable in the form of an energy cascade.

*Source:* https://ensta.hal.science/ENSTA-PARIS/hal-01134639

### 3. Physically realistic cymbal/gong synthesis requires retaining a large number of modal degrees-of-freedom (a high-dimensional dynamical system), not a handful of partials.
*Adversarial vote:* 2-0

> The scheme can take into account a large number of degrees-of-freedom and is energy-conserving. ... implementation comments are given in order to treat efficiently the high-dimensionality of the resulting dynamical system.

*Source:* https://ensta.hal.science/ENSTA-PARIS/hal-01134639

### 4. Individual cymbal modes are high-Q resonances, with Q values on the order of 1000 as measured by the decay method — the noisy character of a cymbal does not come from individually damped (low-Q) modes.
*Adversarial vote:* 3-0

> all the modes involved had high Q values, of the order of 10 3 as measured by the decay method

*Source:* https://scholarship.rollins.edu/cgi/viewcontent.cgi?article=1020&context=stud_fac

### 5. In normal playing, a cymbal's broadband 'shimmering' sound results from a sharp asymmetric impact exciting essentially all vibrational modes at once, followed by nonlinear mode interaction and transitions to chaos — i.e., the noise-like character is produced by the instrument's dynamics, not present in the initial excitation alone.
*Adversarial vote:* 3-0

> In normal musical playing, of course, cymbals are supported at their centre and driven by a sharp asymmetrical impact, so that essentially all modes are sharply excited. Mode interaction and transitions of the kind discussed above then lead to a wide-band 'shimmering' sound.

*Source:* https://acoustics.asn.au/journal/2012/2012_40_3_Fletcher.pdf

### 6. Nonlinearity in thin metal shells causes progressive transfer of vibrational energy from low to high modes; in the Chinese tam-tam this transforms an initially low-frequency sound into a shimmering, high-pitched, genuinely chaotic sound over a timescale of about one second.
*Adversarial vote:* 2-0

> The nonlinearity thus leads to a progressive transfer of vibrational energy from low to high modes ... It is struck with a soft hammer and the low-frequency initial sound becomes transformed over a period of a second or so into a shimmering high-pitched sound that is actually chaotic [12].

*Source:* https://acoustics.asn.au/journal/2012/2012_40_3_Fletcher.pdf

## ❌ Refuted claims (do NOT rely on)

1. **Nonlinearity is essential to cymbal sound production: in normal playing the displacement is comparable to the metal's thickness, producing geometric nonlinearity, so a linear fixed-mode model (like a static modal bank) cannot capture cymbal timbre.**  — vote: 1-2  
   Source: https://www.researchgate.net/publication/49946650_Nonlinear_vibrations_and_chaos_in_gongs_and_cymbals
2. **In the chaotic regime the spectrum becomes broadband with no distinguishable discrete peaks (confirmed chaotic via positive Lyapunov exponent, converging to lambda = 0.02 per sample for a measured cymbal) — explaining why discrete sine/mode clusters read as 'glass' or 'bell' rather than a hat.**  — vote: 0-3  
   Source: https://www.researchgate.net/publication/49946650_Nonlinear_vibrations_and_chaos_in_gongs_and_cymbals

## ❓ Unverified claims (plausible, not adversarially confirmed)

1. An 18-inch crash cymbal supports more than 100 identifiable normal modes (plus many split degenerate mode pairs), i.e. real cymbal mode density is orders of magnitude higher than the 6-40 partials tried so far in the hi-hat patch attempts.  
   _src:_ https://scholarship.rollins.edu/cgi/viewcontent.cgi?article=1020&context=stud_fac
2. The characteristic shimmering/crashing sound of struck cymbals and gongs arises from nonlinear plate vibration described by wave turbulence theory, in which energy flux across frequency scales is statistical rather than a fixed set of linear modes — implying a linear (fixed-ratio) modal bank alone cannot reproduce a cymbal-like sound.  
   _src:_ https://mdphys.org/plates.html
3. Realistic cymbal/gong synthesis requires modeling nonlinear modal coupling that produces an energy cascade from low to high frequencies (turbulence), i.e., energy injected at low modes spreads upward in frequency over time.  
   _src:_ https://mdphys.org/plates.html
4. The wave-turbulence regime manifests audibly as an extremely dense, rich (noise-like) spectrum — the spectral hash that distinguishes cymbal sounds from discrete-mode bell/rod tones.  
   _src:_ https://mdphys.org/plates.html
5. The TR-808 cymbal's six Schmitt-trigger square oscillators run at nominal frequencies 205.3, 369.6, 304.4, 522.7, 359.4-1149.9, and 254.3-627.2 Hz, with oscillators 5 and 6 factory-trimmed to 800 Hz and 540 Hz respectively; each oscillator has ~47.98% duty cycle and 5 V amplitude.  
   _src:_ https://www.researchgate.net/publication/267630051_The_TR-808_Cymbal_a_Physically-Informed_Circuit-Bendable_Digital_Model
6. The summed oscillators are filtered by two active bandpass filters centered around 3440 Hz and 7100 Hz, which emphasize the squares' upper overtones and suppress their fundamentals — i.e., the audible 808 metallic content comes from high overtone hash, not the oscillator fundamentals themselves.  
   _src:_ https://www.researchgate.net/publication/267630051_The_TR-808_Cymbal_a_Physically-Informed_Circuit-Bendable_Digital_Model
7. The TR-808 cowbell, cymbal, open hi-hat, and closed hi-hat all share the same six-oscillator bank and the same design principle of filtering and enveloping rectangular waves; the cymbal voice splits this into three bands, each with its own envelope, nonlinear 'swing-type' VCA, and Sallen-Key highpass filter (highpass #3 resonating around 10500 Hz).  
   _src:_ https://www.researchgate.net/publication/267630051_The_TR-808_Cymbal_a_Physically-Informed_Circuit-Bendable_Digital_Model
8. Inharmonicity of the tightly-packed partials is essential to the cymbal/metallic character: when the oscillators are retuned to harmonically related frequencies (an A major chord), the sound loses its cymbal quality and becomes comb-filter/resonator-like — directly explaining why harmonic or odd-multiple mode clusters fail to read as a hat.  
   _src:_ https://www.researchgate.net/publication/267630051_The_TR-808_Cymbal_a_Physically-Informed_Circuit-Bendable_Digital_Model
9. The TR-909 cymbal and hi-hat voices are sample-playback, not analog synthesis: an address counter reads ROM sample data into a simple DAC, then a VCA shapes the envelope, then a low-pass filter removes the sample clock frequency.  
   _src:_ http://www.colinfraser.com/tr909/my909.htm
10. The 909's open and closed hi-hats share a single 32k ROM, with extra logic selecting which address range plays; open and closed hats are mutually exclusive (choke behavior is architectural).  
   _src:_ http://www.colinfraser.com/tr909/my909.htm
11. The 909 hat/cymbal samples are stored at 6-bit resolution with heavy amplitude compression, and the natural decay envelope is re-imposed afterwards by the analog VCAs — i.e., the stored waveform is roughly constant-level 'sizzle' and the envelope is applied in the analog domain.  
   _src:_ http://www.colinfraser.com/tr909/my909.htm
12. Plaits' 808-style hi-hat generates its metallic noise from six square-wave oscillators at the inharmonic frequency ratios 1.0, 1.304, 1.466, 1.787, 1.932, 2.536 against a nominal f0 of 414 Hz, summing only the sign bits of the phase accumulators (i.e., a sum of naive squares scaled by 0.33 and offset by -1).  
   _src:_ https://github.com/pichenettes/eurorack/tree/master/plaits
13. The metallic noise is shaped by a state-variable bandpass filter whose cutoff is 150 Hz scaled up by as much as 72 semitones with the tone control (clamped at 16 kHz), with Q of 1.0 (or 3.0 + 3.0*tone in the resonant variant), and after the VCA the signal passes a highpass SVF set to the same cutoff at Q = 0.5.  
   _src:_ https://github.com/pichenettes/eurorack/tree/master/plaits
14. The hi-hat uses a two-stage exponential decay envelope: while the envelope is above 0.5 it decays with coefficient 1 - 0.003*2^(-decay*84/12) per sample, and once below 0.5 it switches to a second rate 1 - 0.0025*2^(-decay*36/12); the trigger level scales with accent and decay as (1.5 + 0.5*(1-decay))*(0.3 + 0.7*accent).  
   _src:_ https://github.com/pichenettes/eurorack/tree/master/plaits
15. An alternative metallic-noise source for KR-55/FM-style hats is built from three ring-modulated (multiplied) square×saw oscillator pairs at carrier frequencies 200/7530 Hz, 510/8075 Hz, and 730/10500 Hz, all scaled by ratio = f0/(0.01 + f0); the file header states the whole model is an '808 HH' pushed toward crash-cymbal territory.  
   _src:_ https://github.com/pichenettes/eurorack/tree/master/plaits
16. Plaits' hi-hat model (model 16) synthesizes the hat as a metallic tone from multiple square oscillators, mixed with clocked noise, then passed through a high-pass filter and a VCA — a directly reproducible signal chain (square cluster + noise -> HPF -> enveloped VCA).  
   _src:_ https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/manual/
17. Plaits offers two concrete hi-hat topologies: the OUT variant uses 6 square oscillators summed into a dirty (nonlinear) transistor VCA, while the AUX variant uses three pairs of square oscillators ring-modulating each other into a clean linear VCA — i.e., ring modulation of square pairs is an established alternative metallic source to straight six-square summation.  
   _src:_ https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/manual/

## Synthesis — ranked recipes for the ratsnest palette

> This is the deliverable the workflow's synthesis stage skipped, done by hand
> from the verified claims above and the current `patches/lib/kit/hat.sub`.
> **Untested predictions** — DSL is runnable but none of these has been rendered
> or measured yet. Rank = predicted fit to palette × likelihood of reading as
> "hat" not "glass/bell", given the three documented failures.

### Why the first three attempts failed (the through-line)

Every failure shared one root cause the research names directly: **a cymbal's
sound is the chaotic / wave-turbulence regime, not a discrete mode set**
(confirmed 3-0, claims 1, 2, 5). Sparse or regularly-spaced partials — six sines,
odd-multiple hash — are *too structured*, so the ear resolves individual tones
and hears glass/bell/rod. Two hard constraints fall out:

- **Density + irrationality.** Real 18" crash = 100+ modes, high-Q (~10³),
  packed at *inharmonic* spacings. The 808 model explicitly *loses* its cymbal
  quality when its oscillators are retuned to harmonic ratios (unverified claim
  8). Odd-multiple ratios are still too near-harmonic — that's attempt 3.
- **Broadband hash, not ring.** The metallic content lives in *upper-overtone
  hash* passed by a high bandpass; fundamentals are suppressed (808: BP @ 3440
  & 7100 Hz; Plaits: HPF at cutoff). The current v4 "driven modal = colored
  noise" insight is correct and aligned — it just needs denser, truly-inharmonic
  ratios and a hotter high band.

---

### Recipe 1 — Ring-modulated square pairs + noise wash  ⭐ best palette fit

**Why first:** ring-mod of two square waves emits sum/difference sidebands of
every harmonic pair → an instantly dense, *inharmonic* hash from only 6 oscillators
— exactly the "dense metal without resolvable tones" the failures lacked, at a
fraction of a 40-mode bank's cost. This is Plaits' AUX/FM-hat topology (unverified
claims 15, 17). `math { op: mult }` **is** ring modulation in this palette.

```text
tune = param { default: 1.0 }   # scales all carrier pairs
# three ring-mod pairs (Plaits FM-hat carriers, Hz):
a1 = vco { wave: pulse, freq: 200hz }   b1 = vco { wave: pulse, freq: 7530hz }
a2 = vco { wave: pulse, freq: 510hz }   b2 = vco { wave: pulse, freq: 8075hz }
a3 = vco { wave: pulse, freq: 730hz }   b3 = vco { wave: pulse, freq: 10500hz }
r1 = math { op: mult }   a1->r1.a  b1->r1.b
r2 = math { op: mult }   a2->r2.a  b2->r2.b
r3 = math { op: mult }   a3->r3.a  b3->r3.b
metal = mix {}           r1->metal.in  r2->metal.in  r3->metal.in
hp = svf { cutoff: 6khz, res: 0.15 }   metal->hp.in   # kill fundamentals
# + a noise wash (real-sample lesson): broadband bed under the hash
nz = noise { color: white }   wash = svf { cutoff: 8khz, res: 0.1 }  nz->wash.in
# two-stage env → VCA (Plaits: fast stage, then slower below 0.5)
env1 = func { rise: 0.0002, fall: 0.015 }
env2 = func { rise: 0.0005, fall: decay, curve: exp }
```
Mix ≈ `hp.hp : 0.7` + `wash.bp : 0.5` into the enveloped VCA. **Closed** decay
≈ 0.05 s, **open** ≈ 0.35 s, choke as today.

#### Built & measured 2026-07-19 — the literal recipe FAILED, tuned version works

Rendered and measured against `hat_samples/` with `hat_analyze.py`. Nine
iterations (`scratchpad/lab/r1*.sub`, winner = `r1_final.sub`). What the metrics
taught, in order:

- **The literal Plaits recipe scored worst of all** — 3 ring pairs are too
  *sparse*: peak/median 40–48 (target ≤12), i.e. the sidebands read as discrete
  tones, the exact glass/bell failure again. Centroid 10.7 kHz (too piercing).
- **Fix 1 — density + noise dominance.** Five ring pairs with *spread* carriers
  (3.5–9 kHz, not clustered) + a broadband **low-res** highpass, with the noise
  wash *louder* than the metal (metal 0.22, wash 1.25). Any resonant (high-`res`)
  bandpass immediately reintroduced peaks — broadband/low-res is what keeps it
  noise-like. Dropped peak/median 40 → **9**.
- **Fix 2 — centroid via a non-resonant lowpass ceiling, NOT pink noise.** Pink
  noise starves the high end so the metal sidebands stick out (peak/median back to
  36–46). White noise's flat top *masks* the metal; a gentle `svf` lowpass ceiling
  (~7 kHz, res 0) pulls the centroid down without adding any peak.

As-built winning params (`r1_final.sub`): 5 pulse ring pairs at carriers
200/517/730/941/1273 Hz × 3510/4830/6250/7570/9110 Hz; metal `svf.hp` @ 3.5 kHz
res 0.1 → drv 0.22; **white** `noise` `svf.hp` @ 5 kHz res 0.05 → drv 1.25;
two-stage env; non-resonant `svf.lp` ceiling @ 7 kHz.

Measured (closed decay 0.05 s / open 0.3 s):

| voice | flatness | centroid | peak/median | verdict |
|---|---|---|---|---|
| **Recipe 1 closed** | 0.210 | 9.0 kHz | 9 | in-range, bright |
| **Recipe 1 open**   | 0.200 | 6.0 kHz | 9 | in-range |
| current v4 closed   | 0.384 | 3.0 kHz | 5 | in-range, dark |
| current v4 open     | 0.217 | 4.2 kHz | 12 | in-range |
| acou_closed (real)  | 0.218 | 2.9 kHz | 8 | — |
| acou_open (real)    | 0.143 | 4.8 kHz | 10 | — |
| oh808 (real)        | 0.157 | 7.8 kHz | 12 | — |

**Honest conclusion:** Recipe 1 produces a legitimate hat (tonality nailed once
tuned) but does **not** beat the already-well-calibrated v4 — it lands on the
*brighter, TR-808/electronic* side of the reference set (centroid 6–9 kHz ≈ oh808
7.8), whereas v4 matches the *darker, acoustic* references (centroid 3–4 kHz ≈
acou 2.9–4.8). It's a valid **alternative voice**, not a replacement. The research
hypothesis held — dense inharmonic ring-mod hash + noise reads as a hat — but only
under white-noise dominance with spread carriers and a lowpass ceiling; the literal
sparse recipe did not.

#### Follow-up 2026-07-19 — "still sounds woody" → cascaded high-pass

By ear the promoted voice read as **woody/wood-block**, which the 2–16 kHz metrics
missed entirely — they don't look below 2 kHz. A sub-2 kHz analysis (`low_analyze.py`)
found the culprit: a **sustained pitched ~1.3 kHz partial** (peak/median 4276 on the
open tail vs 66–300 for real hats), a low-mid ring-mod *difference* tone that the
single 12 dB/oct metal high-pass passed and the long tail let ring. Fix: **cascade a
second `svf` high-pass (24 dB/oct at 5 kHz)** on the metal path. The pitched peak
dropped to 65 (real range), `<2k/>2k` energy ratio to 0.00, and every hat metric
held or improved (peak/median 7, flatness 0.30, centroid 7.2 kHz). Lesson recorded:
**a metric window that starts at 2 kHz is blind to woodiness; always check the
low-mid band against real samples.** The shipped `hat_model.sub` carries this fix.

---

### Recipe 2 — 808 / Plaits six-square through a high bandpass  (canonical, numeric)

**Why second:** fully specified numbers, but *summed squares alone* are what read
as glass in attempt 1 — it survives only because the high bandpass turns the stack
into overtone hash and the noise wash fills the gaps. Use as an A/B reference.

Six pulses at the **Plaits ratios × f0 = 414 Hz** (unverified claim 12) →
414, 540, 607, 740, 800, 1050 Hz:
```text
o1..o6 = vco { wave: pulse, freq: <each> }
sq = mix { }                       # sum the six
bp1 = svf { cutoff: 3440hz, res: 0.3 }   # 808 bandpass centres
bp2 = svf { cutoff: 7100hz, res: 0.3 }   # (unverified claim 6)
```
Sum `bp1.bp + bp2.bp`, then a final HPF ~9.6 kHz (Plaits: 150 Hz × 2^(72/12),
Q 0.5) → two-stage env VCA. Plaits' single-SVF variant is equivalent: one
`svf { cutoff: 9.6khz, res: <Q≈1> }` bandpass then HPF at the same cutoff.

---

### Recipe 3 — Refined driven-modal (evolve current v4, the "physical" path)

**Why third:** already committed and validated by the research ("physically
realistic cymbal synthesis needs many modal DOF" + "driven resonator outputs
colored noise"). Two concrete upgrades from the findings:

1. **Replace odd-multiple ratios with irrational, cymbal-measured spacings.**
   Odd multiples are still near-harmonic (attempt 3's residual glass). Push
   `structure` off 0.25 to stretch the bank inharmonically (`modal` warps listed
   ratios by `2^((structure-0.25)*2)`), or hand-pick 48–64 non-commensurate ratios.
2. **Raise mode count toward the cap (current 40 → 64+, up to 128)** and hotter
   high band — real crash is 100+ high-Q modes. Keep the continuous noise drive.

---

### Recipe 4 — FM metal (fewest modules, if 1–3 stall)

`fm` with deliberately inharmonic ratios and `abs`/`fold` operator waves gives
a bright, aliasing-by-design metallic tone under a fast `curve: log` envelope
(the 909 sample path is unavailable — we have no ROM). Lowest priority: less
directly grounded in the confirmed claims than 1–3.

---

### Recommendation

Build **Recipe 1** first — it's the cleanest palette fit and attacks the exact
defect (density + inharmonicity) all three failures shared, with only 6 VCOs +
3 multiplies. Keep the committed v4 as the fallback and A/B against it with the
same measurement harness (`hat_analyze.py`: flatness 2–16 kHz, tail ms, centroid,
peak/median) plus the real-sample references in `hat_samples/`. Want me to build
Recipe 1 into a `hat_lab/` variant and measure it against the samples?

## Sources

1. [primary] https://www.researchgate.net/publication/49946650_Nonlinear_vibrations_and_chaos_in_gongs_and_cymbals  (5 claims)
2. [primary] https://ensta.hal.science/ENSTA-PARIS/hal-01134639  (5 claims)
3. [primary] https://scholarship.rollins.edu/cgi/viewcontent.cgi?article=1020&context=stud_fac  (5 claims)
4. [primary] https://acoustics.asn.au/journal/2012/2012_40_3_Fletcher.pdf  (5 claims)
5. [secondary] https://www.soundonsound.com/techniques/analysing-metallic-percussion  (5 claims)
6. [primary] https://mdphys.org/plates.html  (5 claims)
7. [blog] https://www.baratatronix.com/blog/cascadia-808-cymbal-hi-hat-synthesis  (5 claims)
8. [blog] http://joesul.li/van/synthesizing-hi-hats/  (5 claims)
9. [primary] https://www.researchgate.net/publication/267630051_The_TR-808_Cymbal_a_Physically-Informed_Circuit-Bendable_Digital_Model  (5 claims)
10. [blog] https://www.baratatronix.com/cascadia/cascadia-808-hi-hat  (5 claims)
11. [unreliable] https://www.modwiggler.com/forum/viewtopic.php?t=120280  (0 claims)
12. [secondary] https://docs.cherryaudio.com/misfit-audio/transistor-808-hats  (5 claims)
13. [primary] http://www.colinfraser.com/tr909/my909.htm  (5 claims)
14. [secondary] https://www.soundonsound.com/techniques/synthesizing-realistic-cymbals  (5 claims)
15. [primary] https://github.com/pichenettes/eurorack/tree/master/plaits  (5 claims)
16. [primary] https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/manual/  (5 claims)
17. [secondary] https://www.soundonsound.com/techniques/practical-cymbal-synthesis  (5 claims)
18. [blog] https://noiseengineering.us/blogs/loquelic-literitas-the-blog/hats-off-to-this-series-synthesizing-hats-in-the-final-percussion-synthesis-post/  (5 claims)
19. [blog] https://noiseengineering.us/blogs/loquelic-literitas-the-blog/creating-fm-hats/  (5 claims)
