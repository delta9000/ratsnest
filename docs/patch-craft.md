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
  makeup +1.5..3 dB, clipper drive 1..6.5 dB, in dB (parallel mix ~0.3), limiter
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
- Filter cutoff is `base + env + LFO + ...` — the config value is the
  *floor* of the sweep, not the sweep amount. Set the base to the darkest
  sound you want and scale the env cables up from there.

## Note names need a **220 Hz** oscillator base

`A3v = 0.0` means "absolute pitch = 220 × 2^value", but a `vco` computes
`freq_base × 2^v_oct`. So a note name lands at its written pitch **only** on
a 220 Hz base. Every other A base is in tune but in the wrong octave:

| `freq:` base | `D2v -> osc.v_oct` gives | Error |
|---|---|---|
| `220hz` | 73.4 Hz (D2) | correct |
| `110hz` | 36.7 Hz (D1) | one octave down |
| `55hz` | 18.4 Hz | two octaves down — inaudible |
| `440hz` | 146.8 Hz (D3) | one octave up |
| `659hz` | 220 Hz | detuned *and* transposed |

The 55 Hz case is the dangerous one: it is the natural base to reach for on a
sub voice, and it silently produces sub-audio. **`--stats` cannot see this
bug** — all three bases above render byte-for-byte identical peak and RMS
(measured: −6.0 dBFS peak, −9.0 dBFS RMS for every one). Your only defence is
the arithmetic. If a bass is inaudible on a render that looks healthy, check
this before anything else.

Detune via the freq base (219/221 Hz) or
via `v_oct` constants (±0.007 ≈ ±12 cents); both keep note names intact.
Offsets are bare fractions: `0.25 -> osc.v_oct` is up a minor third from
whatever the pitch input carries — there is no note-name form for
intervals.

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
