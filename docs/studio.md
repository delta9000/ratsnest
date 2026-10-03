# Live patch studio and agent collaboration

Build and open a patch:

```sh
mise run studio -- patches/keepsake.patch
```

This builds `build-studio/ratsnest-studio` with SDL3, OpenGL, Dear ImGui and
imgui-node-editor. SDL3 selects the platform backend, including native Wayland.
Dependencies are pinned to commits in `app/CMakeLists.txt`; the first build
needs network access. The ordinary DSP build does not require these libraries.

The graph shows the compiled signal path, including expanded subpatches. Select
a node to expose all its input pins, edit input bases in the inspector, and read
live outputs. Drag output pins to inputs to add cables. Delete a selected cable
or authored node with Delete. Expanded internals must be edited in their parent
subpatch source; a rejected edit leaves the running graph intact. Add module
opens a type selector and a field for DSL parameters. Scene modules require a
`bars` array, e.g. `bars: [4,8,4]`.

The left list searches names and types. Pan/zoom the graph, use Fit graph for an
overview, or Arrange to restore the automatic columns. Node positions are
session-local. The Source tab edits the patch text, including configs and
subpatch instances. Input-base edits preserve comments and unrelated source.
Undo/Redo apply to live source edits, including an agent's edits. Cable depths
and expanded subpatch internals are currently edited through source.

Live source and disk are separate: Apply makes a valid edit audible; Save
persists it. Invalid source leaves the last good graph playing. A changed file
reloads automatically when there are no conflicting unsaved edits. Save refuses
to overwrite a file changed externally. To reconcile, retain the live source,
merge the external edit, then reopen the resulting patch. The editor asks about
unsaved changes on normal window close; forced process termination cannot do so.

The toolbar controls the first transport/clock and first scene module. Scene
buttons queue a zero-based request (button 1 selects index 0) for the next bar;
Hold keeps the current section, and Cancel queued removes a pending transition.
Re-selecting the same request does not restart it; use its `launch` input for
that. Peak meters show float headroom before device clipping. Outputs are sampled
at roughly 30 Hz, so one-sample pulses are not reliably visible. Audio mode and
PipeWire connection state are displayed; a session uses a requested 48 kHz rate.
The CLI player remains available for its automatic device-reconnection behavior.

## Connect an agent

Click **Copy MCP config** for the executable and socket of the current session.
Paste that configuration into a client supporting local stdio MCP servers. The
shape is:

```json
{
  "mcpServers": {
    "ratsnest": {
      "command": "/absolute/path/to/build-studio/ratsnest-studio",
      "args": ["--mcp", "/path/printed/by/the/running/session.sock"]
    }
  }
}
```

The bridge implements the 2025-11-25 MCP initialization and stdio framing, tools,
and the `ratsnest://modules` documentation resource. It connects to the **same
running session** as the UI; it does not start another audio engine. The socket
is local, owned by the current user, and mode 0600. No network listener is opened.

Tools:

- `state`: source, revision, expanded graph, input bases, observed outputs,
  master peaks, audio status and unsaved state.
- `describe`: runtime module interfaces, config choices and parameter-glide policy.
- `set_parameter`: `{revision, module, parameter, value}`; value is a DSL string,
  such as `"-12"`, `"sine"`, or `"[0,3,7]"`. Targets authored declarations.
- `replace_patch`: `{revision, source}`; validates a complete edit before applying.
- `save`: `{revision}`; persists the current live source to its existing file.

Every mutation requires the current revision. A stale revision fails; the agent
must read state again and reconcile instead of overwriting newer work. Concurrent
calls are serialized on the UI/control thread. Audio processing continues on the
PipeWire thread; telemetry uses a bounded, lossy queue and never waits for a reader.
A topology change can briefly reject another topology change during its 50 ms
crossfade; retry after inspecting state. Observations carry a graph generation so
outputs from the previous topology cannot be mislabelled as the new one. At most
4096 output values are observed; `telemetry_truncated` reports larger graphs.

Useful agent instructions: read the module reference and state; identify the
musical intent; make one coherent edit; inspect its actual outputs; save only
when the user wants the result persisted. The bridge provides tools for an
external agent; it does not embed a model or API credentials.

## Verification and silent previews

```sh
cmake --build build-studio
ctest --test-dir build-studio --output-on-failure
./build-studio/ratsnest-studio patches/keepsake.patch --silent
./build-studio/ratsnest-studio patches/keepsake.patch --headless --silent --socket /tmp/ratsnest-demo.sock
```

`--silent` advances the engine for inspection without opening audio, MIDI, or
capture devices. Its speed follows the UI/headless loop, so use the offline
`ratsnest render` command for precise duration renders. `--frames N` bounds a
smoke test. `--screenshot FILE.ppm` captures the last frame of a bounded GUI run;
SDL's `offscreen` driver can be used for automated rendering checks.
