# The subpatch library — interfaces and measured levels

`patches/lib/*.sub` are finished voices. Instantiate before you rebuild. This
is the table that tells you what they take, what they give, and **how loud they
actually are** — the thing no relative "kick channel ~0.85" advice can supply.

```text
k = sub { file: "lib/kick.sub", tune: G1, click: 0.5 }
clk -> k.trig
k.main -> mixer.in1 : 0.85
```

`file:` is quoted and resolves **relative to the file containing the `sub`**.
From `patches/foo.patch` that is `"lib/kick.sub"`; from `patches/etudes/x.patch`
it is `"../lib/kit/kick.sub"`. Absolute paths also work. Every other key
overrides a `param`; unknown keys are compile errors, so a typo is caught by
`check` rather than by silence.

## Interfaces

| Sub | Params (default) | Inputs | Outputs |
|---|---|---|---|
| `lib/kick.sub` | `tune` 50hz, `decay` 0.095s, `click` 0.32 | `trig` | `main` |
| `lib/bass.sub` | `pitch` G1, `decay` 0.22s | `trig` | `main` |
| `lib/hat.sub` | `pitch` 8khz, `decay` 0.05s, `color` 0.15 | `trig` | `main` |
| `lib/mallet.sub` | `tune` A2, `decay` 0.4s | `trig`, `cv` (0) | `main`, `aux` |
| `lib/sym-drone.sub` | `tune` E2, `damp` .84, `damp2` .88, `pos` .28, `pos2` .35, `prate` .18, `pdepth` .45, `pfb` .15 | `trig`, `root` (0), `fifth` (0.58333), `phmix` (0.35) | `main`, `aux` |
| `lib/frozen-cloud.sub` | `secs` 2.5, `seed` 7 | `in`, `freeze`, `lvl` (1) | `main`, `dry`, `hall` |

### The physical kit — one shared contract

Every `lib/kit/*.sub` takes `trig` and `accent` (0..1, latched per hit, drives
level *and* strike brightness) and gives `main`.

| Sub | Params (default) | Extra inputs |
|---|---|---|
| `kit/kick.sub` | `tune` 48hz, `decay` 0.45, `bend` 2.5 | — |
| `kit/snare.sub` | `tune` 190hz, `decay` 0.35, `snap` 0.5 | — |
| `kit/tom.sub` | `tune` 140hz, `decay` 0.6, `bend` 0.3 | — |
| `kit/rim.sub` | `tune` 480hz, `decay` 0.3 | — |
| `kit/clap.sub` | `tune` 1.1khz, `decay` 0.15s | — |
| `kit/hat.sub` | `tune` 450hz, `decay` 0.25, `tone` 6.5khz | `choke` |
| `kit/hat_model.sub` | `tune` 0.0 (v/oct), `decay` 0.3s, `tone` 7khz | `choke` |

**`decay` units are inconsistent and this will bite you.** On the modal voices
(`kick`, `snare`, `tom`, `rim`, `hat`) it is a 0..1 ring amount. On `clap` and
`hat_model` it is **seconds**. Check the table before copying a value across.

**Cabling `accent` replaces the 0.7 default** rather than adding to it. If you
want per-hit dynamics around a floor, cable a constant base too:

```text
0.5 -> chat.accent            # floor
coin.a -> chat.accent : 0.4   # accents ride on top
```

Closed and open hats are two instances of one file, choke-coupled:

```text
chat = sub { file: "lib/kit/hat.sub", decay: 0.22 }
ohat = sub { file: "lib/kit/hat.sub", decay: 0.45 }
pat.trig -> chat.trig
open_pat.trig -> ohat.trig
pat.trig -> ohat.choke        # every closed hit chokes the open ring
```

## Measured output levels

Each sub instantiated alone, `main -> out.in` at unity, 120 BPM triggers,
`--stats` over 4 s. These are the numbers to stage against.

| Sub | peak | RMS | Note |
|---|---|---|---|
| `bass.sub` | **0.0** | −6.4 | **clips at unity.** Stage at 0.4–0.5. Also carries −0.006 DC — block it |
| `kick.sub` | −0.6 | −17.9 | the peak anchor; ~0.85 on its channel is the house setting |
| `mallet.sub` | −0.3 | −13.4 | the worked example in `writing-subs.md`; `aux` gives you its envelope |
| `kit/kick.sub` | −3.7 | −21.5 | quieter than `kick.sub`; needs ~+3 dB to swap in |
| `kit/tom.sub` | −3.7 | −19.8 | |
| `kit/hat.sub` | −3.7 | −26.0 | |
| `hat.sub` | −3.5 | −27.7 | |
| `kit/clap.sub` | −3.1 | −27.7 | |
| `kit/snare.sub` | −4.1 | −29.8 | |
| `kit/rim.sub` | −4.8 | −25.2 | |
| `kit/hat_model.sub` | −6.9 | −30.4 | the quietest voice; lift it ~3 dB against `kit/hat` |
| `sym-drone.sub` | −5.5 | −27.9 | sustained, so RMS is the meaningful figure |
| `frozen-cloud.sub` | −11.2 | −26.9 | measured through its own demo; feed it **hot** (×2) — its staging assumes a string-level input |

Two consequences worth internalising:

- **The kit is not level-matched.** Peaks span 3.8 dB and RMS spans 10 dB. A
  bare `mix { levels: [1,1,1,1] }` across the kit is a bad mix by default.
- **`bass.sub` is the only voice that clips at unity.** Every "why is my mix
  distorting" investigation should check it first.

## Auditioning

Every `.sub` plays itself — its `demo.*` section runs only when the file is
compiled directly:

```bash
mise run play   -- patches/lib/kit/snare.sub
mise run render -- patches/lib/kit/snare.sub -d 4s -o /tmp/s.wav --stats
```

Note the demo sections apply their own gains (`hat.sub` at 0.6, `kit/hat.sub`
and `hat_model.sub` at 0.7, `bass.sub` at 0.5, `sym-drone` and `frozen-cloud`
through `out { level: 0.6 }`), so a demo render is **not** the level the sub
hands you. The table above is.
