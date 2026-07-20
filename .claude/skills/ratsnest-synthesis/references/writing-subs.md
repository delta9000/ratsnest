# Writing a subpatch — packaging a voice

A `.sub` is a voice in its own file with a declared interface. Declaring any
`param` / `input` / `output` makes the file a subpatch: it may no longer
reference `out` outside a `demo.*` section, and instantiating it stamps a
namespaced copy at compile time.

Reach for one the second time you copy a voice between patches.

## The whole thing

```text
# lib/mallet.sub -- a struck triangle with a decay envelope.
tune  = param { default: A2 }        # compile-time value, not a signal
decay = param { default: 0.4s }

trig = input {}                      # outside: a destination
cv   = input { default: 0 }          # read when nothing is patched (rule 4)
main = output {}                     # outside: a source. First = the default
aux  = output {}

osc = vco { freq: tune, wave: tri }
cv -> osc.v_oct
env = func { rise: 0.004, fall: decay, curve: log }
trig -> env.trig
amp = vca { gain: 0 }
osc -> amp.in
env -> amp.cv
amp -> main.in
env -> aux.in                        # expose the envelope for ducking

demo.clk = clock { bpm: 120 }        # runs ONLY when compiled directly
demo.clk -> trig
main -> demo.out.in : 0.6
```

Used:

```text
clk = clock { bpm: 180 }
a = sub { file: "lib/mallet.sub", tune: A2 }
b = sub { file: "lib/mallet.sub", tune: E3, decay: 0.15s }
clk -> a.trig
clk -> b.trig
0.0833 -> b.cv                       # b a semitone up
a.main -> out.in : 0.5
b.main -> out.in : 0.35
```

Verified: the `.sub` checks and renders standalone (−4.8 dBFS peak), and the
two-instance patch checks and renders (−2.8 dBFS peak).

## The four declarations

| | What it is | Inside the body | Outside |
|---|---|---|---|
| `param { default: X }` | a compile-time **value** | usable in any config, as a constant source, or as a cable depth | overridden by `sub { key: value }` |
| `input { default: N }` | a port | a source (`trig -> env.trig`) | a destination (`clk -> k.trig`) |
| `output {}` | a port | a destination (`amp -> main.in`) | a source (`k.main -> mix.in1`) |
| `subdef {}` | an inline template | prefix everything (`td.tune`) | `sub { def: td }` |

A `param` is **not a signal** — it is substituted before the body compiles, so
it can set a `wave:` or an array element, but nothing can modulate it. If a
value needs to move at runtime, make it an `input`.

## Rules that will catch you

- **The first declared `output` is the instance default.** `k -> mix.in1` reads
  it. Declare the one you'll use most first.
- **Multiple outside cables into one `input` sum**, and depths multiply through
  the splice — same arithmetic as everywhere else.
- **An unpatched `input` reads its `default:` and nothing else.** There is no
  normalling into a subpatch either.
- **Cabling an input replaces nothing.** If the sub's body has `0.7 -> x` as a
  base and you cable `x`, they sum. This is why cabling `accent` on the kit
  voices needs an explicit floor cable — see `lib-voices.md`.
- **Stamped internals stay addressable**: `0.1 -> k.kosc.v_oct` reaches inside.
  The declared interface is the stable surface, not a wall — but if you find
  yourself reaching in routinely, that thing wants to be an `input`.
- **`demo.*` runs only on direct compile**, so every `.sub` auditions itself:
  `mise run play -- patches/lib/mallet.sub`. Give every sub a demo; it is the
  cheapest test you will ever write.
- **Unknown keys are compile errors.** A typo in `sub { tunee: A2 }` fails
  `check` instead of silently doing nothing.
- **Nesting is fine** (16 deep, cycles rejected), and `sub` composes with
  `voice` groups in both directions.
- **Saving a `.sub` hot-reloads every patch that includes it.**

## Design the interface, not the implementation

The good subs in `patches/lib/` all share a shape: a trigger, one or two
performance inputs, two or three timbre params, one main output plus an
optional tap. That is not a coincidence — it is what survives being used in a
track you write six months later.

- **Params are the things you'd retune per song** (`tune`, `decay`, `color`).
- **Inputs are the things that move inside a song** (`trig`, `accent`, `cv`).
- **Expose the envelope as an `aux` output.** Ducking, sends and follower
  tricks all want it, and rebuilding it outside is wasteful.
- **One contract across a family.** The eight kit voices all take
  `trig`/`accent`/`tune`/`decay` and give `main`, which is why they are
  interchangeable in a groove. If you write a ninth, match it.

## Gotchas specific to subpatch levels

Measure your sub's `main` output at unity before you ship it (instantiate it,
`v.main -> out.in`, render, read peak). `lib/bass.sub` clips at unity because
nobody did — its demo hides it behind a 0.5 cable. Aim for a peak around
−3 to −6 dBFS so it drops into a mix without surprises, and write the measured
figure into the file's header comment.
