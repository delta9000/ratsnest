# Subpatches: `sub` / `subdef` — Design

2026-07-18. Status: designed. Refines §3 of `2026-07-10-ratsnest-design.md`
(the patch file is the source of truth; compile-time expansion precedent set
by `2026-07-11-voice-alloc-design.md`). Motivating consumer: the physical-
modeling drumkit (now `2026-07-18-drumkit-design.md`) — a kit is 5–7 voices of ~10 modules each,
untenable to copy into every song patch. Decision recorded there: subpatches
land first; the drumkit ships as includable subpatch files.

## 1. Shape of the solution

An **instantiable subpatch**: a reusable voice with declared ports and
overridable params, stamped into the using patch as namespaced ordinary
modules. Implementation is a compile-time AST expansion pass exactly parallel
to `expand_voices` — the graph compiler, hot-reload diff, and RT engine never
learn subpatches exist.

Grammar cost: **zero new statement types.** Five pseudo-module types carry the
whole feature through the existing `name = type { config }` and cable
statements: `sub` (instantiate), `subdef` (mark an inline template), `param`,
`input`, `output` (interface of a template). All five are consumed by the
expansion pass; none exist at runtime.

Rejected alternatives: token-level textual splicing (hygiene and diagnostic
positions suffer); engine-level composite modules (touches RT code and the
reload pointer swap for encapsulation the drumkit doesn't need).

## 2. Defining a subpatch

A subpatch is a plain patch file (convention: `patches/lib/*.sub`) using
pseudo-modules for its interface:

```text
# lib/kick.sub — interface
tune  = param { default: F1 }
decay = param { default: 0.095s }
trig  = input {}
main  = output {}

# body — ordinary modules
kaenv = func { rise: 0.001s, fall: decay }
trig -> kaenv.trig
kosc = vco { wave: sine, freq: tune }
# ...
kmix.out_l -> main.in

# demo — compiled only when this file is played directly
demo.clk = clock { bpm: 120 }
demo.clk -> trig
main -> demo.out.in
```

- **`param`** declares a named value with a `default:`. The bare identifier is
  usable in any config value position (the AST's `Ident` value kind already
  flows there), as a constant cable source (`tune -> osc.v_oct`), and as a
  cable gain (`a -> b.in : depth`).
- **`input` / `output`** are transparent one-port pseudo-modules; cables
  through them are spliced away at expansion. `input { default: v }` gives
  the value read when no outside cable feeds the port (default 0) — rule 4,
  nothing implicit beyond the declaration.
- **`demo.*`** is a reserved prefix: those declarations (and only there,
  `demo.out`, the ordinary device sink) compile when the file is the
  top-level patch — `mise run play patches/lib/kick.sub` auditions the voice
  standalone — and are dropped before stamping when instantiated. `out`
  anywhere else in a subpatch file is a compile error.
- A file containing interface pseudo-modules **is** a subpatch file. Compiled
  top-level, it is implicitly instantiated once with param defaults — params
  substitute their `default:`, `input`/`output` splice as transparent
  junctions — with the demo section active.

### Inline templates

`subdef` marks a name prefix as a template inside any patch, parallel to how
`voice` marks a prefix for stamping:

```text
kickdef = subdef {}
kickdef.tune = param { default: F1 }
kickdef.trig = input {}
kickdef.kosc = vco { wave: sine, freq: kickdef.tune }
# ...
```

The expansion pass collects all `kickdef.*` declarations as the template body
and removes them — they never run themselves. Inside the body, params and
ports are referenced with the prefix (`kickdef.tune`); stamping strips it.
The `demo.*` convention does not apply to inline templates (the surrounding
patch is the demo).

## 3. Instantiating

```text
k  = sub { file: "lib/kick.sub", tune: G1 }
t1 = sub { def: tomdef, tune: A2, decay: 0.3s }
t2 = sub { def: tomdef, tune: E2 }

clk -> k.trig
k.main -> mix.in1
0.1 -> k.kosc.v_oct        # escape hatch: internals stay addressable
```

- `file:` (path relative to the including file) or `def:` (an in-scope
  `subdef` name), exactly one — both or neither is an error.
- Remaining config keys override `param` defaults; a key naming no param is
  an error, same as unknown config keys anywhere.
- Instance ports patch like module ports: `clk -> k.trig`, `k.main -> mix`.
  Multiple cables into an instance input sum, like any input.
- **Escape hatch:** stamped internals are real nodes (`k.kosc`), addressable
  by full dotted path from outside — the declared interface is the stable
  surface, not a wall. `watch`/`scope` see them like any node.

## 4. Expansion pass

`expand_subs(dsl::PatchAst&, Diags&)` in `src/graph/subs.{h,cpp}`, running
**before** `expand_voices`:

1. **Collect templates.** Each `subdef` prefix's declarations become a
   template body, removed from the AST. Each distinct `file:` is loaded and
   parsed once per compile (cached — ten toms parse `tom.sub` once).
2. **Recurse depth-first.** Template bodies may contain `sub`; an include
   stack detects cycles, depth > 16 errors, both naming the chain.
3. **Substitute params.** Overrides merge onto defaults; then every `Ident`
   config value, constant-cable source, and cable gain matching a param name
   is replaced by its value.
4. **Stamp.** Every internal name is prefixed with the instance name
   (`k.kaenv`); modules and cables splice into the main AST.
5. **Splice ports.** Cables touching `input`/`output` pseudo-modules reroute
   through them and the pseudo-modules are deleted: outside → `k.trig` fans
   out to whatever `trig` fed; `k.main` → outside sums whatever fed `main`;
   unpatched inputs contribute their `default:`.
6. **Demo handling.** Top-level compile of a subpatch file: implicit
   instantiation with defaults (§2), `demo.*` compiles normally. When
   instantiated from another patch: `demo.*` dropped before stamping.

After the pass the AST holds only ordinary modules and cables. Composition
with voice groups needs no rules: a `sub` inside a `voice` group expands
first, leaving dotted-name members for voice stamping (`harp.k.kosc#i`); a
`voice` group inside a subpatch is just more declarations to stamp.

## 5. Hot-reload, errors, diagnostics

- Compile returns the set of files read; the watcher watches all of them —
  saving `kick.sub` hot-reloads every open patch using it. Reload diffing
  operates on the expanded graph, so a `tune:` override edit glides as a
  param change; structural edits swap, as today.
- Rule 6 holds. Every failure — missing/unparseable file, cycle, unknown
  override, non-demo `out` in a file with interface pseudo-modules, an
  interface pseudo-module under a `demo.` prefix, `sub` with both or neither
  of `file:`/`def:` — reports
  `file:line:col` in the file that contains the error plus the instantiation
  chain ("in `k = sub` at kit.patch:12"), and the last good graph keeps
  playing.
- Name collisions after stamping (user declares `k.main` beside `k = sub`)
  are ordinary duplicate-name errors at the colliding declaration.

## 6. Testing

Unit (doctest, on `expand_subs` output):

- Namespacing; param substitution in config values, cable sources, gains.
- Defaults vs. overrides; unknown-override error; input `default:`.
- Port splicing: fan-out on inputs, summing on outputs, multiple outside
  cables summing onto one instance input.
- Inline `subdef` collection and removal; `demo.*` included top-level,
  dropped on instantiation; `out`-in-body error.
- Nesting, cycle error, depth error; template parse caching (parse count).
- Diagnostic file/line/col and instantiation chain.
- Composition: `sub` inside `voice`, `voice` inside `sub`.

Integration:

- `kick.sub` instantiated twice at different `tune:` renders two FFT-verified
  pitches (existing render-test style).
- Watcher reload on a sub-file edit reaches the playing graph.
- Escape-hatch cable into instance internals compiles and affects the render.

## 7. Docs

`docs/modules.md`: sections for `sub`, `subdef`, `param`, `input`, `output`
(registry-coverage test extended to cover pseudo-modules), the
`patches/lib/` convention, and a worked kick example under Recipes.
`patches/lib/kick.sub` ships as the first real subpatch (adapted from
`kick-solo.patch`) so the feature lands with a proven consumer.
