# Shadow Lattice — Design

## Goal

Build track 2 of the Meridian album as a dark, hypnotic, approximately
five-minute arranged techno piece. The composition must make polyphony, Turing
sequencing, and Euclidean rhythm essential to its musical identity rather than
using them as incidental decoration.

The song will live in `patches/shadow-lattice.patch`. Track 1,
`patches/meridian.patch`, remains unchanged.

## Musical Architecture

Shadow Lattice runs about 5:05 at 122 BPM and centers on D minor. A
tracker-style arrangement grid conducts the generative parts through a fixed
album structure:

- 0:00–0:32 — distant four-voice drone and isolated metallic Euclidean hits
- 0:32–1:03 — Turing bass motif emerges
- 1:03–1:34 — kick and interlocking percussion establish the groove
- 1:34–2:06 — first full lattice; polyphonic voices enter in staggered patterns
- 2:06–2:37 — pressure drops while voices sustain and mutate
- 2:37–3:41 — main descent with denser Euclidean overlap and darker harmony
- 3:41–4:13 — breakdown with unstable Turing mutations
- 4:13–4:45 — final and fullest lattice
- 4:45–5:05 — rhythms peel away, leaving the drone

The payoff is rhythmic and textural instead of a bright melodic drop. Four
voices that first appear unrelated gradually lock into one repeating shape.

## Generative Engine

A sequencer-driven four-voice lattice is the central instrument. A shared
Turing register produces pitch material, quantized to a narrow D natural-minor
set. Each voice samples related material from a differently rotated Euclidean
trigger lane, creating harmonic kinship without four parallel melodies.

The voices occupy separate registers and roles:

- A sparse, stable sine/triangle sub voice
- A filtered saw in the low-mid register as the main harmonic shadow
- A narrow-pulse upper voice through an LPG
- A pitched resonant noise-metal voice that blurs rhythm and harmony

Three Euclidean layers drive the form: a grounded 16-step drum cycle, an
asymmetric 13-step accent cycle, and a slowly rotating 11-step voice cycle.
Arrangement lanes automate hit counts and rotations between sections. Turing
mutation remains low during established grooves, rises in breakdowns, and
locks for each major return so recurring sections retain an audible identity.

All random seeds are fixed so offline renders are deterministic.

## Signal Flow and Space

The clock tree begins with 122 BPM sixteenths and derives slower musical and
arrangement divisions. Turing pitch is quantized and sampled only on note
triggers. Every pitched voice is envelope-controlled so free-running
oscillators cannot leak into silent sections.

Kick-derived ducking creates space in the tonal voices and effects returns.
A dark tempo-synchronized delay and short murky reverb add depth without
washing out the rhythmic lattice. Conservative dry and effects gains preserve
headroom when all four voices coincide. Arrangement changes that affect level
use slew where needed to avoid discontinuities.

## Scope and Fallback

The track uses only the existing module palette; it does not expand engine or
DSL scope. The preferred implementation uses a sequencer-driven `voice` group.
If the existing voice-group syntax cannot express the intended trigger
allocation, the patch will use four explicit voices while preserving the same
musical design.

## Verification

Verification consists of:

- Static patch validation
- A deterministic full-duration offline render
- Render statistics checked for silence, clipping, NaNs, and tail truncation
- Short listening renders covering the intro, first lattice, breakdown, and
  final lattice
- The existing automated test suite to catch engine regressions exposed by the
  composition

The patch header documents its album position, arrangement timeline,
generative routing, and expected duration.
