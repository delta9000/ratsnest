# Modular Ambient & Generative Patching Technique: The Actual Craft

*Research agent report, 2026-07-20. Preserved verbatim.*

## 1. Generative / Self-Playing Patch Architectures

### The LFO web (ochd-style drift)

The core insight behind Instruō's **øchd** — designed in collaboration with
Ben "DivKid" Wilson — is that a generative patch needs cheap, abundant,
*non-synchronized* modulation more than it needs more voices. Øchd packs eight
analog LFOs into 4HP, and critically, "the default ranges are deliberately NOT
synced... free running LFOs that were tuned by ear to be musical for a range of
fast to slow modulations." Each frequency range was tuned by ear during
development "to give the optimum spread of control frequencies running in
parallel," and because the circuits are 100% analog, the LFOs drift and phase
against each other organically over time rather than locking into a fixed
relationship ([DivKid, øchd announcement](https://divkidvideo.com/ochd-the-second-divkid-eurorack-module/);
[Clockface Modular product page](https://en.clockfacemodular.com/collections/noise-chaos-generator)).

The patch-level technique this teaches: don't derive all your modulation from
one clock-divided source. Spread several free-running LFOs across octave-spaced
rates (roughly 0.05 Hz to 5 Hz) with no synchronizing reset, patch each to a
different destination (filter cutoff, wavefolder amount, pan, VCA level,
sub-oscillator detune), and the interaction of their un-locked periods alone
generates the sense that "something is always changing" without ever repeating
identically.

### Clock division trees and prime-ratio periods

Layering Euclidean rhythms across different pattern lengths is the rhythmic
analogue of the LFO web. As one synthesis reference puts it: "more complex
grooves can be constructed by layering different Euclidean rhythms on top of one
another, each with different *k* and *n* values and varying durations for the
underlying pulse... the sequence as a whole does not repeat for a number of
steps determined by the lowest common multiple of the pattern lengths"
([North Coast Synthesis, "What's Euclid got to do with it?"](https://northcoastsynthesis.com/news/whats-euclid-got-to-do-with-it/);
[Lawton Hall, "Euclidean Rhythms: Maximum Evenness, Maximum Groove"](https://www.lawtonhall.com/blog/euclidean-rhythms-pt1)).

Practically, this is why patchers deliberately pick clock divisions that are
coprime (3, 5, 7, 11...) rather than powers of two: two Euclidean layers at /3
and /4 realign every 12 steps and feel like a loop; layers at /5 and /7 realign
every 35 steps and feel alive. Modules like vpme.de's Euclidean Circles (three
or six independent sequences with per-channel length/fill/rotation) or Pam's New
Workout are the standard tools for this
([ModWiggler thread on Euclidean patterns](https://modwiggler.com/forum/viewtopic.php?t=257707)).

### Turing Machine looping-random and Marbles' déjà vu

The Music Thing Modular **Turing Machine** established the template for
"locked vs. fresh" randomness that later modules formalized: a shift register of
bits is regenerated each cycle with a probability knob controlling how many bits
flip, so at 12 o'clock you get true randomness, fully clockwise gives a stable
repeating loop, and fully counter-clockwise gives a repeating loop of *inverted*
bits. Mutable Instruments' **Marbles** built its Déjà Vu section explicitly
citing the Turing Machine as prior art for "a slowly evolving or lockable loop"
([Marbles manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/marbles/manual/)).

Concretely: "Déjà Vu allows the user to loop any generated CV patterns, as well
as 'sample' external CV, which can then be quantized, slewed, shifted and
attenuated. With T and X locked, Déjà Vu can be used to allow new random voltages
and triggers into the loop when rotated counter-clockwise, or when turned
clockwise Déjà Vu will start to randomly skip around the loop" (per the manual,
discussed on [ModWiggler](https://modwiggler.com/forum/viewtopic.php?t=253772)).

The patch-level takeaway used across both modules: run a self-patched feedback
loop (module output back into its own randomness-amount CV input) so the
*character* of the randomness itself drifts over a much longer timescale than the
notes/gates it's generating — a slow LFO into the "loop probability" knob turns a
fixed generative riff into one that gradually mutates and occasionally snaps back
to something you heard ten minutes ago.

### Probability gates and Bernoulli branching

Where Turing Machine/Marbles vary a *sequence*, Bernoulli gates vary a *routing
decision* per event: "a Bernoulli Gate... takes a clock or gate input and routes
it to one of two outputs, A & B, based on the value of an internally generated
Bernoulli Variable... at p = 0.5, the process is equivalent to a coin toss"
([m0xpd FLIP module documentation](https://github.com/m0xpd/FLIP);
discussion on [ModWiggler](https://www.modwiggler.com/forum/viewtopic.php?t=211210)).

In an ambient patch, a Bernoulli gate downstream of a clock is typically used to
decide, per pulse, whether a note goes to voice A or voice B (giving
call-and-response phrasing that's never mechanically alternating), or whether a
gate is allowed through at all (thinning a dense clock into a sparse, non-metric
event stream — the generative equivalent of a rest).

## 2. Ambient Sound-Design Primitives

### Drones from detuned/beating oscillators

The technique is nearly universal and its physics is well documented: "when two
tones are close together in frequency, they engage in an alternating pattern of
constructive and destructive interference heard as periodic undulations in volume
known as beating... the rate of beating is almost always the difference in the
two tones' frequencies"
([review of Radigue scholarship, Tandfonline](https://www.tandfonline.com/doi/full/10.1080/07494467.2024.2348272)).

At the patch level, forum consensus converges on: "a classic approach uses two
pairs of oscillators, each tuned to the note of a chord. Each pair goes to a
crossfader, wavefolder, and filter, then modulating each of these very slowly
creates a drone... getting multiple oscillators to beat against each other is
probably going to get you closer to drone territory than filter-sweeping a single
oscillator"
([ModWiggler, "Modules best for drones"](https://www.modwiggler.com/forum/viewtopic.php?t=212245);
[ModWiggler, "Essence of Drone"](https://www.modwiggler.com/forum/viewtopic.php?t=214126)).
A named variant: two slightly detuned triangle/sine oscillators with a slow
random voltage nudging one or both pitches, so the beat rate itself wanders
rather than sitting at a fixed Hz.

### Wavefolding for harmonic motion

Wavefolding reflects a waveform back on itself past a threshold, generating new
harmonics proportional to input amplitude — which is why it pairs naturally with
slow envelopes or LFOs on the fold-amount CV: rising amplitude buys you a
brightening, harmonically-richer timbre without touching a filter cutoff. This is
documented as "wavefolding's most natural habitat" being ambient and drone music,
citing artists like Kali Malone using Buchla 200 systems for "shimmering, complex
textures that evolve organically over time"
([Drey Andersson, "Wavefolding"](https://dreyandersson.com/music-production-terms/wavefolding/)).
The canonical West Coast patch is two detuned oscillators summed through a
Min/Max utility, then into a wavefolder, then a low-pass gate.

### Low-pass gates and Buchla strike behavior

The LPG is the signature primitive of Don Buchla's "West Coast" design
philosophy: it "combines amplitude control (VCA) with low-pass filtering (LPF) in
a single circuit, with the filtering and amplification tied together," typically
implemented with a **vactrol** — an LED shining on a light-dependent resistor,
where "the LDR's slow response to rapid changes in light intensity" is exactly
what gives an LPG its characteristic soft attack and long, non-linear decay rather
than a sharp VCA snap
([Perfect Circuit, "What is a Low Pass Gate?"](https://www.perfectcircuit.com/signal/what-is-a-lowpass-gate);
[Noise Engineering blog](https://noiseengineering.us/blogs/loquelic-literitas-the-blog/getting-started-lowpass-gates/)).

The named technique is the "strike": "patch a very brief AD-type envelope into
the CV input... used to 'strike' or 'ping' the Vactrol," which produces organic,
percussive-but-soft results — the famous "Buchla bongo" patch is "a wavefolded
sine wave... processed through a lowpass gate with a quick decay"
([Eddy Bergman, Buchla 292 build notes](https://www.eddybergman.com/2020/10/synthesizer-build-part-35-resonant.html);
[Baratatronix, Buchla Bongos patch](https://www.baratatronix.com/cascadia/cascadia-basic-buchla-bongos)).
Alessandro Cortini uses exactly this architecture on the Music Easel: "the
track's main drone typically uses one of the oscillators in the Easel's dual
oscillator... he uses one oscillator as a drone while changing the pitch with the
keyboard, with the envelope coming from the pulser"
([synthesizerbook.wordpress.com](https://synthesizerbook.wordpress.com/2011/04/10/alessandro-cortini-spends-the-day-with-a-buchla-music-easel-synthesizer/)).

### Karplus-Strong / modal resonators struck by noise

The technique models a plucked/struck string by feeding an impulsive excitation
(a noise burst, click, or sawtooth cycle) into a lowpass-filtered delay-line
feedback loop: "a burst of white noise as the excitation... tends to produce a
decaying metallic pluck reminiscent of a steel guitar or harpsichord," and it
"can also include any wideband signal, such as a rapid sine wave chirp... or a
single cycle of a sawtooth"
([Wikipedia, Karplus-Strong string synthesis](https://en.wikipedia.org/wiki/Karplus%E2%80%93Strong_string_synthesis);
[x37v.com writeup](https://x37v.com/x37v/writing/karplus-strong-synthesis/)).

Mutable Instruments' **Rings** productizes this as a modal resonator:
"transforms an external, unpitched excitation audio signal (such as a click, a
burst of noise, or whatever is captured by a contact microphone) into a
full-bodied pitched sound"
([Rings manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/rings/manual/)).
In ambient patches this is used generatively: a slow, sparse trigger source (a
Bernoulli-gated clock division, say) fires noise bursts into Rings, whose
resonant frequency is itself under slow LFO or quantized-random CV — yielding
bell-like events that emerge from silence without a "played" quality.

### Granular clouds and freeze

Émilie Gillet's **Clouds** (and its successor **Beads**) is the reference
implementation: incoming audio is written into a buffer and played back as
overlapping "grains" — "the maximum number of concurrent grains is quite large,
between 40 and 60," letting simple input (even a sine wave) become "lush ambient
pads." The **freeze** function is the key structural device for ambient use: "it
is possible, at any time, to FREEZE the audio buffer from which the grains are
taken — the incoming audio is no longer recorded... Clouds is the exact opposite
of a sampler: by default, the module always samples the audio it receives, except
when it is in the frozen state"
([Clouds manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/clouds/manual/);
[Perfect Circuit, Beads writeup](https://www.perfectcircuit.com/signal/mutable-instruments-beads)).
Freeze gated by a slow LFO or a rare random trigger is how a granular module
becomes a compositional event rather than a constant texture — a moment is
captured, held, then released back to live sampling.

### Reverb-as-instrument: shimmer and feedback

Shimmer reverb formalizes an idea ambient musicians used tape and analog delay for
decades before the dedicated effect existed: "shimmer reverb is a staple of
ambient music in which each note is followed by a reverberant wash of sound an
octave higher than the original... at its most basic, a shimmer reverb is a
pitch-shifted reverb tail in a feedback loop," where "the output of the pitch
shifter is fed back into the reverb before that too is sent back into the pitch
shifter... successive passes continue to shift up as they decay, leaving a bright
tail on the sound"
([Sound on Sound, "Creating Shimmer Reverb Effects"](https://www.soundonsound.com/techniques/creating-shimmer-reverb-effects);
[Valhalla Shimmer notes PDF](https://www.valhalladsp.com/shimmer/ValhallaShimmerNotes.pdf)).
The practical patch note: place a compressor before the pitch-shifter to keep the
recirculating tail dense, and add gentle chorus/ensemble modulation to the shimmer
path so the pitched-up feedback doesn't build up perceptible metallic beating
against itself.

### Tape-delay ecosystems and dub techniques

Deepchord/Echospace's Rod Modell describes the discipline as building "a sea of
tumbling chords and skittering delay" from "vintage analog equipment," with an
emphasis on "feeling the pressure zones shift" and mining "deeper for more unusual
tones" rather than defaulting to standard delay settings
([Textura interview](https://www.textura.org/archives/interviews/tenquestions_deepchord_soultek.htm);
[echospace313.com](https://echospace313.com/rod-modell)).
Hainbach's tape practice generalizes the same feedback-delay idea to consumer tape
machines: "how to use a cassette recorder as a budget cool tape echo effect without
any physical mod," noting that pitching the record/playback speed far enough
exposes the machine's own bias oscillator as an audible artifact — turning a flaw
into a sound source
([ModWiggler discussion](https://www.modwiggler.com/forum/viewtopic.php?t=206148);
[Taylor & Francis, "Hainbach and the Sound of Destruction"](https://www.tandfonline.com/doi/full/10.1080/08145857.2023.2216480)).

## 3. Structure Over Time Without a Timeline

Ambient patches substitute *rate of change* for arrangement. The forum-distilled
version: "for modular synth composition, the focus is on choosing the right
configuration of modules and the process of patching, with emphasis on cycles,
rates of change, and attenuating voltages to influence slowly evolving forms"
(paraphrased across [ModWiggler generative threads](https://www.modwiggler.com/forum/viewtopic.php?t=174144)).
The specific mechanisms:

- **Set the range, not the value.** The standard modulation-patching workflow is
  to "bias (offset, open) the destination first, then turn up the attenuator on
  your incoming LFO until you hear the desired effect" — you're deciding the
  *window* a parameter is allowed to wander in, then letting a slow or random
  source pick where inside that window it sits at any moment, rather than dialing
  a single "correct" value
  ([Ask.Audio, "Making Generative Music With Eurorack Synths"](https://ask.audio/articles/making-generative-music-with-eurorack-synths)).
- **Layered fade-ins / envelope followers as macro-structure.** Steve Roach
  describes treating the mixing console itself as the long-form instrument:
  "using pre and post sends to create spatial depth... dry signals for proximity
  and progressively pulling awareness deeper via reverb manipulation," with
  real-time EQ moves on reverb sends across a piece described explicitly as
  mimicking "the magic hour of sunset, how the light shifts over 20 minutes"
  ([Tape Op #161](https://tapeop.com/interviews/161/steve-roach)).
- **Drone-plus-event.** The recurring pattern across nearly every artist surveyed
  here (Rich, Roach, Radigue, Davachi, Malone) is a static or near-static drone
  bed with sparse, irregular events layered on top — the drone supplies
  continuity, the events supply the sense of "something happening" without needing
  a beat or verse/chorus. Robert Rich's *Sleep Concerts* — all-night performances
  beginning in 1982 at Stanford — are the extreme version: structure measured in
  hours
  ([Fact Magazine, "Dreamcatching"](https://www.factmag.com/2014/10/10/dreamcatching-the-remarkable-story-of-robert-rich-and-the-sleep-concerts/)).
- **Eno's incommensurable loops as a structure generator.** For *Music for
  Airports*, Eno built each piece from loops of deliberately different,
  non-integer-ratio lengths — documented lengths include a 20.1-second and a
  19.6-second loop running against each other, and in the earlier *Discreet Music*
  a 63-second loop against a 68-second one. Eno's own explanation: "they all repeat
  in cycles that are called incommensurable — they are not likely to come back into
  sync again"
  ([Reverb Machine, "Deconstructing Brian Eno's Music for Airports"](https://reverbmachine.com/blog/deconstructing-brian-eno-music-for-airports/);
  [Open Culture summary](https://www.openculture.com/2019/07/deconstructing-brian-enos-music-for-airports.html)).
  This is the tape-era ancestor of prime-ratio Euclidean layering above — the
  mechanism (near-coprime periods) is identical, only the medium changed. Eno
  reportedly also wanted "a silence at least twice as long as the sound" in each
  loop and preferred "complicated rather than simple relationships" between
  overlapping fragments.

## 4. Named Artists — Documented, Technique-Specific Practice

**Alessandro Cortini** builds Music Easel patches around a single-oscillator
drone (pitch played from the keyboard) gated by an envelope sourced from the
Easel's built-in pulser into the LPG — deliberately minimal, treating the
instrument as an "empty canvas" and a "bandmate" rather than executing a
pre-formed idea
([Red Bull Music Academy interview](https://daily.redbullmusicacademy.com/2017/03/alessandro-cortini-interview/)).

**Caterina Barbieri** centers her rig on Orthogonal Devices' ER-101 quad
sequencer — "the brain of my setup" — using its Arithmetic/Geometric/Random/Jitter
pattern operators to build a "generative grammar" of hundreds of real-time-varied
patterns, then uses a separate gate track to select which *portion* of a stored
pattern actually sounds at any moment, turning one sequence into many derived
phrases live. Later work (*Patterns of Consciousness*) restricts the entire piece
to one sound source, the Verbos Harmonic Oscillator, deriving melodic content by
filtering into its partials
([Sound on Sound interview](https://www.soundonsound.com/news/interview-minimalist-electronic-artist-caterina-barbieri)).

**Kaitlyn Aurelia Smith** treats unpredictability as a feature of the Buchla
100/200 hardware itself, describing patching as an act of discovering "what the
machine seemed like it wanted to do, rather than what she wanted it to do," and
noting environmental interference (room lighting, grounding) audibly changes a
patch — she leans into this rather than fighting it, and her live 200e rig runs
from a small, deliberately-limited set of ~17 hand-programmed presets
([MusicRadar interview](https://www.musicradar.com/news/kaitlyn-aurelia-smith-interview)).

**Suzanne Ciani** treats quadraphonic space itself as a patchable parameter, not a
mixing afterthought: "quadraphonic in electronic language is meant to be a
parameter like pitch and volume and rhythm... it's not about the sound, but the
way the sound moves" — spatial panning on the Buchla's dedicated spatial-CV
modules is composed and modulated exactly like pitch or filter cutoff
([Ask.Video interview](https://ask.video/article/audio-hardware/suzanne-ciani-reveals-her-buchla-quadrophonic-synth-performance-tips)).

**Éliane Radigue** built entire pieces around *battements* — beating between two
ARP 2500 oscillators tuned within a fraction of a Hz of each other, requiring, in
her words via analysis, "utmost delicacy of touch" since "tremendous patience [was]
needed to move the potentiometers by no more than a hair." She marked her patch
sheets with plus/minus symbols to denote intentional near-unison detuning, and
pointedly never touched the ARP's keyboard, following Buchla's own stated worry
that keyboards induce "piano habits" that lose the "sensory pleasures of gesture"
specific to the instrument
([Tandfonline, "Presence and Absence: Notes on Éliane Radigue's ARP 2500 Synthesiser Technique"](https://www.tandfonline.com/doi/full/10.1080/07494467.2024.2348272);
[Reverb News, "A Synth in Slow Motion"](https://reverb.com/news/synth-in-slow-motion-eliane-radigue-and-the-arp-2500)).

**Sarah Davachi** works from the same beating-oscillator physics as Radigue but
frames it as a listening discipline rather than a compositional trick: "setting two
oscillators on an analog synthesizer in tune and listening to them endlessly drift
in and out of one another" gives the "same simple joy" as the shimmering overtones
of a reed organ sustaining an octave or fifth — she treats pipe organ and modular
synth as equivalent drone sources differentiated mainly by the fixed acoustic space
the organ imposes
([zensounds.de interview](https://www.zensounds.de/p/sarah-davachi-interview)).

**Kali Malone** fuses Buchla 200 sine-oscillator drones with acoustic winds/brass,
all tuned in shared just-intonation systems developed with Stockholm microtonal
collaborators, so that the synthesizer's beating and the acoustic instruments'
overtones reinforce rather than clash
([Bandcamp Daily, "Eight Artists Making Music Built on Buchla Synthesizers"](https://daily.bandcamp.com/lists/bandcamp-buchla-synths-music)).

**Robert Rich** and **Steve Roach** independently converged on the same
architecture: layered analog step-sequencers with mismatched lengths, phased
against each other and re-lengthened live ("I could combine sequences... I'd start
changing the length of each sequence, then combining those phasing sequencer
patterns against each other" — Roach), on top of which slow filter/VCA automation
and mixing-console EQ moves supply the only "arrangement." Roach's *Structures From
Silence* is a single 30-minute take of an Oberheim OB-8 chord progression through a
DSX sequencer, manually shaded in real time rather than edited afterward
([Tape Op #161](https://tapeop.com/interviews/161/steve-roach);
[Fact Magazine](https://www.factmag.com/2014/10/10/dreamcatching-the-remarkable-story-of-robert-rich-and-the-sleep-concerts/)).

**Brian Eno**'s *Music for Airports* system is the tape-domain prototype for
prime-ratio generative structure: differently-lengthed loops of single sustained
notes and short piano phrases, chosen in advance to be harmonically compatible in
any combination, then simply set running and left to phase against each other — "I
just set all of these loops running and let them configure in whichever way they
wanted to"
([Reverb Machine](https://reverbmachine.com/blog/deconstructing-brian-eno-music-for-airports/)).

**Deepchord (Rod Modell)** builds dub-ambient techno from vintage analog gear with
an emphasis on deep, near-subliminal delay/reverb feedback rather than melodic
development — "feeling the pressure zones shift" is his own description of the
intended listening mode
([Textura interview](https://www.textura.org/archives/interviews/tenquestions_deepchord_soultek.htm)).

**Hainbach** builds pieces around the failure modes of tape and lab test equipment
specifically, using a cassette deck's own bias oscillator (audible only when the
machine is pushed past its intended speed range) as a sound source, and describes
his working method as: "my goal is to always make my own sounds and try to break
the machines"
([Taylor & Francis profile](https://www.tandfonline.com/doi/full/10.1080/08145857.2023.2216480)).

## 5. Forum-Accumulated Wisdom (ModWiggler / Reddit / Lines)

**Utilities over voices.** The single most repeated maxim across ModWiggler
generative/ambient threads: "utility modules are the inexpensive, dull polish that
makes the expensive, shiny modules actually shine" — with an explicit hierarchy of
priority: sound sources < sound modifiers < modulation sources < utilities, because
"an oscillator is an oscillator — utilities are possibilities"
([ModWiggler, module-priority discussion](https://modwiggler.com/forum/viewtopic.php?t=256101)).
Specifically called out: sequential/CV switches, comparators, and logic gates,
because they let "one part of the patch respond to what the other part is doing" —
the difference between a patch that merely plays and one that behaves. One
poster's direct testimony: "my ambient tracks improved greatly once I worked
sequential switches into my work flow. A switch and a probability trigger can
really open things up in a big way."

**Ambient needs looping/granular/sampling, not just more oscillators.** "Ambient
stuff is all about layers and textures, which either requires lots of voices or
requires some form of looping, sampling, or granular processing" — the forum's
recommended minimal generative-ambient starting kit is explicitly "a granular
sampler, a good reverb, and a source of modulation" before anything else.

**Patch-from-scratch vs. default patchings — a real, unresolved debate.** Some
practitioners deliberately tear down every patch after recording so nothing
calcifies into habit; others explicitly reject this for live work because "starting
from scratch every time leads to certain things always ending up in the same place"
and instead build a stable core of "default patchings" for consistency
([ModWiggler, live-rig design threads](https://www.modwiggler.com/forum/viewtopic.php?t=256773)).
Both camps agree on the failure mode to avoid: buying more gear to solve a patching
problem an existing system can already handle.

**Constrain the tonal palette, then let generative modules loose inside it.**
Recurring forum advice for ambient/minimal generative patches: use "a minimum of
two mid/upper range tones with a lower octave tone in a specific tuning or key
signature, occasionally transposing all this," with quantizers doing the harmonic
guardrail work so random/generative CV never produces an out-of-key note —
"quantizers being paramount for this classic ambient/minimal tonal approach"
([ModWiggler](https://modwiggler.com/forum/viewtopic.php?t=164962)).

**Set the range, not the value** (see §3) is stated almost verbatim across multiple
threads as the core generative-patching workflow: bias the destination parameter to
its center, then use an attenuator on the modulation source to define how far it's
allowed to swing — composing the *boundaries* of behavior rather than a fixed
instantaneous setting.

**Common beginner mistake:** treating a generative patch as "set it and walk away"
rather than as an instrument you still tend. Successful long-form ambient/generative
patches are consistently described as requiring occasional real-time intervention —
nudging an attenuator, refreshing a Turing Machine's register, re-triggering an
envelope — even if that intervention happens only once every few minutes; a patch
with literally zero human input tends toward audible stasis or, conversely, chaotic
drift. Forum posters flag this as the gap between "self-playing" (needs no input to
sound intentional) and "self-generating" (produces CV/gates autonomously, but still
needs curation).

---

## Cross-cutting synthesis for patch-authoring purposes

Every technique above reduces to the same handful of primitives recombined:

1. Multiple periodic sources at deliberately non-integer-ratio rates (LFOs, clock
   divisions, tape loops) so recombination never repeats on a human-perceivable
   timescale.
2. A locked-vs-fresh randomness axis (Turing Machine bit-flip probability, Marbles
   Déjà Vu, Bernoulli gate probability) that itself gets slow modulation so the
   *character* of the randomness drifts.
3. Strike/excite-then-resonate signal chains (LPG vactrol strike, Karplus-Strong
   noise burst into a resonator) rather than sustained-tone-into-filter, because
   struck-and-decaying envelopes read as "events" against a drone in a way
   sustained tones cannot.
4. Beating/detuning as the primary drone-generation mechanism, at a rate itself
   either fixed by ear (Radigue) or under slow random modulation.
5. Time-based effects (reverb, delay, granular buffer) used as compositional
   instruments with their own feedback paths, not terminal sends — shimmer,
   freeze, and tape-echo-into-itself are all the same idea of recirculating and
   slowly degrading a signal rather than merely wetting it.
