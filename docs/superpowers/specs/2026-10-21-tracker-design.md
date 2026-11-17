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

Declares the next track (track 1, then 2, ... up to 16, in file order) and
the **fields its cells hold, in the order they are written**. Fields:
`note`, `vel`, `cva`, `cvb`, `cvd`, each at most once. (`cvd` came later, so
a four-note chord fits one track: `note cva cvb cvd`.) `<name>` is a lowercase
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

| token | in `note` | in `vel` / `cva` / `cvb` / `cvd` |
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
holds `voct` 0, `vel` 1, `cva`/`cvb`/`cvd` 0, gate off.

### 2.3 The order list

    order <entry>...

Appends entries to the song's arrangement; `order` lines accumulate, so an
arrangement can be written one section per line with a comment. Entries:

- `name` — play pattern `name` for its length.
- `a+b+c` — play the patterns **together** (layers). The step lasts as long
  as the **first** pattern; each other layer loops within the step (its row
  is `step_row mod its length`), so none may be longer than the first: a
  longer one would lose its tail unheard, and is an error (list the longest
  layer first). Layers must cover disjoint tracks (`@` overlays excepted, §8.4). Tracks no
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
- `layer 'mel' (128 rows) is longer than the step (64 rows, set by 'harm'); list the longest layer first`
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

Outputs: six banks of sixteen, named like `alloc`/`midi` banks: `voct1..16`,
`gate1..16`, `vel1..16`, `cva1..16`, `cvb1..16`, `cvd1..16` (track n is
`voctn`, `gaten`, ...; grouped per track, so track n's ports are outputs
6(n−1) .. 6(n−1)+5), then
`step` (0-based order step), `pattern` (index, in file order, of the step's
first pattern), `row` (row within the step), `step_trig` (one-sample pulse
when a step starts), `row_trig` (one-sample pulse on every row), `end`.
A bare `song` reads `voct1`. Tracks the file does not declare output 0.

The bank is fixed at sixteen tracks because outputs must be declared up
front (like `alloc`/`midi` banks); sixteen tracks × six columns plus six song
outputs is 102 ports. (It was eight × five until songs ran out of tracks and
had to split across two trackers whose order lists then had to agree step
for step.) Patches cable by name, so growing the bank moves output indices
but no patch. Port names cannot contain dots (an endpoint splits at its last
dot), hence `voct1`, not `t1.voct`. A cable from a track the `.trk` never
declares is an error at load (it would read a silent 0).

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

- **No pitch or level effects.** Portamento, arpeggio and vibrato are
  modules (`func` slew, `switch`, `lfo`) patched to the tracker's CVs; the
  `cv` columns are the effect columns, and `>` ramps are the volume slide.
- **No instruments or samples.** A track is a control stream; what it plays
  is the patch.
- **No tempo, speed or groove.** Rows advance on the clock; swing lives in
  `transport`.
- **No polyphony inside a track.** One note per track; a chord is three
  fields (`note cva cvb`) or three tracks.
- **No effect column beyond timing.** Sub-row timing (§8.8b) is the one
  classic effect family built in, because a gate the tracker owns cannot be
  delayed or ratcheted from outside it.
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

## 8. Composing

Three composers wrote songs with the first version; they needed 430–600 lines
for 13–105 musical ideas, because the format could not say "this riff over the
current chord", "this section up a whole step", "notes last one row" or "a
fill over the groove". These additions all resolve while the file is parsed
and flattened (non-RT); the flattened `TrkRow` array, the module and its ports
are unchanged.

### 8.1 Transposition: `name^N`

Any layer of an order entry may carry `^N`, N in −48..48 semitones:
`order verse verse^5`, `bass^-2+beat`, `a^2+b+c^2`. It adds N/12 v/oct to
every note the layer writes (note-name or numeric) and to `cva`/`cvb`/`cvd` values
**written as note names**; the parser records per value whether it was a
pitch, so numeric levels are never shifted. `x` keeps the held pitch.

### 8.2 Note length: `len N`, `C4:L`

