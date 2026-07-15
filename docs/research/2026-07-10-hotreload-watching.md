# Engineering Brief: Hot-Reload File Watching & Live Graph Replacement

## A. inotify correctness

**Why watching the file breaks.** vim's default `backupcopy=auto` writes new content to a temp file, deletes the original, and renames the temp over the original path — the watched inode is destroyed; kernel emits `IN_ATTRIB`/`IN_DELETE_SELF` then `IN_IGNORED` and the watch dies silently (vim docs warn `backupcopy=yes` is needed for external watchers). Emacs renames the visited file aside before writing a new one — inotify is inode-based, not path-based, so the watch is orphaned. VS Code's atomic save does the same write-temp-then-rename dance (VS Code #15111, fsnotify #17).

**Robust pattern.** Never watch the file inode directly. Watch the *parent directory* for `IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVE_SELF | IN_DELETE_SELF` and compare each event's `name` against the target basename. In-place writers fire `IN_CLOSE_WRITE` on the basename; rename-based writers fire `IN_MOVED_TO`. Re-arm if the directory itself is renamed/deleted (`IN_MOVE_SELF`/`IN_DELETE_SELF` on the dir watch → re-resolve and re-watch).

**Read-after-event races.** `IN_CREATE` fires the instant the inode appears, before bytes are written — reading then risks an empty/half-written file. `IN_CLOSE_WRITE` and `IN_MOVED_TO` are the only events guaranteeing the writer is done; they are the sole reparse triggers.

**Debounce.** Editors emit multiple directory events per logical save. Standard fix: quiescence window — collect events, reset a timer on each new one, fire when the timer expires. watchexec's default is **50 ms**, explicitly to collapse editor event bursts and avoid running mid-write. Use 50 ms.

**API mechanics.** `inotify_init1(IN_NONBLOCK | IN_CLOEXEC)`; `inotify_add_watch(fd, dir, mask)`. Events read as packed variable-length `struct inotify_event { wd; mask; cookie; len; char name[]; }`; buffer must be ≥ `sizeof(struct inotify_event) + NAME_MAX + 1` (man 7 inotify; too-small buffer → EINVAL on modern kernels). Integrate the fd into a `poll()` loop on a non-RT thread (composes with timerfd/eventfd). Never on the audio thread.

**Alternatives.** fanotify: overkill for one file. mtime polling: only needed for NFS (inotify misses server-side changes) — a code comment, not a design branch.

### Pseudocode

```
wd_dir = inotify_add_watch(fd, dirname(patch_path),
             IN_CLOSE_WRITE|IN_MOVED_TO|IN_MOVE_SELF|IN_DELETE_SELF);
deadline = never;
loop poll(fd, timeout = deadline - now):
    if readable(fd):
        for ev in read_events(fd):           // buffer >= sizeof(inotify_event)+NAME_MAX+1
            if ev.wd == wd_dir and ev.name == basename(patch_path)
               and (ev.mask & (IN_CLOSE_WRITE|IN_MOVED_TO)):
                deadline = now + 50ms;        // debounce: restart quiescence timer
            if ev.mask & (IN_MOVE_SELF|IN_DELETE_SELF):
                rewatch_directory();
    if now >= deadline and deadline != never:
        deadline = never;
        enqueue_reload();
```

## B. Live graph replacement precedent

**VCV Rack (v2):** engine runs on the audio thread; module add/remove uses a shared mutex (process share-locks, structural changes exclusive-lock). Mutex *inside* process() is documented as a source of audible clicks in real modules — reinforces the no-lock rule; our lock-free pointer swap is the RT-safe equivalent of their brief exclusive rendezvous.

**SuperCollider scsynth:** `/d_recv` replaces a SynthDef by name but running nodes keep their instantiated graph — no live swap, no crossfade; click-free transitions are the user's job (new synth + envelope + free old). Validates the new-graph/old-graph/fade/retire model over in-place mutation.

**State migration precedent:** targeted search (Faust, gen~, Cardinal, Bespoke, SunVox, SuperDirt, shader hot-reload) found **no direct precedent** for automatically migrating per-module DSP state (oscillator phase, delay buffers) across a live graph recompile keyed by name+type. Closest analog: Erlang/OTP hot code swap's explicit `code_change(OldVsn, State, Extra)` per-process state adapter. ratsnest's (name,type) migration is genuinely novel for modular audio.

**Crossfade discipline:** equal-power via `gain_old = cos(θ)`, `gain_new = sin(θ)`, `θ = t·π/2` over the window (gain_old² + gain_new² = 1 throughout; avoids the perceptual dip of linear). Web Audio prior art for "run both paths briefly and crossfade": ConvolverNode buffer swap glitch workaround instantiates a second node and crossfades. 2× DSP for 50 ms is trivial.

**Recommended sequencing (researcher's version — NOTE: controller amended step 1, see below):**
1. Main thread parses+compiles new graph; on success, migrates state by (name,type) and sets glide targets.
2. Main thread publishes a single immutable `FadeState { old_graph*, new_graph*, total_fade_samples, samples_done=0 }` via one pointer swap — audio thread never observes old and new independently.
3. Audio thread renders BOTH graphs per block, mixes `old*cos + new*sin`, advances samples_done; params glide linearly over their own 20 ms window.
4. At fade end, audio thread adopts new_graph alone, pushes retired old_graph* onto the SPSC free-queue; main thread drains and destructs off-RT.
5. Parse/compile failure: nothing published; last good graph keeps playing.

**CONTROLLER AMENDMENT (data race):** step 1 as written has the main thread reading live module state (phase etc.) while the RT thread concurrently writes it — a data race/UB. Correct design: main thread precomputes a migration pair list (old node index → new node index) with no string work needed on RT; the RT thread performs the actual state copy at swap-adoption time via an alloc-free, noexcept `Module` state-copy hook, before starting the crossfade. Bounded memcpy of small state is RT-safe.

**Param glide:** fixed-duration **linear ramp** (per-sample increment) over ~20 ms, not one-pole exponential — this is a single bounded scheduled transition that must provably arrive at the target by t=20ms; exponential only asymptotes (it's the right tool for continuous knob automation instead). (EarLevel, musicdsp refs.)
