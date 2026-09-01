# Patch craft: hard-won authoring lessons

Agent-facing lessons derived by building and verifying arranged tracks
(`mainstage`, `afterglow`, `fathom`). These are the things that cost a probe
render, a failed check, or a quiet master to learn — not the things
`docs/modules.md` already documents. Read `docs/modules.md` first; read this
before writing a song-length or mastered patch.

## Verify without ears: the `--stats` workflow

You cannot listen. `ratsnest render patch -d Ns -o out.wav --stats 4s` is
your ears — a per-window peak/RMS timeline that validates almost everything:

- **Arrangement timing**: each section boundary should show an RMS step at
  the expected second (drop slams +4 dB, breakdown dips −7 dB, fade reaches
  −240 by the end). If a boundary lands at the wrong time, the grid or a div
  is off.
- **Loudness targets**: mastered EDM drop windows ≈ −12 dBFS RMS with
  −0.4 dBFS peaks (crest ~11 dB = loud, not bricked). Breakdown should sit
  6–8 dB below the drops. Intro ≈ −25 dB RMS or lower.
- **Hygiene**: overall peak ≤ the limiter threshold, `dc` ≈ 0, no flat
  peak==RMS windows (that's a brick), no dead air after the fade.

Iterate on numbers, not vibes: change a level, re-render, re-read the
timeline. Two or three iterations converges.

`--start Ns` captures a later window (`--start 10m -d 60s`) and reports
absolute offsets, but it does **not** skip the work: the graph is stateful,
so it still runs from zero. For a song-length patch the cheap probe is to
divide the arrangement grid instead — drop the divisor that clocks the
tracker lanes by 10-15x and the whole form plays in a minute. That verifies
section boundaries, lane crossfades and grid wrap; it does *not* verify
anything with a real time constant (reverb tails, multi-second `func` slews,
tempo ramps, granular freeze windows), which still needs a full pass.

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

## Mastering: the calibration numbers

The proven chain is `dcblock → eq → compressor(external kick sidechain) →
stereo width → clipper(soft) → limiter` (see `halcyon`/`ion`/`mainstage`).
What the docs don't tell you:

- **A post-limiter attenuation scales the ceiling.** The idiom
  `-1 -> out.level; main_out -> out.level : 0.8` fades the whole track, but
  at full play the 0.8 turns a −0.4 dBFS limiter ceiling into a −2.3 dBFS
  master — your render comes back ~2 dB quiet and no amount of clipper
  drive fixes it without crushing. Use `out.level : 1.0` and let the limiter
  own the ceiling.
- **Working backwards from loudness**: premaster peak ≈ −3 dBFS, glue comp
  makeup +1.5..3 dB, clipper drive 4..6.5 dB (parallel mix ~0.3), limiter
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
- **`math`'s `b` input defaults to 1, not 0.** `math { op: add }` with
  `contour -> m.b` adds `1 + contour` -- the whole line comes out an octave
  high, in tune, and sounds like a voicing choice rather than a bug. Write
  `math { op: add, b: 0 }` and let the cable own the input. (By the same
  arithmetic, the `1 -> m.b` octave-lift idiom in `mainstage` lands two
  octaves up, not one.)
- Filter cutoff is `base + env + LFO + ...` — the config value is the
  *floor* of the sweep, not the sweep amount. Set the base to the darkest
  sound you want and scale the env cables up from there.

## Note names need a 220 Hz (or 110/440) oscillator base

`A3v = 0.0` means "absolute pitch = 220 × 2^value". Note-name literals only
land at written pitch on oscillators whose `freq` base is an A (220, 110,
440, 55). If you copy a patch that sets `freq: 659hz` and feed it note
names, everything is transposed. Detune via the freq base (219/221 Hz) or
via `v_oct` constants (±0.007 ≈ ±12 cents); both keep note names intact.
Offsets are bare fractions: `0.25 -> osc.v_oct` is up a minor third from
whatever the pitch input carries — there is no note-name form for
intervals.

**The note letter must be uppercase, and a lowercase one fails silently.**
`D2v` is a note; `d2v` is not, and it does not raise — it reads as `0.0`,
so every voice sits at its oscillator's base frequency and the patch renders
plausible-sounding nonsense. (Probed: a `stepseq { steps: [d2v, ...] }` puts
out flat zero, the same patch with `D2v` puts out −1.58.) This is why
identifiers must stay lowercase — the two namespaces are told apart by case
alone.

## Free-ringing voices ghost; ADSR voices don't

`string`, `lpg`, and `modal` ring after their trigger. If an arrangement
lane fades one *in* while it happens to be ringing from a strike during its
silent section, a ghost note swells in from nowhere. Gate the strike itself
with the **stepped** lane: `logic { op: and }` of `mel.gate` and
`arr_voice.cv`. `func`/`adsr`-driven VCAs start from zero every note and
never have this problem.

The stereo version of the same bug: a voice muted by a mixer lane still
bleeds into reverb/delay if the send is tapped pre-mute. Gate the voice
*before* its sends (a post-fader VCA, e.g. `stab_out`), then tap the sends
from there. (`meridian.patch` demonstrates this; it's why its stabs vanish
cleanly instead of leaving reverb ghosts.)

## Composition idioms that read as "composed"

Learned by diffing the good patches (`meridian`/`halcyon`/`ion`) against
the flat ones:

- **Chord lanes re-voice everything.** Three bar-clocked stepseqs — root,
  third, fifth as absolute v/oct (note names) — feed bass, pad, arp, and
  lead alike. The whole track re-harmonizes from three numbers.
- **`switch { mode: seq }` is the melody generator.** An eighth-clocked
  switch stepping a fixed 8-step contour through the (octave-lifted) chord
  lanes makes an arp/riff that is consonant over every chord. `math { op:
  add }` with `b: 1` is the octave lift. An `sh` after the switch holds
  pitch through each note's decay so ringing notes never retune mid-tail.
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
