# Tracker: songs as a plain-text pattern file — Design

2026-10-21. Status: designed. Refines §3 (the patch is the source of truth),
§4 (signal conventions) and §6 (hot reload) of `2026-07-10-ratsnest-design.md`.
Motivation: score-length patches are hand-built from a dozen `stepseq` lanes,
a bar-divider tree and `switch` tricks (`patches/mainstage.patch`,
`patches/longwave/`, `docs/patch-craft.md` "Score-length patches"). The notes
of a song live in horizontal arrays nobody can read down; the arrangement is
spread over parallel lanes whose columns must be counted by hand.

## 1. Shape of the solution

A **`tracker` module** — not a GUI and not new patch syntax:

    song = tracker { file: "song.trk" }
    sixteenth -> song.clock
    song.voct1 -> lead.v_oct
    song.gate1 -> env.gate

The module reads a column-aligned, human-first text file of **tracks**,
**named patterns** of rows, and an **order list** (the arrangement). Every
rising `clock` edge plays one row; rows-per-beat is the clock's business, so
the same file runs at sixteenths, eighths or one row per four-bar block. The
outputs are ordinary audio-rate floats (rule 2): per track `voct`, `gate`,
`vel`, `cva`, `cvb`, plus song-position signals.

The whole file is parsed and **flattened off the audio thread** into one
precomputed array of resolved row states (rule 3); `process()` only counts
clock edges and indexes it.

Rejected alternatives:

- *A `song { ... }` block in the patch grammar.* Patterns are a different
  shape from the module/cable graph (two-dimensional, column-aligned); forcing
  them through the DSL lexer makes both worse. A separate file also keeps the
  patch about sound and the `.trk` about notes, and diffs stay local.
- *A GUI tracker.* The GUI is shelved (CLAUDE.md); any future editor would
  round-trip through this text anyway (rule 1).
- *Clock-follow gates* (stepseq's convention: the gate drops with the clock's
  low half before a re-articulation). Gate length would then depend on the
  clock's duty cycle: a `transport.clock` pulse is one sample wide. The
  tracker instead holds a gate until the note ends and re-articulates with a
  one-sample gap (§4.3), so the clock can be any shape.

## 2. The `.trk` format

Line oriented. Blank lines are ignored. `#` starts a comment when it begins
the line or follows whitespace or `|` (so a stray `C#4` is reported, not
silently cut — see §2.4). Every directive and row is one line.

### 2.1 Tracks

    track <name> <field>...

Declares the next track (track 1, then 2, ... up to 8, in file order) and
the **fields its cells hold, in the order they are written**. Fields:
`note`, `vel`, `cva`, `cvb`, each at most once. `<name>` is a lowercase
identifier (`[a-z][a-z0-9_]*`) used by pattern headers and diagnostics; the
port names stay positional (`voct1`, `gate1`, ...). All tracks are declared before the first
pattern.

    track kick  note           # 1: gate only
    track lead  note vel       # 2
    track harm  note cva cvb   # 3: root, third, fifth as note names
    track mix   cva cvb        # 4: two levels, no notes

Declaring only the fields a track uses keeps cells short: a drum cell is one
token, a level lane is one number.

### 2.2 Patterns and rows

    pattern <name> <rows> [<track>...]

Begins a pattern of `<rows>` rows (1..1024) over the listed tracks, in the
listed order (default: every track in declaration order). Row lines follow
until the next directive:

    [<row>] | <cell> | <cell> ...

- **Cells are separated by `|`**, each preceded by one — the OpenMPT/ModPlug
  pattern-clipboard convention (`|C-501...|E-5...`). A trailing `|` is
  allowed. Fewer cells than tracks leaves the rest empty.
- **`<row>`** is an optional decimal row number. Unlabelled lines take the
  next row (the first is 0). Labels must increase and stay below `<rows>`.
  Rows never written are empty, so a sparse pattern lists only the rows where
  something happens; a dense one is the classic tracker page. Decimal, not
  the hexadecimal of ProTracker-era trackers: musicians count bars in tens.
- **A cell** is whitespace-separated tokens, one per declared field, in the
  track's field order. Trailing fields may be omitted.

Field tokens:

| token | in `note` | in `vel` / `cva` / `cvb` |
|---|---|---|
| `.` / `...` / `-` / `---` (any run of one of them) | empty: nothing changes | empty: the value holds |
| `C4`, `Cs4`, `Bb3`, `A3v` | new note at that pitch (v/oct, A3 = 0) | that pitch as v/oct |
| a number (`0.5`, `-1`, `.25`) | new note at that v/oct | that value |
| `x` | new note at the held pitch (drums) | — |
| `~D4` / `~0.5` | slur: change pitch without re-articulating | — |
| `off` / `===` | note off: the gate falls | — |

