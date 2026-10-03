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
needed. `--start` captures a window from further in:

```sh
mise run render -- patches/longwave/4-lydian.patch --start 10m -d 60s --stats 10s
```

A stateful graph has no seek, so `--start` runs the patch from zero and
discards the skipped samples — it costs the same time as rendering the whole
span, but writes only the window you asked for, and `--stats` still reports
absolute piece time.

### Play a patch live

```sh
mise run play -- patches/first.patch
```

Starts a PipeWire output stream and plays the patch. Edit and save the
`.patch` file while it's running — the graph hot-reloads, keeping module
state (filter memory, envelope phase, sequencer position, ...) intact
wherever the module name and type haven't changed.

### Discover module interfaces

```sh
mise run build
./build/ratsnest describe          # JSON interfaces for all runtime modules
./build/ratsnest describe additive     # one module: input bases, config types, enum values, outputs
```

For agent-driven work: discover the interface, consult `docs/modules.md` for
units/ranges and recipes, run `check`, then render a short representative window
with `--stats`. A successful check establishes syntax/configuration validity;
listening and signal measurements establish whether a patch does what you want.

### Musical patterns and live scenes

`patches/keepsake.patch` demonstrates a shared swung transport, a generated motif
captured into phrase memory, clocked harmony changes, bar-quantized scenes, and
stereo channels with post-fader sends. Render its complete form:

```sh
mise run render -- patches/keepsake.patch -d 40 --stats 9.6 -o keepsake.wav
```

While playing, edit `form.request` to 0, 1, or 2 to queue a section for the next
bar, or set `form.hold: 1` to remain in a section. The module reference describes
`transport`, `scene`, `phrase`, and `channel`, plus repeat/reset controls on
`bern` and clocked scale-degree transposition on `quant`.

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
  events. Gates rise through `0.5`; audio/bipolar signals nominally run `-1..1`;
  unipolar signals run `0..1`; pitch is volts/octave relative to A3/220 Hz by default.
- Cables *sum* onto a built-in input's base value — there's no implicit
  normalling. An unpatched input just reads its default.
- Float output preserves headroom; the live device boundary clips. Use an explicit
  limiter when required.
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
