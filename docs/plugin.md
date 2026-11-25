# ratsnest and CLAP plugins

Both directions, CLAP only:

- [Loading plugins](#loading-plugins): the `plugin` module runs a
  third-party CLAP plugin (Surge XT, say) inside a patch, headless.
- [ratsnest as a plugin](#ratsnest-as-a-plugin): `ratsnest.clap` runs your
  patches inside a DAW.

## Loading plugins

```text
clk = clock { bpm: 480 }
seq = stepseq { steps: [0, .25, .58333], gates: [1, 0, 1] }
clk -> seq.clock

# notes from the sequencer, and from a keyboard under `play`
synth = plugin { file: "Surge XT", midi: on }
seq.gate -> synth.gate
seq.cv -> synth.voct

# an effect with one param driven; the name as `ratsnest plugins` prints it
fx = plugin { file: "Some Filter", params: ["Cutoff"], p1: 0.4 }
wob = lfo { freq: 0.1hz }
wob -> fx.p1 : 0.2
synth.out_l -> fx.in_l
synth.out_r -> fx.in_r
fx.out_l -> out.left
fx.out_r -> out.right
```

Port and config reference: [`plugin` in docs/modules.md](modules.md#plugin).
A worked patch: `patches/plugins/surge.patch`.

**Finding plugins.** `ratsnest plugins` lists every `.clap` on the search
path (`$CLAP_PATH`, `~/.clap`, `/usr/lib/clap`, `/usr/local/lib/clap`) with
the plugin ids inside. `ratsnest plugins "Surge XT"` adds each plugin's
params: id, quoted name, range, default, and that default as a `p` base, so
you can copy a name into `params:` and its base into `p1:`.

**The patch stays the source of truth.** There are no plugin GUIs and no
saved plugin state. Everything the plugin does comes from the patch: the
params it lists (each driven by a `p` input, so cables modulate them like any
other base), notes from `gate`/`voct`/`vel`, and, with `midi: on`, the
keyboard. Unlisted params sit at the plugin's own defaults, so a synth plays
its init sound until the patch says otherwise. A preset or plugin state file
is a possible next step.

**Timing.** Plugins process blocks; the graph runs per sample. The module
collects 64 samples, runs the plugin once, and plays the result during the
next 64: audio, notes and params all come out 64 samples late (1.3 ms at
48 kHz), consistently, so a plugin in a feedback loop adds that delay to the
loop. Notes land on their sample inside the block. Params are sent when they
change, at the end of a block.

**Reloads.** Param edits glide as usual. A topology edit builds a new graph,
so the plugin starts fresh (and reloads, which for a big synth can take a
moment on the control thread; the old graph keeps playing meanwhile).

**What can go wrong.**

- A plugin that is missing, fails to load, or has no param by a listed name
  prints a warning and runs silent. The patch still checks and plays, so a
  patch that names a plugin you have not installed is not an error.
- A plugin runs on ratsnest's audio thread, in-process. If its process()
  allocates or locks, that bends rule 3; if it crashes, it takes the session
  with it. ratsnest cannot prevent either without an out-of-process host.
- Libraries stay loaded until ratsnest exits; many plugins dislike unloading.

## ratsnest as a plugin

`ratsnest.clap` puts your patches inside a DAW. Every `.patch` file in the
plugin folder shows up in the host's plugin list as its own plugin, named
after the file. The patch file stays the source of truth: the plugin saves no
state of its own, and saving the file hot-reloads it inside the host, exactly
like `ratsnest play`.

### Install

```sh
mise run plugin-install                  # builds, copies ratsnest.clap to ~/.clap/
mkdir -p ~/.config/ratsnest/plugins
ln -s "$PWD/patches/keys-poly.patch" ~/.config/ratsnest/plugins/
```

Then rescan plugins in the host. `keys-poly` appears under vendor
`ratsnest`. Symlinks are followed to the real file, so a patch's relative
`.sub`, `.trk` and `.wav` paths resolve against the checkout, and editing the
file in the repo reloads it in the DAW.

The folder is `$RATSNEST_PLUGIN_DIR` if set, else
`$XDG_CONFIG_HOME/ratsnest/plugins`, else `~/.config/ratsnest/plugins`. Only
`*.patch` files at its top level are listed. Adding or renaming a patch needs
a plugin rescan; editing one does not.

### What each plugin is

| Patch | Plugin type | Ports |
|---|---|---|
| reads no `extin` | instrument, synthesizer | note in, stereo out |
| uses `extin` | audio effect | note in, stereo in, stereo out |

- **Notes and MIDI.** Host notes and raw MIDI (CC, pitch bend, aftertouch,
  sustain) reach every `midi` module, sample-accurately: an event at frame 100
  of a block changes the gate at frame 100. Velocity maps to MIDI velocity
  1..127. A wildcard note-off from the host is All Notes Off. Patches written
  for a keyboard under `ratsnest play` work unchanged.
- **Audio in.** The stereo input is mixed to mono and fed to every `extin`,
  in the same block (no added latency). Blocks larger than 4096 frames
  overrun `extin`'s buffer and drop the oldest input.
- **Audio out.** `out.l` / `out.r`, unclipped float, as `render` writes it.
  The DAW mixes in float; keep a limiter in the patch if it can run hot.
- **Sample rate.** The patch compiles at the host's rate on activation.

### Hot-reload and errors

The plugin asks the host for a 200 ms timer and polls the patch and every file
it pulled in, as `play` does. Param edits glide; topology edits crossfade over
50 ms. A save that fails to parse or compile logs `file:line:col: error: ...`
through the host's log and keeps the last good graph playing (rule 6). A patch
that is broken when the project opens still loads, silent, and starts playing
on the first good save. Hosts without timer support load the patch once and
log that hot-reload is off.

### Not yet

- **Parameters.** No host-automatable knobs; the patch has no "exposed
  param" concept yet. Automate by sending MIDI CC into a `midi` module.
- **Transport sync.** The host's tempo and play position are not signals in
  the patch; clocks free-run.
- **VST3 / LV2.** CLAP only. On Linux, Bitwig Studio, REAPER and Qtractor
  load CLAP plugins. For a VST3-only host, the MIT-licensed
  [clap-wrapper](https://github.com/free-audio/clap-wrapper) can wrap it as a
  separate build step (not set up here).

## Why CLAP

CLAP is a plain C ABI in MIT-licensed headers, so both directions are a few
source files with no SDK to vendor. VST3's SDK is a large C++ framework and
would be a fourth dependency; VST2 is unlicensable. Loading stays headless
on purpose: plugin GUIs bring back GUI work, and opaque plugin state breaks
"the patch is the source of truth". `-DRATSNEST_CLAP=OFF` skips building
`ratsnest.clap`; the headers are always fetched for the `plugin` module.

## Testing

`tests/test_plugin.cpp` loads `tests/plugin/test_tone.cpp`, a tiny CLAP
plugin built beside the tests, through the `plugin` module: audio latency,
param drive by name and id, note timing from a gate, keyboard MIDI with
`midi: on`, and silence (not failure) for a missing plugin or param.

`clap_host_test` (ctest `clap_plugin`) is a minimal host: it dlopens the built
`ratsnest.clap`, scans a scratch folder, plays host notes and raw MIDI and
checks they land on their frame, feeds audio through an `extin` patch,
rewrites a patch to hot-reload it on the host timer, and checks a bad save
reports `line:col` while the last good graph keeps playing.
