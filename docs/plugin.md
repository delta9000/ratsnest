# ratsnest as a plugin (CLAP)

`ratsnest.clap` puts your patches inside a DAW. Every `.patch` file in the
plugin folder shows up in the host's plugin list as its own plugin, named
after the file. The patch file stays the source of truth: the plugin saves no
state of its own, and saving the file hot-reloads it inside the host, exactly
like `ratsnest play`.

## Install

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

## What each plugin is

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

## Hot-reload and errors

The plugin asks the host for a 200 ms timer and polls the patch and every file
it pulled in, as `play` does. Param edits glide; topology edits crossfade over
50 ms. A save that fails to parse or compile logs `file:line:col: error: ...`
through the host's log and keeps the last good graph playing (rule 6). A patch
that is broken when the project opens still loads, silent, and starts playing
on the first good save. Hosts without timer support load the patch once and
log that hot-reload is off.

## Not yet

- **Parameters.** No host-automatable knobs; the patch has no "exposed
  param" concept yet. Automate by sending MIDI CC into a `midi` module.
- **Transport sync.** The host's tempo and play position are not signals in
  the patch; clocks free-run.
- **VST3 / LV2.** CLAP only. On Linux, Bitwig Studio, REAPER and Qtractor
  load CLAP plugins. For a VST3-only host, the MIT-licensed
  [clap-wrapper](https://github.com/free-audio/clap-wrapper) can wrap it as a
  separate build step (not set up here).

## Why CLAP, and why this direction

Exporting patches into hosts gives a DAW user every ratsnest voice with one
small file and no new syntax. Hosting third-party plugins inside ratsnest
would need plugin GUIs, opaque state blobs that break "the patch is the source
of truth", and a thread model per host; it is the bigger, riskier half.

CLAP is a plain C ABI in MIT-licensed headers, so the plugin is one source
file with no SDK to vendor. VST3's SDK is a large C++ framework; VST2 is
unlicensable. Build with `-DRATSNEST_CLAP=OFF` to skip the plugin and the
header download.

## Testing

`clap_host_test` (ctest `clap_plugin`) is a minimal host: it dlopens the built
`ratsnest.clap`, scans a scratch folder, plays host notes and raw MIDI and
checks they land on their frame, feeds audio through an `extin` patch,
rewrites a patch to hot-reload it on the host timer, and checks a bad save
reports `line:col` while the last good graph keeps playing.