Notes use the patch DSL's spelling exactly (`dsl::note_num`): uppercase
letter, optional `s` (sharp) or `b` (flat), octave digit, optional `v`.
Every note in a `.trk` is v/oct (A3 = 0.0 at 220 Hz), so the trailing `v` is
accepted but unnecessary. `vel` must lie in 0..1 (unipolar, spec §4); `cv`
values are any finite number within ±1000000.

**Every value is held until changed**, across rows, patterns and steps: a
note sounds until `off` or the next note (tracker convention); `vel`, `cva`
and `cvb` hold their last written value. Before anything is written a track
holds `voct` 0, `vel` 1, `cva`/`cvb` 0, gate off.

### 2.3 The order list

    order <entry>...

Appends entries to the song's arrangement; `order` lines accumulate, so an
arrangement can be written one section per line with a comment. Entries:

- `name` — play pattern `name` for its length.
- `a+b+c` — play the patterns **together** (layers). The step lasts as long
  as the **first** pattern; each other layer loops within the step (its row
  is `step_row mod its length`). Layers must cover disjoint tracks. Tracks no
  layer covers are empty for the step (their values and notes hold). This is
  how a four-bar chord pattern, a one-bar drum loop and a two-bar hook stack
  without being copied into every section.
- `entry*N` — repeat the entry `N` times (1..4096).
- `|:` — the loop point (a musical repeat sign; at most one, not last). After
  the last step the song returns to the step following `|:`. Without it the
  song plays once and stops. The idea is the restart position trackers keep
  with their order list (provenance: `docs/attribution.md`).

The flattened song is capped at 65536 rows (34 minutes of sixteenths at
128 BPM); a longer order list is an error.

### 2.4 Diagnostics

Every error carries `file:line:col` and the session keeps the last good graph
(rule 6). Representative messages:

- `unknown directive 'patern' (directives: track, pattern, order)`
- `row outside a pattern (start one with 'pattern <name> <rows>')`
- `'C#4' is not a note (Cs4 for sharp, Bb3 for flat)`
- `row has 4 cells; pattern 'beat' has 3 tracks`
- `cell has 3 values; track 'kick' holds 1 (note)`
- `vel must be a number in 0..1`
- `row 16 is outside pattern 'beat' (16 rows: 0..15)` / `row labels must increase`
- `unknown pattern 'drop' in order` (with did-you-mean)
- `layers 'beat' and 'kit' both write track 'kick'`
- `no order list: nothing would play`
- `song is longer than 65536 rows`

### 2.5 Example

```text
# demo.trk -- 4/4 at a sixteenth clock: one row per sixteenth.
track kick  note            # 1
track bass  note vel        # 2
track harm  note cva cvb    # 3: chord root, third, fifth

pattern beat 16 kick        # one bar, looped under longer patterns
00 | x
04 | x
08 | x
12 | x

pattern verse 64 bass harm  # four bars; the step length comes from here
#    bass         harm
00 | D2  .9     | D3  F4  A3
06 | D2  .6     | .
08 | off        | .
16 | Bb1 .9     | Bb2 D4  F3
32 | F2         | F3  A4  C4
48 | C2         | C3  E4  G3
62 | ~D2        | .              # slur back up, no re-attack

pattern fill 64 bass harm
00 | D2  1      | D3  F4  A3
60 | off

order verse*2          # intro: no drums
order |: verse+beat*3  # loop from here
order fill+beat
```

## 3. Ports

    tracker { file: "song.trk" }

Configs: `file` (quoted path, resolved against the directory of the file
that declares the module, like a `sub`), and `text` — the song text itself,
which the loader fills from `file` (§5). Writing `text` by hand is possible
but the DSL has no multi-line strings.

Inputs (all discrete: `glide: false`):

| input | default | meaning |
|---|---|---|
| `clock` | 0 | each rising edge (through 0.5) plays the next row |
| `reset` | 0 | rising edge: rewind; the next clock plays row 0 of step `start` |
| `start` | 0 | order step (0-based, rounded, clamped) that `reset` rewinds to |

Outputs: five banks of eight, named like `alloc`/`midi` banks: `voct1..8`,
`gate1..8`, `vel1..8`, `cva1..8`, `cvb1..8` (track n is `voctn`, `gaten`, ...), then
`step` (0-based order step), `pattern` (index, in file order, of the step's
first pattern), `row` (row within the step), `step_trig` (one-sample pulse
when a step starts), `row_trig` (one-sample pulse on every row), `end`.
A bare `song` reads `voct1`. Tracks the file does not declare output 0.

