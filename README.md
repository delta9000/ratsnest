# ratsnest

A software modular synthesizer for Linux where **the patch is a plain-text
file**. Hand-written C++20 DSP, PipeWire audio out, hot-reload on save.

It's a personal instrument, not a DAW, plugin host, livecoding language, or
marketplace. You write a `.patch` file describing modules and cables, and
ratsnest renders it offline to a WAV or plays it live — reloading the graph
every time you save, so the sound updates as you edit.

```text
osc1 = vco { freq: 110hz, wave: saw }
osc2 = vco { freq: 110.7hz, wave: saw }
vib  = lfo { freq: 0.3hz }
amp  = vca { gain: 0.4 }

vib  -> osc1.v_oct : 0.02
osc1 -> amp.in : 0.5
osc2 -> amp.in : 0.5
amp  -> out.in
```

## Quickstart

### Prerequisites

- Linux with PipeWire (`libpipewire-0.3` dev headers, via `pkg-config`)
- A C++20 compiler
- [mise](https://mise.jdx.dev/) — manages `cmake`/`ninja` versions and wraps
  the build/test/render/play commands below

```sh
git clone git@github.com:delta9000/modsynth.git ratsnest
cd ratsnest
mise run build
```

### Render a patch offline

```sh
mise run render -- patches/first.patch -d 10s -o take.wav
```

Renders 10 seconds of `patches/first.patch` to `take.wav`, no audio device
needed.

Offline renders are stereo float32 WAVs and preserve headroom above 0 dBFS;
they are not silently hard-clipped. Live PipeWire playback is safety-clamped
only at the audio-device boundary. Put an explicit `clipper` or `limiter` in
the patch when peak control is part of the sound.

### Play a patch live

```sh
mise run play -- patches/first.patch
```

Starts a PipeWire output stream and plays the patch. Edit and save the
`.patch` file while it's running — the graph hot-reloads, keeping module
state (filter memory, envelope phase, sequencer position, ...) intact
wherever the module name and type haven't changed.

### Validate a patch without running it

```sh
mise run check -- patches/first.patch
```

Parses and type-checks the patch, reporting `line:col` diagnostics for
anything wrong. A bad patch never kills a running session — `play`/`render`
keep the last good graph going and report errors the same way.

## Writing a patch

A patch is a list of module declarations and cables:

```text
name = module_type { param: value, ... }
source_port -> dest.input_port : gain
```

- Every signal is a float at audio rate — no separate control rate, no
  events. Gates rise through `0.5`; audio/bipolar signals run `-1..1`;
  unipolar signals run `0..1`; pitch is volts/octave.
- Cables *sum* onto a declared input's base value — there's no implicit
  normalling. An unpatched input just reads its default.
- Omitting a cable's output name reads the module's first/primary output
  (a bare `stepseq` reads `.cv`, a bare `svf` reads `.lp`).
- `name = voice { notes: <alloc-or-midi> }` stamps a block of per-voice
  modules for polyphony; see `docs/modules.md` for the full voice-group
  rules.

`docs/modules.md` is the authoritative, agent- and human-facing reference
for every implemented module — inputs, outputs, configs, ranges, and worked
recipes (Euclidean rhythms, sidechain ducking, sympathetic strings, guitar
pedal chains built from existing modules, and more). Read it before writing
or reviewing a patch. The `patches/` directory has dozens of complete
example patches, from a single detuned-saw drone to full multi-track songs.

## Development

```sh
mise tasks           # list all available tasks
mise run test        # build + run the full ctest suite
mise run test-san    # same tests under ASan+UBSan
mise run ci          # warnings-as-errors build + tests + sanitized tests
```

Design principles, the RT-safety rules `Module::process()` must follow, and
commit conventions are in `CLAUDE.md`. The full design rationale is in
`docs/superpowers/specs/2026-07-10-ratsnest-design.md`; module-by-module
implementation plans live in `docs/superpowers/plans/` and `docs/plans/`.

## Attribution

This project synthesizes ideas and, in places, adapted code from other
open-source and academic DSP work. Every borrowed algorithm is recorded in
`docs/attribution.md` with its source and license, and required notices are
preserved in `THIRD_PARTY_NOTICES.md`.

## License

MIT — see `LICENSE`.
