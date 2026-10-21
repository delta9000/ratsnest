# Patch craft: hard-won authoring lessons

Agent-facing lessons derived by building and verifying arranged tracks
(`mainstage`, `afterglow`, `fathom`). These are the things that cost a probe
render, a failed check, or a quiet master to learn — not the things
`docs/modules.md` already documents. Read `docs/modules.md` first; read this
before writing a song-length or mastered patch.

## Measure the render; audition the music

`ratsnest render patch -d Ns -o out.wav --stats 4s` produces a per-window
peak/RMS timeline. It catches technical faults and helps locate arrangement
changes. It cannot establish whether the balance, groove or transition works
for a listener. A limited kick can still overwhelm the music.

- **Arrangement timing**: each section boundary should show an RMS step at
  the expected second (drop slams +4 dB, breakdown dips −7 dB, fade reaches
  −240 by the end). If a boundary lands at the wrong time, the grid or a div
  is off.
- **Loudness examples**: mastered EDM drop windows ≈ −12 dBFS RMS with
  −0.4 dBFS peaks (crest ~11 dB = loud, not bricked). Breakdown should sit
  6–8 dB below the drops. Intro ≈ −25 dB RMS or lower.
- **Hygiene**: overall peak ≤ the limiter threshold, `dc` ≈ 0, no flat
  peak==RMS windows (that's a brick), no dead air after the fade.

Use measurements to investigate a specific concern, then audition the result.
Float renders preserve overs, so a peak above 0 dBFS is useful evidence of
overload. Live output clips at the device boundary; an explicit limiter remains
the way to control peaks. Compressor/clipper dry-wet blends and hot reload now
use linear interpolation, so old equal-power gain boosts are gone. Compressor
and limiter detectors peak-hold for `min(release, 20 ms)` (clean bass, but a
slow-attack glue comp now reduces 2–3 dB more; raise its makeup). Rebalance
older mastered patches by measurement rather than assuming old numbers still hold.
Those EDM levels are calibration examples, not acceptance criteria for every
style. Compare at similar playback loudness. Render a short, full-tempo clip
that includes the approach, entrance and settled groove; a good steady groove
does not validate how it arrives. Treat listener feedback as musical evidence.

`--start Ns` captures a later window (`--start 10m -d 60s`) and reports
absolute offsets, but it does **not** skip the work: the graph is stateful,
so it still runs from zero. For a song-length patch the cheap probe is to
divide the arrangement grid instead — drop the divisor that clocks the
tracker lanes by 10-15x and the whole form plays in a minute. That verifies
section boundaries, lane crossfades and grid wrap; it does *not* verify
anything with a real time constant (reverb tails, multi-second `func` slews,
tempo ramps, granular freeze windows), which still needs a full pass.

## Verify timbre, not just level

`--stats` is a level meter. It cannot see a drone that is supposed to be
darkening, a filter arc that is being cancelled by something else rising, or an
unmask that revealed the wrong layer. For any piece whose subject is timbre
rather than dynamics — drones, generative beds, anything with no beat — level
is nearly constant by design and the timeline tells you nothing.

Split the render into bands and compare head against tail:

    W=take.wav
    band () { ffmpeg -hide_banner -ss "$2" -t 30 -i "$W" \
                -af "$1,astats=measure_perchannel=none" -f null - 2>&1 \
              | awk '/RMS level dB/{print $NF; exit}'; }
    for f in "lowpass=f=120" "bandpass=f=400:w=500" \
             "bandpass=f=2000:w=2000" "highpass=f=6000"; do
      printf '%-24s %8s %8s\n' "$f" "$(band "$f" 0)" "$(band "$f" 250)"
    done

Read the deltas against what the header comment claims. "It gets darker" means
the high band must fall relative to the low one; if both fall equally you wrote
a fade, not a filter. A rising resonant layer cancelling a falling master
lowpass shows up here as a flat top band and nowhere else.

Do this once per piece before believing your own arrangement note. It is the
only verification the drone genres have.

## Arrangement grid semantics (probed, not documented)

A stepseq clocked by the div chain plays **step 0 at t=0** — there is no
first-edge skip. (`sausage.patch` and `power-metal.patch` have contradictory
comments about this; the probe settled it. Trust renders over comments.)

The real hazard is **wrap**: the grid loops, and the intro comes back at
`num_steps × block_seconds`. Always end lanes with silent guard steps and
choose a render duration that ends after the fade but before the wrap:

    wrap_time = lane_steps × (240 / bpm)     # 4-bar blocks
    render    = fade_end + ~3 s cushion < wrap_time

`func` with nothing on `trig` is the section fader (lane CV → `func.in`).
Asymmetric rise/fall is the whole trick: risers attack slow (6–15 s) and
release fast (0.4 s) so the drop lands clean.

## Mastering choices

An explicit mastering recipe is `dcblock → eq → compressor(external kick sidechain) →
stereo width → clipper(soft) → limiter` (see `halcyon`/`ion`/`mainstage`).
These settings are starting points, not neutral defaults:

- **A post-limiter attenuation scales the ceiling.** The idiom
  `-1 -> out.level; main_out -> out.level : 0.8` fades the whole track, but
  at full play the 0.8 turns a −0.4 dBFS limiter ceiling into a −2.3 dBFS
  master — your render comes back ~2 dB quiet and no amount of clipper
  drive fixes it without crushing. Use `out.level : 1.0` and let the limiter
  own the ceiling.
- **Working backwards from loudness**: premaster peak ≈ −3 dBFS, glue comp
  makeup +6.5..8.5 dB at mix ~0.85 (blends are linear now; the old +1.5..3 leaned on the equal-power boost), clipper drive 1..6.5 dB, in dB (parallel mix ~0.3), limiter
  −0.4 dBFS. Kick channel level ~0.85 with the kick saturated to ~±1 sets
  the track's peak anchor; everything else stages under it.
- The pump is three layers deep, not one: per-channel `kick_env -> mix.lvlN
  : -0.2`, the same on the FX returns, plus the external-sidechain glue comp
  on the master bus. One layer alone sounds like a demo.

## Input bases sum — zero them first

Cables sum onto input bases. Two classic self-owns:

- `tempo.seconds -> dly.time` with the default `time: 0.4` base gives a
  0.75 s delay, not a dotted 8th. Write `delay { time: 0ms }` and let the
  tempo module own the input outright.
- `math` now defaults both inputs to zero. Multiplication needs an explicit
  factor or a cable into `b`; addition no longer adds an accidental 1.
- Filter `cutoff` sums in Hz, so bipolar modulation can move below the declared
  base. Use `v_oct` for octave tracking: effective cutoff is
  `(base + Hz cables) * 2^(v_oct cables)`. This keeps low notes from requesting
  negative cutoffs.

## Absolute pitch and transposition

`A3v = 0.0` uses a 220 Hz reference. The default `vco`, `fm`, and `additive`
base is 220 Hz, so note literals sound at their written pitch. Setting `freq`
to 110/440 transposes one octave down/up; setting it to 55 transposes two down.
Other bases transpose by other intervals. Use bare fractions for relative
intervals (`1` = octave, `0.0833333` = semitone), and uppercase note names for
absolute pitch (`D2v`). Lowercase names in numeric arrays are validation errors.

For a harmonic stack, use `additive { levels: [...] }` and one external
amplitude envelope. Use `ratios` for inharmonic partials, `phases` for their
relative phases, and `tilt` for live spectral balance. This avoids repeating
FM envelope arrays when there is no phase-modulation routing.

Noise choice is independent of envelope/filter choice: `noise` offers white,
pink, brown, blue, and violet spectra; `dust` supplies irregular impulses;
`random` supplies slow stepped/smooth CV. Different colors have different RMS
levels. Render at comparable levels before judging timbre.

## Free-ringing voices ghost; ADSR voices don't

`string`, `lpg`, and `modal` ring after their trigger. If an arrangement
lane fades one *in* while it happens to be ringing from a strike during its
silent section, a ghost note swells in from nowhere. Gate the strike itself
with the **stepped** lane. `func`/`adsr`-driven VCAs start from zero every
note and never have this problem.

**Do not use `logic { op: and }` for this.** Boolean ops threshold their
inputs at 0.5, and arrangement lane values are routinely below that — a lane
sitting at 0.35 mutes the voice completely for the whole section (measured:
−240 dBFS, i.e. digital silence). Compare against a floor instead, which
passes for any lane that is meaningfully on:

    gate_on = logic { op: cmp }     # cmp tests a > b
    arr_voice.cv -> gate_on.a
    0.06 -> gate_on.b
    strike_gate = logic { op: and }
    mel.gate -> strike_gate.a
    gate_on  -> strike_gate.b       # now a 0..1 gate, not a level

Verified: lane 0.05 stays silent, 0.35 and 0.95 both pass at full level.

The stereo version of the same bug: a voice muted by a mixer lane still
bleeds into reverb/delay if the send is tapped pre-mute. Gate the voice
*before* its sends (a post-fader VCA, e.g. `stab_out`), then tap the sends
from there. (`meridian.patch` demonstrates this; it's why its stabs vanish
cleanly instead of leaving reverb ghosts.)

## `curve: exp` times are −60 dB times

`curve: exp` on `adsr`/`func`/`fm` is a true RC decay normalized to land on
time, so its stage time is where the tail reaches −60 dB; it passes −20 dB at
a third of the stage. (Before it became a true exponential, `exp` was `x²`,
now `quad`, which holds near the top and passes −20 dB at 95% of the stage.)
When converting a linear or `quad` decay, or a number from an older note:

- **Time ×3** keeps the −20 dB point (the heard length) where it was.
  A squared amp (`vca { response: exp }`) halves every dB, so it needs ×5;
  so do `fm` segments, whose brightness follows the index nonlinearly.
  A tail summed under a louder transient (the kit hats) needs more: measure.
- **Amp depth ×1.4 (+3 dB)** roughly restores the level: a natural decay
  carries ~4 dB less energy than a held one of the same heard length, at the
  same peak (×1.2 on the cv of a squared amp). Measured across the 2026-10
  rebalance: percussion bands back within ~2 dB (most within 1), mastered
  tracks within ±0.5 dB RMS.
- **Keep `quad`** where the hang is the point: bar-gated pads and subs that
  "swell per bar" (the release bridges the gate's low half) and slow
  arrangement arcs that should hold tension and then accelerate.

## Composition idioms that read as "composed"

Learned by diffing the good patches (`meridian`/`halcyon`/`ion`) against
the flat ones:

- **Entrances need phrase cues.** In the first `pressure-gradient`, a kick
  faded up over about thirteen beats at bar 61. Its timing was regular, but
  the listener found its arrival unpredictable. Gate the strikes on a clear
  phrase boundary and let each envelope finish; keep long fades for the room
  and sustained textures. Check the kick's tail against the bass onset.
- **Activity changes alone do not create tension and release.** Establish a
  motif, make a directed change in density or harmony, leave a deliberate
  breath, then resolve on a recognizable downbeat. Keep the main rhythm
  dependable while varying accents and tone around it.

- **Chord lanes re-voice everything.** Three bar-clocked stepseqs — root,
  third, fifth as absolute v/oct (note names) — feed bass, pad, arp, and
  lead alike. The whole track re-harmonizes from three numbers.
- **`switch { mode: seq }` is the melody generator.** An eighth-clocked
  switch stepping a fixed 8-step contour through the (octave-lifted) chord
  lanes makes an arp/riff that is consonant over every chord. `math { op:
  add }` with `b: 1` is the octave lift. An `sh` after the switch holds
  pitch through each note's decay so ringing notes never retune mid-tail.
  Give that switch `fade: 0`: its default 5 ms declick slew would glide
  the pitch, and the `sh` sampling on the same eighth would catch it
  mid-slew. The same goes for any `switch` carrying gates or stepped CV.
- **Fixed phrases are fine if they're pentatonic.** A minor-pentatonic hook
  has no avoid notes over an i–VI–III–VII cadence ("big, dumb, correct" —
  `ion.patch`). If the phrase isn't pentatonic, use the chord-tracking
  switch contour instead (`halcyon.patch` explains the clash math).
- **An accent lane is not a fader.** The kit subs take `accent` (per-hit
  dynamics) and `trig`; neither sets how loud the channel is. Driving only
  `accent` from an arrangement lane makes a drum play at full mixer level
  the instant it is gated on, so "the kick arrives" and "everything is here"
  measure the same in `--stats` -- verified: two sections designed 15 dB
  apart came back 0.4 dB apart. Put the lane on `mix.lvlN` as well, with the
  config `levels` entry as the floor. (A tight glue comp hides the mistake
  further; loosen the master before concluding the arrangement is wrong.)
- **Euclid density is an automation target.** `lane -> euclid.hits : 7`
  thickens hats/rolls/arps into a drop; `lane -> func.fall : 0.03` opens
  hat decays; a glacial LFO into `euclid.rot` rotates a pattern over
  minutes. Texture changes without a single new lane.
- **Offbeat = `eighth AND NOT quarter`**, both from the same div chain —
  phase-locked to the kick, no stepseq needed.
- **The dub doctrine**: the dry stab is quiet; the tempo-synced echoes
  (`tempo` module, dotted 8th + 3/8th) and the long plate are the
  instrument. Send echoes into the reverb and duck the returns with the
  kick.

## Infinite scenes

For a no-arrangement groove: skip the tracker grid entirely. Variation
comes from three or four **incommensurate glacial LFOs** (periods like
53/88/141/303 s) on filter cutoffs, FM operator levels (`lvl3`/`lvl5` are
inputs — timbre is modulatable), echo feedback/tone, and reverb decay, plus
a low-probability `turing` for harmony drift. Do the render's fade-in/out
**in post** (`ffmpeg -af "afade=t=in:st=0:d=4,afade=t=out:st=291:d=9"`),
not in the patch — verify with `ffmpeg astats` on the first/last seconds.

## Score-length patches: the form lane and the switch trap

Learned building `patches/symphony/` (four movements, fifteen mono parts):

- **The form is a `stepseq` whose `lens` are bars.** Clock it by the bar
  divider; its `trig` fires once at every section start. Clock every role
  and dynamics lane from that trig (one value per section, no block
  arithmetic) and reset every theme from it, so a section always begins on
  its theme's first note. Section boundaries then land exactly where the
  bar count says; `--stats` verifies them at a glance.
- **A theme carries its own harmony.** Melody and chord lanes in one `.sub`,
  sharing `clock`/`reset`/`transpose`, mean "the horn plays the second
  subject in Bb" is one transposition cable, not four.
- **`switch` defaults to `mode: seq` and ignores `addr`.** A selector fed
  by a lane without `mode: addr` sits on `in1` for the whole render -- and
  since `in1` is conventionally silence, the symptom is an almost silent
  render, not an error. (`check` cannot catch it: the patch is valid.) A
  selector routing lanes or gates also wants `fade: 0`, the instant cut.
- **`quant` ties snap down**, so a minor-to-major "re-reading" through a
  major quantizer turns F into E, not F#. Nudge the input up a quarter-tone
  (`0.04167 -> q.in`) and every off-scale note snaps up while in-scale
  notes stay put.
- **Orchestral low end runs away.** Basses doubling cellos an octave down,
  plus timpani, put half the mix's energy below 80 Hz before anyone
  notices; keep the bass channel around a third of the cello's level and
  shelve the master below 90 Hz. Check band-energy fractions (an FFT per 8 s window),
  not just the RMS timeline.

## Writing a song with tracker

For anything with a form, write the score as `.trk` files and keep the patch
about sound (`docs/modules.md` `tracker`; worked port:
`patches/mainstage-trk.patch`, which nulls against `mainstage.patch` to
about −80 dB per block). Three songs in `patches/tracker/` show the composing
features: `whole-step` (chord tones from one harmony track, `chorus^2` for the
key change, sections), `dorian-sidestep` (lanes with ghost notes, overlay
fills, `^N` and `@R` motif variation, swing from `transport`) and `lantern`
(extended chords, `>` ramps for the form, a counter-line in chord degrees
through `quant`):

- **Two trackers, two clocks.** One for notes at the finest grid the song
  needs (`clock { bpm: 4 × tempo }` = sixteenths), one for the form clocked
  by a four-bar divider (`div { by: 64 }` off the same clock). The form file
  is a table: one row per block, one `cva` column per section level, read
  down like a mix sheet. Slew each column through `func` exactly as the old
  arrangement lanes did; the tracker's values are stepped and held. When
  every order step of the notes is one block, `song.step_trig -> form.clock`
  ties the form to the order list instead of a divider: one form row per
  step, whatever each step's length.
- **Swing is the clock's.** The tracker has no groove; drive it from
  `transport { bpm: 128, division: 4, swing: s }` and `t.clock -> song.clock`.
  Drum-machine swing P% (the first sixteenth of a pair takes P% of it: 50 %
  straight, 66 % triplet) is `s = 2P − 1`: 54 % → .08, 58 % → .16,
  66 % → .32, 75 % → .5 (the cap .75 is 87.5 %).
- **Layer, don't copy.** `order chords+chop+hook+beat` stacks a four-bar
  chord pattern with one-bar and two-bar loops: the step lasts as long as
  the first pattern and the rest wrap inside it. Put the longest pattern
  first. A one-shot (a drop hit, a fill) is an overlay, `hit@0` or
  `fill@48`: it plays once and overrides the loop under it.
- **The gate is the trigger.** Each note or `x` re-articulates with a
  one-sample gap, so `gateN` drives `func.trig`, `adsr.gate`, `lpg.strike`
  and `vco.sync` directly; no `stepseq.trig`, no `logic { op: and }` with a
  lane. Write `off` where the old gate fell if you care about gate length.
- **Chords are one track.** `track harm note cva cvb` carries root, third
  and fifth as note names; every voice reads `voct5`/`cva5`/`cvb5`.
- **Write a riff once.** `track arp note vel from harm` and cells like
  `t1+12`, `~t3`, `t2-12:2` follow harm's chord on every row, so one bar of
  arp or bass loops under the whole progression; `verse^5` or a section's
  `^N` moves a whole key change. `len 1` on a drum or pluck track and
  `lane hat x.x. 9x5x` write rhythm without an `off` row per hit; a trailing
  `>` ramps a level across a build. `pattern verse 4x16` lets labels read
  `3:06`. The full list is `docs/modules.md` `tracker`, "Composing".

- **No wrap hazard.** Without `|:` the song ends after its last row (gates
  fall, `end` goes high), so the guard-step arithmetic of the arrangement
  grid section is unnecessary. Add `|:` only for an endless loop.
- **Edit while it plays.** `play` watches the `.trk` files; a save keeps the
  current order step and row. A typo reports `song.trk:line:col` and the last
  good version keeps playing.

## Small gotchas

- `quant`'s pitch output is `out`, not `cv` (stepseq has `cv`; quant does
  not).
- `mix` `lvlN` CVs are **additive** to the config `levels` — both a lane
  fade and a kick-duck on the same channel just sum.
- `func` retriggers on a rising edge and is a one-shot; `adsr` holds
  sustain while the gate is high. Envelopes for sustained pads are `adsr`;
  percussive/pitched-riser one-shots are `func`.
- `delay` returns on their own mixer channels use `mix: 1` (wet-only);
  inserts use `mix < 1`.
- A `stepseq` with only `gates:` works for rhythm (its `cv` is ignored);
  `trig` fires per clock step, `gate` respects the gates array — use `gate`
  when rests should not retrigger envelopes.
