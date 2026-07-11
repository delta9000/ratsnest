# State-Variable Filter Design

Implement `svf` as a topology-preserving transform state-variable filter with
audio-rate modulation and simultaneous low-pass, band-pass, and high-pass
outputs.

Inputs are `in`, `cutoff` (default 1000 Hz), and `res` (default 0). Outputs are
`lp`, `bp`, and `hp`, with `lp` first so it is the default output. Cutoff is
clamped from 5 Hz to 45% of sample rate. Resonance is clamped to `0..1` and maps
from critically damped response toward a small positive damping coefficient at
the top of the range, allowing a strong resonant peak without unstable infinite
self-oscillation.

The implementation uses the zero-delay TPT SVF equations and two equivalent
integrator states. Coefficients are recalculated per sample so cutoff and
resonance modulation remain audio-rate. Processing performs no allocation,
locking, or I/O. Both integrator states migrate during hot reload.

Tests measure low-pass attenuation above cutoff, DC settling, the shared-output
state identity, finiteness under extreme modulation, port metadata, and state
migration.

In the active patch, use separate filters for the bass, arpeggio, and supersaw
lead. Drive their cutoff inputs from existing envelopes and the long arrangement
arc so the build gains spectral energy before the drop.