`track hat note len 1` gives every note (and `x`, and slur) on the track a
length in rows; a note token may override it with `:L` (`C4:3`, `x:2`,
`~D4:4`), L in 1..4096. A note of length L at song row R sounds rows R..R+L−1
and its gate drops on row R+L, unless another note, slur or `off` on that
track comes first (it replaces the pending drop). Without `len` a note holds
until changed, as before. Lengths count song rows, so a note in a looping
layer, or on a step's last row, rings across the wrap or the step boundary.
This removes the `off` row after every short note (hats, plucks, hooks).

### 8.3 Chord tones: `from <track>`, `t1`..`t4`

`track arp note vel from harm` makes `arp` a dependent of `harm`, which must
be declared earlier and hold `note` (and optionally `cva`, `cvb`, `cvd`). In
a dependent track's cells the pitch tokens `t1`, `t2`, `t3`, `t4` mean the
source's **resolved** note, cva, cvb and cvd on that song row, after the source's own
`^N`, with an optional semitone offset (`t1-12`, `t3+12`, `t2+7`, ±48) and
`:L` length. In the note field they act like a note (gate, retrigger; `~t2`
slurs); in `cva`/`cvb`/`cvd` they set the value. They count as pitches, so the
dependent layer's own `^N` adds on top. Tracks resolve in declaration order
on every row, which is why the source comes first. A token naming a field the
source does not hold is an error. This is what lets one 16-row arp or bass
riff loop under every chord of the song instead of being rewritten per chord.

### 8.4 One-shot overlays: `name@R`

In a layer group, a layer written `name@R` (with `^N` before or after) plays
**once**, starting at row R of the step, instead of looping; rows past the
step's end are cut. For its span its **non-empty** events replace those of
earlier layers on the same tracks (an empty cell lets the groove show
through), so `order verse+beat+fill@48` puts a fill over the groove's last
bar and `chords+chop+hit@0` a drop hit, without full-length copies. Only
overlays may share tracks with other layers; the first layer sets the step
length and cannot be one; R past the step's last row is an error.

### 8.4b Free-running layers: `name~`

In a layer group, a layer written `name~` (with `^N` before or after) loops
on the **song** row instead of the step row: on song row s it plays its row
s mod its length, so its phase carries across steps and sections instead of
restarting at each. A 7-row figure over 16-row bars therefore drifts through
the bar and realigns every 7 bars, with no second tracker and no
least-common-multiple clock. It follows the looping layers' rules otherwise
(it may not share tracks; every event replaces the held state), except that
it may be longer than the step, since its tail plays in the steps that
follow. It must be repeated in every step that should hear it (a section is
the place to put it once). The first layer sets the step length and cannot
be free-running; a layer cannot be both `~` and an `@` overlay. Phase counts
from the first row of the song, so a `start` or a `|:` wrap replays the
phase it had there the first time through.

`~` was chosen because it already means "carries on" in a cell (`~D4`, a
slur: the note continues without re-attack) and is not one of the order
list's other marks (`+ ^ @ * |:`); a word (`free:name`) would not compose
with `^N`/`@R` the same way, and a second order list per layer would split
one song across two timelines.

### 8.5 Drum lanes: `lane <track> <steps>`

Inside a pattern, `lane kick x... x... x..x x.0.` writes one row per
character from row 0; whitespace is ignored, so bars can be spaced out. `.`
is empty, `x` a hit at the held vel, `1`–`9` a hit with vel n/9 (on a track
without `vel`, just a hit), `0` a note off. A lane longer than the pattern,
or naming a track the pattern does not cover, is an error. Lanes and row
lines mix freely in one pattern: a lane fills cells a row line left empty and
vice versa, and writing the same field of the same cell twice is an error.
A lane is the drum-machine view of what is otherwise one sparse row per hit.

### 8.6 Bars: `pattern <name> BARSxROWS`

