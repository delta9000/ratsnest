# DROID Patch Language — Prior-Art Digest for a New Modular-Synth DSL

*Research agent report, 2026-07-10. Preserved verbatim. Subject: the text patch language of the DROID Universal CV Processor (Der Mann mit der Maschine / Mathias Kettner, launched 2020, current firmware blue-7, Dec 2025). Primary sources: the official manual (blue-3 PDF, 600+ pages, read in full for the language chapters), blue-4→7 release notes, the 24-page MOD WIGGLER thread (2020–2025, retrieved via Wayback), lines (llllllll.co) posts, and real-world patches on GitHub/Patchstorage. Note: the vendor community lives on Discord plus MOD WIGGLER, so user-experience evidence comes from MOD WIGGLER, lines, and public repos.*

---

## 1. Core syntax

A DROID patch is a single plain-text file `droid.ini` on a micro SD card. It is **a flat list of circuit declarations** — no nesting, no includes, no top-level config. Circuits (their word for module instances) are declared with an ini-style header, followed by `parameter = value` lines. Everything is an assignment; there are no statements and no execution order semantics (all circuits run concurrently every ~sub-ms tick).

```ini
[lfo]
    hz       = 0.5
    triangle = _CABLE_1

[contour]
    gate    = I1
    decay   = _CABLE_1
    sustain = P1.1
    release = I2
    output  = O1
```

Key mechanics:

- **Inputs vs outputs share one syntax.** `hz = 0.5` sets an input; `triangle = _CABLE_1` routes an output. You only know which is which from the circuit reference (the Forge colors inputs blue, outputs red). A line is simultaneously "set parameter" and "patch cable."
- **Hardware is addressed through registers** — single letters + numbers: `I1..I8` (CV in), `O1..O8` (CV out), `N1..N8` (writable *normalization* of each input: write a signal to `N1` and `I1` reads it whenever no physical cable is inserted — software-defined normalling), `G1..G8` / `G2.7` (gate expanders), `P1.2` (pot 2 on controller 1), `B3.6` (button), `L3.6` (that button's LED), `R1..R56` (panel LEDs), `X1` (a special register that displays any value on the master's 16 LEDs — their only "debugger display" pre-2025).
- **Controllers are declared as empty sections** — `[p2b8]`, `[b32]`, `[m4]` — and their *order in the file* assigns the numbering used by `P1.x`/`B2.x` registers.
- **Internal patch cables** are just identifiers starting with `_` (e.g. `_CLOCK`, `_SELECT_ATTACK`). Rules enforced at load: every cable must have **exactly one writer** and **at least one reader**, otherwise the patch refuses to load (green blink code). Cables are implicit multiples (one writer, many readers).
- **Outputs are readable**: you can use `O1` or an LED register `R1` as an input elsewhere; writing to an input register is a load error.
- Comments with `#` (used heavily); indentation is conventional, not significant; **max 63 chars per line**; max 64 KB patch (excluding whitespace/comments); ~110 KB circuit RAM budget with documented per-circuit costs.
- **Error reporting**: on load failure the master writes `DROIDERR.TXT` (`ERROR IN LINE 17: Invalid output 'O9'. Allowed is O1 ... O8`) *and* flashes the line number and error class in colored LED blink codes — a full error taxonomy exists (unknown register / unknown parameter / unknown circuit / line too long / cable misuse / invalid header...).

