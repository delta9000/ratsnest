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
| `lib/synth-voice.sub` | `wave1` saw, `wave2` pulse, `blend` .35, `osc_lvl` 1, `detune` .004, `oct2` 0, `pw` .5, `sublvl` 0, `sub_oct` −1, `cutoff` 1200hz, `res` .25, `drive` 0, `slope` 1 (ladder; 0 = svf), `track` .5, `env_amt` 2500hz, `attack`/`decay`/`sustain`/`release` .01/.4/.7/.2, `curve` exp, `hp` 5hz, `glide` 0, `vib` 0, `vib_rate` 5.3hz | `voct`, `gate`, `vel` (1), `level` (1), `cut` (0, Hz) | `main` |
| `lib/strip.sub` | `gain` 1, `pos` 0, `width` 1, `send_gain` 0 | `in` or `in_l`/`in_r`, `level` (1), `duck` (1), `pan` (0), `send` (1) | `l`, `r`, `fx` |
| `lib/duck.sub` | `depth_db` −6, `attack` .004, `release` .25, `curve` lin | `trig`, `amount` (1) | `gain` |
| `lib/master-chain.sub` | EQ, `comp_thr` −16, `comp_ratio` 2.4, `comp_blend` .85, `key_bus` 0, `clip_drive` 6.5, `clip_blend` .32, `ceiling` −0.4, `true_peak` off | `in_l`/`in_r`, `side_l`/`side_r`, `width` (1.15), `makeup` (3.0 dB) | `l`, `r` |
| `lib/club-master.sub` | `ceiling` −1.0 (dBTP), `drive` 12.5, `key_bus` .35 | as master-chain; `width` (1.0), `makeup` (10.5) | `l`, `r` |

## A song's voices and mix: synth-voice, strip, duck

The default way to build a song's pitched parts and its mix. A part is a
voice, a strip, and the strip's two cables into the master:

```text
lead = sub { file: "../lib/synth-voice.sub", cutoff: 1400, env_amt: 3500, slope: 0 }
song.voct7 -> lead.voct
song.gate7 -> lead.gate
song.vel7  -> lead.vel
lead_s = sub { file: "../lib/strip.sub", gain: 0.6, pos: 0.1, send_gain: 0.8 }
lead -> lead_s.in
lead_lvl -> lead_s.level          # the form lane
pump -> lead_s.duck               # duck.sub gain, relative
lead_s.l -> mast.in_l
lead_s.r -> mast.in_r
lead_s.fx -> room.in_l            # post-fader, post-duck
```

- **synth-voice** presets by params (lead is the default): pluck =
  `sustain: 0, decay: 0.7, release: 0.7, cutoff: 600, env_amt: 4000`;
  pad = `wave2: saw, blend: 0.5, slope: 0, curve: lin, attack: 0.3, decay: 1,
  sustain: 0.8, release: 1.5, env_amt: 0`; bass = `blend: 0, sublvl: 0.6,
  sub_oct: 0, cutoff: 200, drive: 0.5, sustain: 0, decay: 0.5, release: 0.5`.
  Set `track: 0` to reproduce a hand-built voice with an untracked filter.
  Velocity into `vel` brightens as well as louder; into `level` it only
  louder. A chord pad is one instance per chord tone (an inline `subdef`
  holds the shared params: `patches/tracker/glasshouse.patch`).
- **strip** replaces `mix` for anything but a static mono kit: no slot limit
  (cables into the master's inputs sum), a stereo source is one strip, the
  `fx` send is tapped after the fader and the duck (a part muted by its lane
  sends nothing, so no reverb ghost), pan is equal-power. Mono `in` at
  centre is −3 dB per side, a stereo pair at centre is unity.
- **duck** turns a kick gate into a gain that dips `depth_db` and returns.
  It multiplies, so the dip is the same in dB whatever the lane level; a
  negative envelope summed onto `mix.lvlN` subtracts a fixed amount and
  mutes quiet sections. One instance per depth (bass deep, returns light).

Worked: `patches/tracker/glasshouse.patch` and `undertow.patch` (each
rebuilt from a hand-patched original; section RMS within 0.4 dB).

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
| `kit/crash.sub` | `tune` 250hz, `decay` 1.0s (−20 dB time), `tone` 7khz | `choke` |

**`decay` units are inconsistent and this will bite you.** On the modal voices
(`kick`, `snare`, `tom`, `rim`, `hat`) it is a 0..1 ring amount. On `clap`,
`hat_model` and `crash` it is **seconds**. Check the table before copying a value across.

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
| `bass.sub` | **+3.5** | −8.5 | **clips at unity.** Stage at 0.4–0.5. Also carries −0.006 DC — block it |
| `kick.sub` | −0.6 | −17.9 | the peak anchor; ~0.85 on its channel is the house setting |
| `mallet.sub` | −0.3 | −13.4 | the worked example in `writing-subs.md`; `aux` gives you its envelope |
| `kit/kick.sub` | −3.7 | −21.5 | quieter than `kick.sub`; needs ~+3 dB to swap in |
| `kit/tom.sub` | −3.7 | −19.8 | |
| `kit/hat.sub` | −3.8 | −27.1 | |
| `hat.sub` | −3.5 | −27.7 | |
| `kit/clap.sub` | −2.4 | −28.9 | |
| `kit/snare.sub` | −4.1 | −29.8 | |
| `kit/rim.sub` | −3.6 | −26.5 | |
| `kit/hat_model.sub` | −6.3 | −31.5 | the quietest voice; lift it ~3 dB against `kit/hat` |
| `kit/crash.sub` | −5.1 | −21.6 | one hit, RMS over its first second; −20 dB at 0.98 s, centroid 5.7 kHz |
| `synth-voice.sub` | −3.2 | −18.0 | lead defaults, accented eighths A2–A3; pluck −3.3/−20.2, pad −2.1/−12.6, bass −4.4/−20.3 |
| `strip.sub` | — | — | unity: mono at centre −3.0 dB per side, stereo pair at centre 0 dB |
| `duck.sub` | — | — | `gain` 1.0 at rest, 0.501 at the −6 dB dip, back to 0.99 at `release` |
| `sym-drone.sub` | −5.5 | −27.9 | sustained, so RMS is the meaningful figure |
| `frozen-cloud.sub` | −11.2 | −26.9 | measured through its own demo; feed it **hot** (×2) — its staging assumes a string-level input |

Two consequences worth internalising:

- **The kit is not level-matched.** Peaks span 3.9 dB and RMS spans 12 dB. A
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