`pattern verse 4x16 bass harm` declares four bars of 16 rows (64 rows; the
total is still at most 1024). In such a pattern a row label may be written
`bar:row`, bar 1-based and row 0-based (`3:06` = row 38), and so may an
`@R` overlay start whose step's first layer has bars (`fill@4:08`). Plain
decimal rows keep working. `bar:row` against a pattern declared without bars
is an error that says to declare `pattern <name> BARSxROWS`.

### 8.7 Sections: `section <name> <layers>`

`section verse mel_v+pad_v+bass+beat` names one layer group (layers may
carry `^N` and `@R`); the order list then uses the name like a pattern:
`order verse*2 chorus verse^2`. In an order entry a section is spliced into
its layers and more layers may follow it (`verse+fill@48`). `^N` on a
section transposes the music in it **once**: it adds to every written pitch
(notes, note-name cvs) but not to chord tones, which already move with their
transposed source; a layer's own `^N` (`riff^-12` inside the section) still
applies to its chord tones as in §8.3. Otherwise `verse^5` would move an arp
that follows the chords by ten semitones (a dependent whose source is not in
the section keeps following that source). A section cannot itself be an `@`
overlay or contain another section. Sections share the pattern namespace
(a clash is an error; a typo gets a did-you-mean over both) and may be
declared before the patterns they name. Each section is checked at its own
line; errors that arise only from how an order entry combines it are
reported at the entry.

### 8.8 Ramps: a trailing `>`

A `vel`, `cva` or `cvb` value with a trailing `>` (`.2>`, `A3>`) glides
linearly, one step per row, to the next value written in the same field of
the same track later in the same pattern: the rows between are filled in
when the file is parsed. A ramp with no later value in its pattern (or one
whose target is a chord tone) is an error at the ramp. The filled values are
pitches only when both ends are, so `^N` moves a pitch glide and leaves a
level fade alone. Values stay stepped per row (§4.3); `func` still smooths.

### 8.8b Sub-row timing: `@D`, `*N`, `?P`

Suffixes on a note-field token, in any order and mixed with `:L`, borrow
three classic tracker effects (concept only: note delay EDx and Renoise's
delay column, retrigger E9x, Renoise's Yxx maybe-play / Elektron trig
probability):

- `C4@.25` lands the row's whole change for that track (pitch, gate, vel,
  cvs) a quarter of a clock period after the edge. `off@.5` is a note cut.
  D is in 0 up to but not 1.
- `x*3` hits three times, evenly across the rest of the row (after any
  delay), each hit a one-sample gate drop like any re-articulation. 1..16.
- `x?.5` sounds with probability .5: on its first hit a draw from the
  module's xorshift32 (config `seed`, default 1) decides, and a miss keeps
  the track's gate low until its next note or `x`. Rising `reset` rewinds
  the draws, so a reset replays the same pass; a looping song draws fresh
  each time round. `*N` and `?P` go on notes and `x`, not on slurs or `off`.

A row's length is the module's own measurement: the interval two edges
back, which equals the row's length on a straight grid and under the
transport's two-row swing (where the last interval would be the other half
of the pair, and a short row's ratchet would spill). Timing so follows tempo
with no tempo input; until three edges have been seen (the first two rows
after load) everything lands on the edge. These are per row: they never
hold, and the flattened `TrkRow` carries `delay`, `ratchet` and `chance`.
A change still owed when the next edge arrives lands on that edge.

### 8.9 Example

Every composing feature in one song (a test parses it):

```text
track harm note cva cvb
track arp  note vel cva from harm len 1     # chord tones, one-row notes
track kick note vel len 1
pattern prog 2x16 harm                      # two bars of sixteen
1:00 | D3  F4 A3
2:00 | Bb2 D4 F3
pattern arp 8 arp                           # loops under every chord
| t1+12 .3> t3                              # vel ramps to .7 at row 4
4 | ~t2+12:3 .7 t1-12
pattern beat 16 kick
lane kick 9..5 x..5 9..5 x.0.
pattern fill 4 kick
lane kick 9999
section verse prog+arp+beat
order verse*2 |: verse^5+fill@2:12 verse^-2
```