The bank is fixed at eight tracks because outputs must be declared up front
(like `alloc`/`midi` banks); eight tracks × five columns plus six song
outputs is 46 ports. Port names cannot contain dots (an endpoint splits at
its last dot), hence `voct1`, not `t1.voct`. Patterns needing more voices use a second tracker.

## 4. Timing semantics

### 4.1 Rows and the grid

One row per rising clock edge. Before the first edge the outputs show the
starting row's values with every gate low. The first edge plays row 0 of
step 0: with a `clock`/`div` chain (high at t = 0) **row 0 sounds at t = 0**,
matching patch-craft's arrangement-grid rule (no first-edge skip). A
coincident `reset` and clock edge plays row 0 on that sample (as `stepseq`
and `div` do).

### 4.2 End of song

The clock edge after the last row of the last step either wraps to the step
after `|:` (`end` pulses for one sample, `step_trig` fires) or, without a
loop point, **ends the song**: every gate falls, values hold, `end` stays
high, further clocks do nothing until `reset`. A rendered song therefore
needs no guard steps against wrapping (patch-craft's wrap hazard): it simply
stops. A looped section starts in the state it had on first arrival (the
flattening is linear; the state at the end of the song does not leak into
the loop).

### 4.3 Gates, retriggers, ties

`gate` is high while a note sounds. A new note (or `x`) on a row whose track
was already gated produces a **one-sample low gap** on the clock-edge sample,
so `adsr`, `func`, `lpg` and `vco.sync` see a fresh rising edge — even when
the same note repeats. A slur (`~`) moves pitch with no gap; `off` drops the
gate on its row's edge. `voct` changes on the edge sample. This makes the
gate independent of the clock's duty cycle (a one-sample `transport.clock`
works). Cost: a re-articulated gate rises one sample (21 µs) after the edge;
a note from silence rises on the edge itself.

`vel`, `cva`, `cvb` are stepped and held: slew them with `func` if a lane
should fade (the arrangement-lane idiom of patch-craft).

## 5. Loading, hot reload and state

- **Load.** `build_patch` (graph layer, non-RT) gains a pass after subpatch
  expansion: for each `tracker` declaration with `file:`, resolve the path
  against the declaring file's directory, read it, parse it, and on success
  store the text in the module's `text` config. Parse errors are reported as
  `song.trk:line:col`; an unreadable file at the `file:` parameter. The path
  joins the subpatch files in `PatchBuild::files`, so `play` **watches it like
  a `.sub`**.
- **Init** (non-RT: graph compile) parses `text` and flattens it into the
  module's arrays. A module given only `file` (unit tests that call
  `graph::compile` directly) reads the file itself; any failure leaves an
  empty song that outputs zeros. `process()` allocates nothing.
- **Edit of the `.trk`.** The watcher reloads the patch; the new `text`
  makes the config differ, so the reload takes the topology path and the new
  tracker adopts the old one's position through `copy_state_from`: same order
  step and row within it (clamped to the new song: a vanished step lands on
  the last row of the last step, a shortened pattern on its last row), clock
  and reset edge memory, ended/started flags and the last gate per track, so
  an edit never retriggers a sounding note by itself. Edits to the current
  pattern take effect from the current row.
- **Edit of the patch** with an unchanged `.trk` is an ordinary reload;
  unrelated param edits glide without touching the tracker.

## 6. What it deliberately does not do

- **No effect column.** Portamento, arpeggio, volume slides and retrigger
  commands are modules (`func` slew, `switch`, `euclid`, `adsr`) patched to
  the tracker's CVs. The two `cv` columns are the effect columns.
- **No instruments or samples.** A track is a control stream; what it plays
  is the patch.
- **No tempo, speed or groove.** Rows advance on the clock; swing lives in
  `transport`.
- **No polyphony inside a track.** One note per track; a chord is three
  fields (`note cva cvb`) or three tracks.
- **No sub-row timing** (note delay, cut). Clock faster for finer grids.
- **No recording or editing from the engine.** The file is the source of
  truth; it is written by people (and diffed by git).

## 7. Tests

Parser: every syntax element, every diagnostic with line:col, mutation fuzz
(no crash, no hang, bounded output). Module: row timing against the clock
(row 0 at t = 0), retrigger gap, slur and off, held values, layers and
wrapping, order repeats, loop point and end, reset and `start`, hot-reload
position continuity, empty/garbage text, the registry-wide hostile sweep
(with a `.trk` fixture) and the every-patch no-allocation test. Integration:
a `.trk` error reports `file:line:col` through `build_patch`; the `.trk` is
listed among the watched files. Musical: `patches/mainstage-trk.patch` ports
`mainstage.patch` and renders with matching section boundaries and levels.
