# Genre-Specific Modular/Electronic Synthesis Technique: A Patch-Level Reference

*Research agent report, 2026-07-20. Preserved verbatim.*

This report surveys how twelve electronic genres are actually built — oscillator
by oscillator, envelope by envelope, delay tap by delay tap — plus the
cross-genre mix/arrangement craft that applies regardless of style. It's aimed at
informing patch-authoring decisions, not historical trivia, so each section leads
with the mechanism.

---

## 1. Berlin School / Sequencer Music (Tangerine Dream, Klaus Schulze, Node)

**Tempo/meter:** Loose, often rubato at the outset (long non-metric intro
drones), settling into 4/4 sequencer pulses anywhere from ~90–140 BPM once the
sequence locks in. Pieces run 15–30+ minutes as a single continuous form rather
than looped song structure.

**Signature recipe — the sequencer as a performance instrument:**

- Core unit: an 8- or 16-step analog sequencer (e.g., the Synthanorma Sequenzer
  Klaus Schulze used on *Moondawn*) driving one or two VCOs. The pattern itself
  is simple and repetitive, but the *sequence is transposed in real time* by
  playing a keyboard that shifts the whole pattern's root pitch — this is why the
  parts feel like they "weave into each other more or less randomly" over a long
  piece even though the underlying step pattern never changes
  ([Chris Wirsig](https://blog.chriswirsig.com/2016/05/12/step-sequencing-like-klaus-schulze-the-easy-way/),
  [Vintage Synth Explorer](https://forum.vintagesynth.com/viewtopic.php?t=35770)).
- **Cross-delay / ping-pong feedback**: the sequencer output is split and sent
  through two delay lines (or one delay panned/cross-fed) with feedback, so that
  the delayed repeats interleave with the live steps, producing polyrhythmic
  counterpoint from a monophonic source. Forum consensus: "the best friend to a
  sequencer is a delay/echo unit — you can do ratcheting, quasi-polyphonic
  counterpoint things"
  ([ModWiggler](https://www.modwiggler.com/forum/viewtopic.php?t=144723)).
- **Phase-shifted arpeggios**: two sequencers (or one sequencer feeding two
  voices) run at very slightly different clock divisions or step counts (e.g., 16
  vs. 15 steps) so the patterns drift in and out of phase — a Reich-style phasing
  technique applied to gear rather than tape.
- **Filter opening over minutes**: a slow LFO or manual filter-cutoff automation
  opens the VCF from fully closed to bright over the course of many bars — the
  single most important "arrangement" gesture in the genre, since it's often the
  only dynamic element while the sequence itself stays locked.
- Gear: Moog Modular systems; tape echo (Roland Space Echo) and BBD delay units,
  plate/spring reverb for space
  ([Melodigging](https://www.melodigging.com/genre/berlin-school)).

**Canonical records:** Tangerine Dream *Phaedra* (1974) — cross-delayed Moog
sequences over a 17-minute side; Klaus Schulze *Moondawn* (1976) — first use of
the Synthanorma sequencer, real-time transposition technique; Node (1990s) —
updated the Berlin approach onto MIDI-synced modular/hardware step sequencers with
the same cross-delay logic.

---

## 2. Dub Techno (Basic Channel, Rhythm & Sound, Deepchord, Echospace, Fluxion)

**Tempo/meter:** 115–130 BPM, straight 4/4, deliberately minimal drum programming
(often just kick + a soft hat/rim, sometimes no snare at all).

**Signature recipe — the chord-stab-into-feedback-chain:**

1. A single chord stab (minor 7th, suspended, or ambiguous major/minor voicing) is
   played once or twice per bar, deliberately soft/short.
2. It's sent into a **filtered delay chain** — the archetypal move is a three-band
   filtered delay (each of 3 delay taps has its own filter, so the repeats
   darken/brighten independently) with feedback pushed near self-oscillation,
   emulating a tape echo's regenerative character
   ([Attack Magazine, dub delays](https://www.attackmagazine.com/technique/tutorials/creating-dub-delays-with-standard-plugins/)).
   Dotted-eighth delay time is the classic setting for the skank/ricochet feel.
3. The wet output is routed through a **reverb chamber** (plate-style), then a
   low-pass filter, then further "sub bass reinforcement," so that by the time the
   tail decays, the *decay itself* is the compositional content — Attack Magazine's
   schematic: Original sound → Delay/Feedback → Reverb Chamber → Low Pass Filter →
   Sub Bass Reinforcement → Silence/Decay Zone, cyclically, so the tail of one hit
   becomes the "attack" of the next perceived event
   ([Basic Channel-style dissection](https://www.attackmagazine.com/technique/beat-dissected/basic-channel-style-dub-techno/)).
4. **Tape/tube saturation** is applied before the delay send to generate harmonics
   that the filter can then sculpt — saturation, not EQ boosting, is how "warmth"
   and top-end presence get added
   ([Pheek's dub techno guide](https://audioservices.studio/production/pheeks-dub-techno-making-guide)).
5. **Hiss as texture**: tape hiss or vinyl-style noise is deliberately layered in
   rather than removed — noise floor is part of the sound's identity, not a flaw.

**Mix conventions:** everything above the stab/delay chain sits back in the mix;
sub-bass is kept narrow/mono and often the loudest single element; very few
simultaneous voices (drums, one bass, one chord voice, delay returns) — "economy of
means" is the explicit aesthetic
([Monumental Movement](https://monumental-movement.jp/en/column-basic-channel/)).

**Canonical records:** Basic Channel *Quadrant Dub* / the *BCD* series; Rhythm &
Sound *w/ The Artists*; Deepchord & Echospace *The Coldest Season* (2007) —
"vintage equipment including analog synthesizers and tape delay," field recordings
woven into the reverb tails
([Echospace bio](https://echospace313.com/deepchord)); Fluxion
([RA news](https://ra.co/news/39511)).

---

## 3. Detroit / Chicago Techno & Acid

**Tempo/meter:** Techno 120–135 BPM; acid house typically 120–130 BPM. Straight 4/4.

**TB-303 accent/slide mechanics** (the acid engine): each step in the 303's
sequencer carries three flags — note, accent, and slide/glide. **Accent** doesn't
just raise volume: it simultaneously widens the filter envelope amount and pushes
cutoff higher for that step, producing the "wow" — Roland's own internal name was
the "gimmick circuit"
([Ultimate Preset](https://www.ultimatepreset.com/roland-tb-303-acid-house-techno-guide/),
[MusicTech](https://musictech.com/guides/essential-guide/how-to-create-a-chicago-style-acid-house-bassline/)).
**Slide** (portamento) ties two adjacent notes into one continuous envelope stage
instead of retriggering — the envelope/filter don't reset, so the pitch glides while
the filter continues its existing decay, producing the "melting" legato quality. The
two features interact: an accented note gliding into an unaccented one produces the
classic bark-into-moan phrase shape.

Patch practically: a single resonant lowpass VCF with self-oscillation headroom, an
envelope with fast attack/short-to-medium decay routed to cutoff, and a sequencer
whose per-step "accent" output sums onto both the VCA level and the
filter-envelope-amount CV, while "slide" simply suppresses the envelope/portamento
retrigger for that transition.

**TR-909/808 drum synthesis:**

- **808 kick**: a single-cycle pulse (or the discharge of a bridged-T oscillator)
  driven into a self-oscillating filter; the filter's own resonant ringdown *is* the
  pitched "boom," with a pitch-envelope pulling the resonant frequency down over the
  decay ([Perfect Circuit](https://www.perfectcircuit.com/signal/kick-drum-synthesis)).
- **909 kick**: a pitch envelope sweeps a sine/triangle VCO from a higher starting
  pitch down to the fundamental, plus a short filtered noise burst and a transient
  click summed into a fast-decay VCA to sharpen the attack — this click is why 909
  kicks read as "punchier" than 808s
  ([Learn Music Production](https://learnmusicproduction.substack.com/p/classic-drum-machines-the-tr-909)).
- **808 snare**: same resonant-filter-as-oscillator trick as the kick, but excited
  by a noise burst instead of an impulse, giving a tone plus a noise "shell."
- **808 hats/cymbal/cowbell**: not filtered noise at all — **six square-wave
  oscillators** at inharmonic ratios, mixed and run through a CD4584 hex
  Schmitt-trigger inverter acting as a comparator/wave-shaper, then a VCA/envelope
  and (for the hats) a highpass filter to strip the fundamental and leave only the
  metallic beating of the six tones
  ([Hackaday](https://hackaday.com/2022/05/12/how-the-roland-808-cowbell-worked/)).
  The **cowbell** uses two of those square oscillators tuned to an inharmonic
  near-fifth (roughly 800 Hz and 540 Hz), mixed (not ring-modulated — the 808 circuit
  is a straight mix, contrary to popular belief) and run through two "swing type" VCAs
  that emphasize high harmonics before a bandpass filter cuts the fizz and low end
  ([Baratatronix](https://www.baratatronix.com/blog/606-cymbal-and-hi-hat-synthesis-nnfnz)).
- **909 hats/cymbals** are sample-based, which is part of why 909 and 808 kits are
  often blended — analog kick/snare punch plus sampled cymbal shimmer.

**Hoover/reese bass:** Two related but distinct lineages. The **Hoover** originates
from Eric Persing's Roland Alpha Juno "What The" patch — three oscillators an octave
apart, heavy PWM, thick chorus; it broke through via Second Phase's "Mentasm" (1991)
([Sonicstate](https://sonicstate.com/news/2026/03/03/the-hoover-a-joke-preset-which-defined-an-era/),
[Wikipedia](https://en.wikipedia.org/wiki/Hoover_sound)). The **Reese** proper traces
to Kevin Saunderson's "Just Another Chance" (1988) — also built on the Alpha Juno 2's
unison mode, but voiced as a sustained detuned-unison bass rather than a stab
([DOA](https://www.dogsonacid.com/threads/history-of-the-reese.259788/)).

**Canonical records:** Juan Atkins/Model 500 for the Detroit 909-driven template;
Phuture "Acid Tracks" for the 303 as lead instrument; DJ Pierre's "wiggle" technique
of live-tweaking cutoff/resonance while sequencing.

---

## 4. Drum & Bass / Jungle / Breakcore

**Tempo/meter:** 160–180 BPM (jungle/DnB); breakcore often 180–220+ BPM or
variable/polymetric. Half-time DnB feels like 80–90 BPM despite the same 170 BPM clock.

**Amen break chopping:** slicing the Winstons' "Amen, Brother" break into individual
transient hits, then re-sequencing those slices rather than playing the loop straight
— "a rite of passage for any producer working in jungle, DnB, or breakcore"
([KAN Samples](https://kansamples.com/blogs/learn/how-to-chop-amen-break)). Producers
layer programmed elements underneath the chopped break — a clean sub kick for weight,
a separate snare layer for crack, programmed hats for detail — because the raw sample
alone rarely has modern low-end weight.

**Timestretch artefacts as instrument:** early jungle producers (Akai S1000/S1100)
sped the Amen to 160–170 BPM and stretched it, and the resulting **granular smearing,
pitch-independent time artefacts, and transient smudging became a deliberate texture**
([breakcore.info](https://breakcore.info/amen-break/)).

**Reese bass:** built from two-plus detuned sawtooths — the detune amount sets the
beating/phasing rate directly; more detune = faster modulation
([Native Instruments](https://blog.native-instruments.com/reese-bass/)). A **sweeping
notch filter** placed after the oscillators, modulated slowly, is the classic DnB
"phaser" movement; a static midrange notch carved with a parametric EQ ("notched-out
Reese") creates the hollow, vowel-like quality heard in liquid and neurofunk
([Toolroom Academy](https://toolroomacademy.com/features/everything-you-need-to-know-about-the-reese-bass/)).

**Sub management / half-time feel:** sub bass is kept strictly mono and low-passed
(~80 Hz), sidechained to the kick to avoid masking; half-time tracks run the same
170 BPM grid but place kick/snare on the half-time positions, letting the drum
programming imply a much slower groove while breaks and hi-hats can still reference
full-tempo jungle patterns
([Mixgraph](https://www.mixgraph.io/mixing-guide/drum-and-bass),
[Melodigging](https://www.melodigging.com/genre/halftime)).

**Canonical artists:** Remarc, DJ Hype, Shy FX; Photek/Source Direct — glacial,
negative-space liquid arrangement over chopped breaks; neurofunk (Noisia, Black Sun
Empire) — FM-modulated Reese layering.

---

## 5. IDM / Braindance (Aphex Twin, Autechre, Boards of Canada)

**Tempo/meter:** No fixed tempo — deliberately variable, polymetric, or
algorithmically generated.

**Generative rhythm / algorithmic sequencing:** Autechre's evolution into
algorithmic process-driven composition is the throughline — patterns are generated or
perturbed by rule-based systems (Max/MSP patches, custom software, modular CV chains)
rather than hand-programmed step by step, producing rhythms that feel intentional but
never quite repeat ([Grokipedia](https://grokipedia.com/page/Intelligent_dance_music)).

**Granular/glitch:** granular synthesis — resynthesizing audio from tiny
(millisecond-scale) overlapping grains with independent pitch/position/density control
— plus glitch editing (buffer stutters, bit-crush, sample-rate reduction, codec
failure) are treated as the primary sound-design tools, not post-processing
([Loopmasters](https://www.loopmasters.com/articles/2887-An-Introduction-to-Granular-Synthesis)).
Aphex Twin's "Windowlicker"-era work exemplifies **micro-editing**: rapid,
sample-accurate manipulation of vocal/orchestral samples fused with glitch percussion
([Gearnews](https://www.gearnews.com/the-wizard-of-idm-how-to-sound-like-aphex-twin/)).

**Boards of Canada tape technique:** two layers of instability stack. First, the
**analog synths themselves are detuned** slightly. Second, everything is **bounced
back and forth across multiple tape machines** deliberately to accumulate wow/flutter,
hiss, and generational compression loss
([Lars Lentz](https://larslentzaudio.wpcomstaging.com/2026/01/10/deconstructing-the-boards-of-canada-boc-sound-and-music/)).
They still use physical 4-track tape today specifically for the "tape speed"
pitch-knob effect ([KVR](https://www.kvraudio.com/forum/viewtopic.php?t=474665)).
Slow-rate flanger/phaser and subtle reverb round out the "wobbly" timbre.

**Unquantized humanization:** deliberate micro-timing deviation (swing beyond a fixed
grid, per-hit velocity/timing randomization) at the sequencer/CV level rather than as a
mixing effect.

**Canonical records:** Aphex Twin *Selected Ambient Works 85–92* and *Windowlicker*;
Autechre *Confield* / *Draft 7.30*; Boards of Canada *Music Has the Right to Children*.

---

## 6. Drone / Doom / Dark Ambient

**Tempo/meter:** Often no pulse at all, or extremely slow (40–70 BPM implied but
without percussive onset).

**Sub-bass as felt, not heard:** the genre explicitly targets low-end you feel rather
than perceive as pitch — keep sub content prominent but restrained/smooth,
distinguishing true sub-bass from "rumble," where **rumble's identity comes from the
interaction of low frequencies with grain, resonance, and noise layered on top**, not
the low frequency alone
([Bluezone](https://www.bluezone-corporation.com/blog/the-sounds-of-dark-ambient-music-understanding-their-roles)).

**Resonant feedback as instrument:** a widely cited practical technique is misusing
reverb as a resonator — set reverb size to minimum and decay to maximum, then filter
and slowly modulate the size parameter, producing a metallic, feedback-like howl rather
than a spatial effect (in a modular patch: a short comb/allpass delay with feedback
pushed near self-oscillation, its delay time slowly modulated). Compose the piece's
sense of space using volume/pan/filtering *before* adding reverb/delay
([ModWiggler](https://www.modwiggler.com/forum/viewtopic.php?t=218207)).

**Field recordings and spectral masking:** raw environmental recordings (wind, water,
room tone, degraded electronics) are stretched until they lose their identifiable
source and folded into the drone bed; layers are frequency-carved against each other so
that a slow crescendo can be achieved purely by *unmasking* previously hidden layers
over time, rather than by adding new material — the crescendo is a filter-opening/reveal
move, structurally identical to the Berlin School filter-opening technique but stretched
over a much longer, less rhythmic timescale.

---

## 7. Kosmische / Motorik / Krautrock

**Tempo/meter:** Motorik is a relentless straight 4/4, typically ~120–140 BPM, with a
distinctive **even eighth-note kick/snare or kick/rim pattern** (Klaus Dinger's "Apache
beat") that never varies for the duration of a piece — repetition itself is the point
([Grokipedia](https://grokipedia.com/page/Motorik)).

**Beat anatomy:** kick on every beat (or every eighth), snare/rim reinforcing a steady
mid-frequency pulse, hi-hat often absent or minimal — the drum kit is played *live* but
with mechanical evenness, which is why it reads as "motor-like" rather than as a drum
machine.

**Tape echo as arrangement tool:** engineer Conny Plank (Neu!, Kraftwerk) used
tape-based delay units and multitracking to build loops that feel infinite — echo
applied to the drum kit specifically, to push the rhythm section back into a vast,
spacious field even though the takes were often single continuous performances.

**Sustained organ/synth drone under repetition:** guitars contribute arpeggiated,
interlocking ostinato patterns rather than riffs; synths layer melodic loops and
sustained tones on top, providing harmonic stasis against which the motorik pulse can
feel hypnotic rather than static — rhythm section = motion, synth/organ pad = stasis,
and the tension between the two is the composition.

**Canonical records:** Neu! *Neu!* (1972); Kraftwerk *Autobahn*.

---

## 8. Ambient Techno & Deep House Pads

**Tempo/meter:** Deep house 118–124 BPM; ambient techno can run house-tempo or drop to
non-danceable, beatless tempos for interludes.

**Chord voicing:** the house/deep-house vocabulary is built almost entirely from **m7,
maj7, m9, and 9sus4 voicings** — never bare triads — because the extended tones (7th,
9th) are what give the chord warmth and harmonic ambiguity rather than a resolved,
pop-functional sound ([Beatkey](https://beatkey.app/how-to-make-deep-house-music)).

**Sidechain as instrument, not utility:** sidechain compression in house music is
treated as a *rhythmic/musical* device. Typical settings: attack ~1 ms, release
120–180 ms, ratio 4:1, 4–6 dB gain reduction, applied to bass and pad layers keyed off
the kick ([Ben Rainey](https://www.benrainey.co.uk/blog/sidechain-compression-house-music)).
Pads, reverb tails, and background textures are all routed through the same ducking bus
so the entire mix "breathes" in sync; for tracks with syncopated or broken kick
patterns, the ducking envelope is manually automated rather than triggered directly from
the kick
([Ableton](https://www.ableton.com/en/blog/sidechain-compression-part-2-common-and-uncommon-uses/)).

**Arrangement minimalism:** classic deep house strips to one pad/Rhodes layer, one bass,
one vocal/lead motif — the discipline is subtractive.

---

## 9. Trance / Progressive

**Tempo/meter:** 132–140 BPM, straight 4/4, with the genre's identity substantially
carried by **macro-form** rather than sound design alone.

**Supersaw anatomy:** a common construction layers two saw-based oscillator stacks —
e.g., one with 12 unison voices, one with 6 — each independently detuned, so the wide
stack provides shimmer/body and the narrower stack provides definable pitch center; an
attack envelope as long as a full second gives the pad its characteristic slow "bloom"
([Computer Music](https://www.pressreader.com/australia/computer-music/20200422/282458531088363)).

**Gated pads:** an LFO (often synced to eighth notes) modulates a VCA or filter on a wide
supersaw pad, producing rhythmic "breathing" without programming discrete notes — the
"transgate" technique associated with acts like Above & Beyond
([Myloops](https://www.myloops.net/how-to-create-an-emotional-uplifting-trance-breakdown)).

**Pad layering by frequency zone:** a lush trance pad complex is built from three
distinct zones rather than three saws competing in the same range — a sub-anchor, a mid
"chord body," and a bright "air shimmer" pad high-passed above ~4 kHz with a slow filter
LFO or reverse-reverb send for movement
([Myloops](https://www.myloops.net/how-to-arrange-an-uplifting-trance-track-from-start-to-finish)).

**Macro-form — the defining trance skill:** the breakdown is a structured 32-bar arc:
strip (bars 1–8, removing rhythmic elements to expose melody) → rebuild (9–24, layering
elements back in) → lift (25–32, filter sweeps, riser, snare-roll acceleration) into the
drop. The drop itself is capped at 32–64 bars, organized as two 16-bar or four 8-bar
phrases each with a clear internal variation at the boundary
([Myloops](https://www.myloops.net/trance-song-structure-breakdown-basics)).

---

## 10. Modern Electro / Synthwave / Outrun

**Tempo/meter:** 80–118 BPM depending on sub-style, usually straight 4/4 with heavy
gated snare emphasis on 2 and 4.

**Gated reverb snare:** the single most load-bearing effect in the genre — heavy reverb
(often a bright plate or hall) applied to the snare, then a noise gate set to slam shut
the tail after a fixed, short window. Sources are blunt about its centrality: "the snare
absolutely must have gated reverb — this single effect defines the 80s sound more than
anything else" ([EDMProd](https://www.edmprod.com/how-to-make-synthwave/)).

**Bass arps and sidechain pumping:** arpeggiated bass (typically a square or saw at 16th
notes) sidechain-compressed against the kick/snare so the bass "breathes" with the beat
([ModeAudio](https://modeaudio.com/magazine/synthwave-5-production-essentials)).

**PWM leads:** classic square/saw lead voices with pulse-width modulation driven by a
slow LFO give the lead a subtle chorus-like movement without an actual chorus effect —
combined with pitch vibrato for expressiveness
([DailyMoss](https://www.dailymoss.com/how-to-make-synthwave-music-hardware-production-techniques-guide/)).

**Arrangement:** outrun favors cinematic build — sparse arpeggio or pad intro
establishing mood before the full gated-snare groove enters.

---

## 11. Bass Music / Dubstep / Neuro

**Tempo/meter:** Dubstep 138–142 BPM with half-time drum feel (snare on beat 3);
neurofunk sits within DnB's 170–176 BPM with the same half-time-perception drum
programming underneath dense bass.

**LFO wobble mechanics:** the wobble is fundamentally an LFO (triangle or sine,
tempo-synced) modulating filter cutoff on a resonant lowpass or bandpass filter; a
**delayed LFO** (fade-in modulation depth) is the classic move that gives a wobble its
"breathing in" character rather than starting fully modulated from note-on
([MusicRadar](https://www.musicradar.com/how-to/lfo-wobble-bass)).

**FM growl:** growl basses are built on FM synthesis with aggressive filtering stacked on
top — modulator-to-carrier FM amounts around 30–50%, often with the **FM index itself
modulated by a fast LFO** (near audio-rate) to create vocal-formant-like movement,
described as making "the synthesizer talk or sing"
([Gearspace](https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/1144312-dubstep-growl-scream-monster-vowel-bass-fm.html)).
Neuro-specific bass design layers this FM/warp movement over a Reese base
([Preset Drive](https://www.presetdrive.com/how-to-recreate-famous-dubstep-sounds-in-serum/)).

**Formant morphing:** formant filtering (fixed vowel-shaped bandpass peaks swept or
crossfaded) is the specific technique for "vocal bass."

**Mid/side and low-end rules:** sub built as a pure sine, mono, low-passed around 80 Hz,
sidechained against the kick; the growl/FM layer sits above the sub and is where stereo
width, distortion, and OTT-style multiband compression are applied
([EDM Templates](https://edmtemplates.net/blogs/edm-templates-blog/how-to-make-neuro-dnb-bass-in-serum-2)).

---

## 12. Modular Percussion Without Drum Modules

The core substitution principle: every classic drum-machine sound is really (a) an
excitation source, (b) a resonant/damped filter or oscillator bank, and (c) a shaping
envelope — none of which require a dedicated "drum module."

- **Pinging filters/resonators for kicks and toms:** send a short, sharp pulse (a
  VCA-gated click, or an envelope itself run at audio rate) into a **self-oscillating
  resonant filter**; the filter's own ringdown becomes the pitched body of the sound,
  with a pitch envelope on the filter's cutoff shaping the pitch sweep — this is
  explicitly how the TR-808 kick works (a twin-T resonant filter configuration)
  ([Perfect Circuit](https://www.perfectcircuit.com/signal/modular-synthesizer-filter-pinging)).
- **Noise + VCA + snappy envelope for snares/claps/hats:** a noise source into a VCA
  driven by a fast-attack, short-decay envelope, then shaped by a filter (bandpass for
  snare body, highpass for hats).
- **Ring-modulated metallic hits:** short envelopes sent into audio-rate LFOs/oscillators
  to ring-modulate two tones creates cowbell/clave/triangle-like metallic transients
  ([ModWiggler](https://www.modwiggler.com/forum/viewtopic.php?t=115402)).
- **808 cowbell reproduction:** two square-wave oscillators tuned to an inharmonic
  near-fifth (~800 Hz / ~540 Hz), summed (not ring-modulated in the original circuit),
  through swing-type VCAs that emphasize upper harmonics, then a bandpass filter.
- **808 hi-hat reproduction:** six square oscillators at inharmonic ratios, mixed, passed
  through a comparator/wave-shaping stage, then a fast-decay VCA and a highpass filter to
  strip the fundamentals, leaving the beating metallic noise band.
- Dedicated modules exist (e.g., Steady State Fate Entity: resonant bandpass filter + dual
  VC decay envelopes + multimode noise + VCA + diode wave multiplier) but they're built
  from exactly these primitives.

---

## Cross-Genre Craft

**Frequency-slot allocation:** give each element its own spectral pocket rather than
relying on level alone to separate parts — sub/kick own roughly 30–100 Hz, bass
body/growl owns 100–250 Hz warmth or 250–700 Hz for character/movement, pads and chords
are voiced or filtered to sit above that, and "air" layers are high-passed around 4 kHz+
specifically so they don't compete with lead/vocal presence.

**Sidechain ducking:** used two ways — as a *corrective* mix tool (~1 ms attack,
120–180 ms release, 4:1 ratio, 4–6 dB reduction) and as an *instrument* in its own right
(house's pumping groove, trance's gated pads, synthwave's breathing bass) — the mechanism
is identical; the genre distinction is whether the pumping is felt as a groove element or
hidden as invisible gain management.

**8/16/32-bar phrasing and tension curves:** dance genres near-universally organize
arrangement in powers-of-two bar blocks, with tension built via (a) filter opening/closing,
(b) rhythmic density increase (snare rolls, hat subdivision), (c) layer
accumulation/removal, and (d) pitch rise (risers) — trance's 32-bar breakdown
(strip/rebuild/lift) is the most codified version, but the same three-phase logic (reduce
→ rebuild → release) recurs in dub techno's decay cycle, Berlin School's filter-opening
arc stretched over 15+ minutes, and dark ambient's slow-crescendo unmasking.

**Mono-bass rule:** near-universal below roughly 100–150 Hz — bass, sub, and kick are
summed to mono because long low-frequency wavelengths go out of phase with themselves on
club systems and phone speakers alike, and the ear can't localize direction down there
anyway; engineers commonly start narrowing stereo content around 300 Hz and enforce full
mono by 120–150 Hz
([Mastering the Mix](https://www.masteringthemix.com/blogs/learn/how-to-add-width-to-bass-without-losing-mono-compatibility)).

**Stereo width by frequency:** width is added *above* the mono-bass threshold, generally
increasing with frequency: harmonics/growl content (250–700 Hz) can carry modest width,
mids get chorus/unison-driven width, and air/shimmer layers (4 kHz+) can be widest of all.
Measurement convention: mid/side analysis after a 200 Hz highpass
([TrackScore](https://trackscore.ai/blog/stereo-width-mixing)).

**Loudness/mastering conventions per genre:** club-oriented four-to-the-floor genres are
mastered hot and dense, generally tolerating heavier bus compression/limiting than
ambient/drone or IDM material, which is mixed and mastered with far more dynamic range
preserved specifically because its low end is meant to be "felt" at low playback volumes
rather than slammed; DnB/dubstep masters prioritize transient punch and sub translation
over sheer loudness.