A real patch from the wild ([drum sequencer](https://github.com/alienlebarge/droid/blob/main/Episodes/episode-13/drumsequencer.ini)) shows idiomatic style — a large comment header documenting the entire control surface, then dozens of small circuits wired by `_CABLES`:

```ini
[p2b8] # Master
[p2b8] # BD
...
# P1.1  Global activity
# B1.2  Mute BD
...
[lfo]
    hz = P1.2 * 16
    square = G8

[togglebutton]
    button = B1.3
    led = L1.3
    output = _BRANCHES
```

## 2. Values and expressions

- **Everything is a unitless float internally**; jacks map ±10 V ↔ ±1.0. Two suffixes sugar this: `2.5V` ≡ `0.25`, `45%` ≡ `0.45`; `on`/`off` ≡ 1/0. Pitch is 1 V/oct, so a semitone is `1/120` — patches quantizing to semitones set `outputscale = 1/120`, a recurring idiom (and a recurring "huh?" for newcomers). No scientific notation; no `.5` without leading zero. The manual itself warns about the suffix-precedence trap: `1/12V` parses as `1/(12V)` = 0.8333, **not** (1/12)V = 0.00833.
- **Expressions are hard-limited to the affine form `A * B + C`** on input lines only. One multiplication, one addition, in that order; each operand may be a constant, register, or cable. Sugar: `-P1.1` (≡ `-1 * P1.1`, consumes your multiply), `P1.1 - 0.5`, `I1 / 12` (division by *constants only*). No brackets, no chained ops, no dynamic division — for that you instantiate a `math` or `mixer` circuit.
- The manual is explicit about **why**: every input line compiles to a fixed multiply-add (`B=1`, `C=0` when absent), so the realtime engine evaluates no expression trees, no branches. "It's really super fast. And that's important because you want your Droid to have low latency and smooth envelopes."
- This yields the canonical Eurorack idioms directly in text: attenuation `level = P1.1 * 0.5`, CV with attenuator `level = I1 * P1.1`, inversion `level = -P1.1`, offset/range mapping `level = P1.1 * 0.8 + 0.2` (2–10 V), coarse+fine `rate = 0.05 * P1.2 + P1.1`, two-signal mix `input = I1 + I2`. **Every input has a free VCA and offset built in** — the marketing line is accurate and users love it.

## 3. Multi-instance and naming

- **Instances are anonymous and positional.** Multiple LFOs = write `[lfo]` twice. There is no instance name in the language; the firmware numbers same-type circuits by order of appearance ("button 2", "buttongroup 1").
- This positional identity is load-bearing and fragile: saved state is keyed by *(circuit type, ordinal)*. The manual concedes: "If you change the order of the circuits in your patch, circuits will get the 'wrong' states when you first start it."
- **Multi-output circuits use named outputs**, one per waveform/function: `[lfo]` has `square`, `sawtooth`, `triangle`, `paraboloid`, `sine`, `cosine` outputs *plus* a generic `output` whose shape is selected by a `waveform` input; `[button]` has `output`, `inverted`, `longpress`; `[contour]` has `output` plus stage outputs.
- **Parameter arrays** handle multi-channel: `output1, output2, …`, `pitch1..pitch8`, `button1..buttonN`; the `1` may be omitted (`output` ≡ `output1`).
- Since instances have no names, **internal cables are the de-facto naming layer** — `_BD_TRIGGER`, `_SELECT_ATTACK` carry all the semantic naming in real patches, plus `#` comments on the section headers (the Forge shows these as circuit labels).

## 4. Array-ish / tabular state

- **Small sequences are literal parameter arrays** — an 8-step sequencer is 8 numbered lines (`pitch1 = 1V … pitch8 = 7`), gates via `gate1 = L1.1` etc. Longer sequences chain circuit instances with `chaintonext = 1`. There is no list/table literal syntax at all.
- **Scales** are not tables either: `minifonion` takes `root = 2`, `degree = 7` (enumerated scales), and per-degree toggle inputs `select1..select13` / `selectfill1..5` — users build custom scales by wiring 12 buttons to those inputs.
- **Big mutable state lives outside the text.** `motoquencer` (32-step motor-fader sequencer, 8 parameters per step) keeps its pattern data in an opaque binary state file, edited via hardware faders, never rendered as text. Text sets *behavior* (`numsteps = 16`, `page = _PAGE`, `cvbase`/`cvrange`, `fadermode`); state is invisible.
- **Presets** are numbered slots inside stateful circuits: inputs `preset` (slot number CV), `loadpreset`, `savepreset` (triggers), with counts fixed per circuit (algoquencer 16, motoquencer 4). All state + presets auto-save to `DROIDSTA.BIN` (binary, 128 KB, per-card) ~1.5 s after any manual change. `dontsave = 1` opts a circuit out. Doc-notable distinction: only *results of manual interaction* are state; anything driven by a CV input (even a pot mapped to it) is not saved — "it's still a CV."

## 5. Live workflow

Evolution of the load loop, in order:

1. **2020, launch:** edit `droid.ini` on a computer → sneakernet the SD card → press the master's button → full restart with LED error codes. One patch per card (later: `droidXY.ini` alternates, each with its own state file).
2. **X7 expander (~2021):** USB access to the SD card *in place* + **fast patch upload via MIDI SysEx**.
3. **DROID Forge (2022, v1.7 current):** free Mac/Windows GUI that edits the *same* text format — spreadsheet-like three-column input cells (value × factor + offset), rack view with clickable jacks, named-cable dialogs, drag-drop "sections" (saved as comments), a problem checker that refuses to deploy broken patches, and one-click **"Activate!"** over USB/SysEx.
4. **Patch generators (recent Forge feature):** embedded **Python scripts that generate droid.ini**, with a community library ([droid-metapatch](https://github.com/Eising/droid-metapatch)) — the official answer to the language's lack of loops/macros.
5. **blue-7 (Dec 2025):** the DB8E **display** controller + `[display]` circuit ("Hell has frozen over. Droid got a display!") — the answer to years of introspection complaints.

**What survives a reload:** not runtime dynamics (LFO phase, cvlooper contents — explicitly not saved), but all manual-interaction state, restored by (type, ordinal) matching: fewer circuits → extra states dropped; more → defaults; reordered → wrong states. There is no live re-patching of a running graph; every load is a restart with state re-injection.

## 6. User feedback

**Praise (recurring themes):**
- *Total ownability*: "you can probably make it do exactly what you want it to do, exactly the way you want it done. The documentation is very good and the developer is readily available" (mdoudoroff, lines). Because *you* designed the control mapping, "you automatically know how to control it."
- *The sweet spot*: "the module seems to sit in a space between configuration and programming, as the former is too restrictive and the latter simply too involved" (MOD WIGGLER, pre-release — echoed by the maker: "These are no programs… you just assign which virtual jacks to wire where").
- *Text beats GUI for some*: "Personally I'd find typing out a text file quicker and more precise than fiddling around with virtual knobs and patch cables."
- *Shareability*: patches are posted as text on Discord/Patchstorage/GitHub; comments make them self-documenting; the manual promises forward-compat ("will still work in 30 years").
- The **manual itself** is repeatedly cited as a selling point.

**Complaints:**
- *The edit loop* (dominant early complaint): "Moving the SD-card back and forth between the Droid and the PC had become so tedious that I had paused my experimenting for quite some time" (user, post-X7); starthief pre-release: Teletype's live-coding "flow… would be utterly killed by having to move an SD card."
- *Debugging/introspection*: "it's sometimes hard to debug things using just the X1 LED output, a double-click to generate a status file and a few other limited ways of getting info"; "If there was a display… I would drop like $1k on Droid without hesitation."
- *Learning curve*: "steep and long, but if it clicks for you it opens up an entirely new perspective" — the vendor's own product page says patches "can look intimidating."
- *Typing-as-barrier*: "typing code, even if it's considered simple, is a huge hurdle" (pre-Forge; multiple users asked for a GUI + simulator on day one — the maker replied he'd rather build circuits than GUIs, then built the Forge anyway).
- *Verbosity / no abstraction*: no loops, no macros, no user-defined circuits; big patches are hundreds of near-identical lines (the vendor's own 512-button "Megasequencer" demo needs 16 `[p2b8]` declarations and enormous generated files). Community answered with Python generators; vendor then absorbed generators into the Forge.
- *Positional state fragility* (§3) and one-patch-at-a-time.

**Vendor response pattern** — notable in itself: the grammar never changed across 7 firmware generations; every complaint was answered with *hardware, tooling, or new circuits* (X7 → Forge → generators → DB8E display), and release notes credit suggesting users by name.

## 7. Design lessons for the modsynth DSL

For a grammar of `name = type { param: value, ... }` declarations plus `src.port -> dst.port : depth` cables, with modulation summing onto param bases and no normalling:

1. **Named instances fix DROID's worst structural flaw.** DROID's positional (type, ordinal) identity silently corrupts saved state on reorder and prevents refactoring. Our `name =` is the fix — commit to it fully: key *all* persisted/runtime state, error messages, and hot-reload state carry-over by instance name, and make renames explicit rather than silent state loss.

2. **DROID's `A * B + C` restriction is our cable grammar, validated.** Their single hard-limited multiply-add per input line is exactly `base + Σ(src × depth)` — and it proved both fast (compiles to a fixed FMA, no expression trees in the audio path) and *idiomatically right* for synthesists (every input is an attenuverter+offset). Lesson: resist inline expression creep; when users need more math, give them a `math`-style node, not a richer grammar.

3. **Make cable errors structural, not stringly.** DROID validates `_CABLES` at load — exactly one writer, at least one reader, dangling = refuse to load with a specific error class. With explicit `src.port -> dst.port` lines we can do better: verify both endpoints and port names exist, flag never-read outputs / unreachable modules as `check` warnings, and (since we sum, unlike DROID) *allow* multiple writers to one param as the normal case. Keep DROID's strictness — it catches typos in exactly the place a flat text format is weakest.

4. **Error messages and introspection are the product, not a feature.** `ERROR IN LINE 17: Invalid output 'O9'. Allowed is O1 ... O8` made a screenless module debuggable; conversely, the *lack of runtime value visibility* was the longest-running complaint, only resolved by shipping a display five years in. For load errors: exact location + what was expected + what's allowed. For runtime: build value-watch/scope/probe on any port from day one — DROID proves users hit this wall immediately on non-trivial patches.

5. **Units need to be first-class, not suffix arithmetic.** DROID's normalized-float core with `V`/`%` sugar is elegant until `1/12V ≠ (1/12)V` and "semitones are `1/120`" — real traps documented in the manual itself. modsynth avoids the precedence trap by having no inline arithmetic at all; keep it that way, and keep declaring params in natural units (`hz`, `s`) normalized at lex time.

6. **Freeze the grammar, grow the vocabulary.** Seven firmware versions, zero syntax changes — everything shipped as new node types and new params on existing types. That stability is why third-party editors, the Forge, and Python generators all interoperate on the same files, and why old patches keep loading. Version the *module library*, not the language.

7. **Separate structure (text) from performance state (sidecar), and define the boundary explicitly.** DROID's rule — text declares behavior; manual-interaction state auto-saves to a binary sidecar and is re-matched on reload; CV-driven values are never state — is a genuinely good architecture for hot-reload. modsynth's answer differs (the GUI writes *back to the text*, which stays the single source of truth) but the "CV-driven values are never state" distinction and the per-node `dontsave` escape hatch are worth keeping in mind for the GUI/preset era.

8. **Plan the abstraction layer before users do.** DROID's flat format has no loops, arrays-of-nodes, or user-defined subcircuits; power users immediately built external Python generators and the vendor eventually blessed metaprogramming rather than extending the language. Decide up front: lightweight in-language repetition (banks), or a first-class generation API. Either beats five years of ad-hoc scripts. Corollary on the "no normalling" stance: DROID made normalling *programmable* (`N` registers as writable defaults) and it's beloved — modsynth's params-with-defaults covers most of this; if a "default source overridden by a later cable" idiom is ever needed, design it deliberately.

---

## Sources

- [DROID manual, firmware blue-3 (primary reference for all syntax sections)](https://dermannmitdermaschine.de/download/droid-manual-blue-3.pdf) — esp. ch. 2 (patches, numbers/voltages, multiply-add, internal connections), ch. 3.2 (presets), ch. 4 (text-editor syntax, error codes, registers, attenuation grammar, parameter arrays, comments, multi-patch), ch. 9 (state saving, circuit ordering), circuit refs 13.28 lfo, 13.36 minifonion, 13.38 motoquencer, 13.53 sequencer
- [DROID manual, blue-7 (current)](https://dermannmitdermaschine.de/download/droid-manual-blue-7.pdf) · [Downloads page](https://shop.dermannmitdermaschine.de/pages/downloads)
- [Release notes blue-7 / Forge 1.7](https://dermannmitdermaschine.de/download/Release%20notes%20blue-7.pdf), plus blue-4/5/6 release notes (same URL pattern)
- [DROID product page](https://shop.dermannmitdermaschine.de/pages/droid-universal-cv-processor)
- [MOD WIGGLER: "DROID Universal CV processor" thread](https://www.modwiggler.com/forum/viewtopic.php?t=231105) (retrieved via Wayback Machine)
- [lines (llllllll.co) — mdoudoroff on Droid](https://llllllll.co/t/eurorack-discussion-advice-learning-questions/26440)
- [alienlebarge/droid — real-world patch collection](https://github.com/alienlebarge/droid)
- [Eising/droid-metapatch — Python patch generator library](https://github.com/Eising/droid-metapatch) · [PyPI](https://pypi.org/project/droid-metapatch/1.1.0/)
- [letmp/droid-gogglepack — VSCode/Sublime language extensions](https://github.com/letmp/droid-gogglepack)
- [Patchstorage DROID platform](https://patchstorage.com/platform/droid/)
- [ModularGrid forum thread on DROID](https://modulargrid.net/e/forum/posts/index/10621)
