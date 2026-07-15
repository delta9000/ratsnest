# Dark Ambient Patch Design

Create a self-playing, deadmau5-inspired dark minor drone using only the Phase
1 module set. Build a D-minor chord from low sine and triangle voices, then add
a restrained detuned saw halo for width and sheen. Use unrelated slow LFOs for
subtle pitch drift and long amplitude swells. Keep individual saw gains very
low so the smooth chord remains the dominant texture.

Add a 124 BPM rhythmic layer without interrupting the underlying drone. Use a
decaying sine oscillator for a four-on-the-floor kick, a restrained eighth-note
tick for definition, and the beat contour as subtle sidechain-style gain
movement across the chord and saw halo.

Turn the fixed harmony into a four-bar `Dm - Bb - F - C` progression. Build a
separate low, smooth oscillator bank for each chord and crossfade them with
phase-shifted triangle LFOs at one chord per bar. Feed all banks through shared
stereo buses so the existing 124 BPM sidechain movement affects the complete
progression consistently.

Extend the progression to four bars per chord for a 16-bar harmonic cycle.
Separate the chord roots into a rolling bassline that follows the progression
and recovers between quarter-note kicks. Add a 32-bar variation cycle that
lifts the bass an octave and introduces a quiet phase-aligned upper pulse on
alternate passes.

Replace the static upper pulse with a phase-aligned eighth-note arpeggio. Use
additive square-wave pitch clocks to outline `Dm7`, `Bbmaj7`, `Fmaj7`, and
`Cmaj7`, selecting the matching root voice with the progression envelopes.
Lift the melody alongside the bass on alternate 16-bar passes.

Add a soaring lead as four chord-selected supersaw banks, each made from three
narrowly detuned oscillators. Drive them with a slower two-bar seventh-chord
motif and use the long variation clock plus a slow swell to bring the lead
forward primarily during the alternate 16-bar section.

Improve flow through close voice leading: retain common upper tones between
`Dm`, `Bb/D`, `F/C`, and `C` while the bass carries the root progression.
Replace simultaneous square-wave octave and level changes with a smooth 32-bar
arrangement curve. Use it to crossfade focus between the eighth-note arpeggio
and a slower, more lyrical four-bar supersaw motif.

Turn the continuous arrangement into a 32-bar progressive-house performance
arc using a cycling function generator: 16-bar exponential build, peak drop,
and 16-bar release. Scale kick, bass, chord width, and supersaw intensity with
the shared arc. Introduce chord-aware half-note stabs gradually through a
separate triggered function envelope for drop articulation.

Use independent state-variable filters to separate the main spectral roles.
Keep the rolling bass under a low cutoff that opens modestly into the drop,
combine low-pass and band-pass outputs for a resonant pluck, and sweep the
supersaw lead from muted to bright across the full build. Preserve the unfiltered
voice-led chord bed as the stable harmonic foundation.

The patch lives at `patches/dark-ambient.patch`. It must pass `ratsnest check`
before being launched with the live PipeWire player.
