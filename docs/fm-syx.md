# Classic 6-op voices from SysEx (`tools/syx2patch.py`)

`tools/syx2patch.py` turns voice banks for the classic 1983 six-operator FM
instrument (the DX7 family's `.syx` files) into ordinary ratsnest subpatches
built on the `fm` module. The output is plain text: one `.sub` per voice that
you can read, diff, and play, so the `.patch` file stays the source of truth
(design rule 1). The repo doesn't bundle any voice data. Bring your own banks.

```sh
python3 tools/syx2patch.py my-bank.syx -o patches/dx/my-bank   # all 32 voices
python3 tools/syx2patch.py my-bank.syx -o out --voice 11       # just one
mise run play -- patches/dx/my-bank/play.patch                 # MIDI keyboard
mise run play -- patches/dx/my-bank/03-some-voice.sub          # demo riff
```

It reads the 32-voice packed bulk dump (4104 bytes), the bare 4096-byte voice
block some librarians save, and single-voice edit-buffer dumps (163 bytes).
A file can hold several messages. Checksum mismatches produce a warning, and
the voices are still converted.

## What you get

Each `NN-name.sub` has inputs `voct` (0 = A3, as `midi` emits), `gate` and
`vel` (0..1), output `main`, and a `level` param (default 0.25, the carriers'
straight sum times this). It also carries a `demo.*` arpeggio so the file
plays itself. `play.patch` is an 8-voice MIDI wrapper with ±2 semitone bend;
change its `file:` to pick a voice.

```text
v = sub { file: "patches/dx/my-bank/03-some-voice.sub", level: 0.3 }
```

## Mapping

| Voice parameter | ratsnest |
|---|---|
| Algorithm 1–32 | `algo: custom` + `route` edge list; the feedback op gets `fb` |
| Feedback 0–7 | `fb` = 2π·2^(n−7)/2.5 (a phase push of up to 2π × the op's output) |
| Algorithms 4 and 6 | `fb_from`: the loop runs through ops 6→4 (algo 4) or 6→5 (algo 6) |
| Coarse / fine / detune (ratio) | `ratios` (coarse 0 = 0.5; detune ≈ 0.9 cents per step) |
| Fixed mode | `fixed` Hz = 10^(coarse mod 4 + fine/100) |
| Output level + EG levels L1–L4 | `l1..l4` amplitudes (0.75 dB per level step), `levels` all 1 |
| EG rates R1–R4 | `curve: db`, `t1..t4` = seconds per 96 dB at that rate |
| Rate scaling 0–7 | `key_rate` = RS/8 octaves per octave, neutral at C4 (`key_break: 0.25`) |
| Breakpoint, depths, curves | `keys1..6` tables, exact at every MIDI note |
| Key velocity sensitivity 0–7 | `vel_curve` (the velocity table) × `vel_depth` = KVS/8 |
| Pitch EG rates/levels | `pitch_t` (s per octave) / `pitch_l` (octaves, ±4) |
| Osc key sync | `sync: on/off` |
| Transpose | `freq` = 220·2^((T−24)/12) |
| LFO speed, delay, wave, sync | `lfo` (+ `noise`→`sh` for S&H), an `adsr`+`math` delay ramp, `gate → lfo.reset` |
| PMD × PMS | the LFO into `fm.v_oct` at depth (PMD·165/64)·PMS-table/65536 octaves |
| AMD × AMS | the unipolar LFO into `lvlN` (approximate, see below) |
| Name | file name and header comment |

## How faithful it is

The aim is that a voice from your bank plays with its intended timbre,
envelopes, scaling and response. It isn't a sample-exact emulation of the
original chips. Known differences, roughly in order of audibility:

- **No chip grit.** The original computed in fixed point with log-sine and
  exponent ROMs, a 12-bit floating DAC and a ~49 kHz clock. ratsnest runs in
  floating point at the session rate. Some of the hardware's edge and
  aliasing character is absent.
- **LFO amplitude-modulation depth is approximate.** There's no verified
  public description of the AMS curve, so `AMS 0..3` maps to depths
  0/¼/½/1 of AMD, applied linearly rather than in dB.
- **One LFO per voice.** The original has a single LFO shared by every
  voice. Here each `sub` instance runs its own: with LFO sync on (it resets
  on each key) this is the same, but a free-running LFO has independent
  phases per voice. LFO pitch modulation also doesn't reach fixed-frequency
  operators (the pitch EG does).
- **Rate scaling is continuous**, rather than the original's 3-semitone steps.
  Envelope times can differ by up to about 10% between the steps.
- **Osc key sync** resets phases only when the voice is silent, so a
  retrigger of a ringing voice carries its phases (see `fm`'s `sync`).
- **Velocity** is interpolated between the table's even-velocity steps.
- **Not in voice data**: pitch-bend range, mod wheel / aftertouch routing,
  portamento and mono/poly mode are global function settings on the
  original, not part of a voice. Set them in your own patch (`play.patch`
  uses `midi { bend: 2 }`).

The envelope, scaling, velocity, pitch-EG and LFO constants follow the
reverse-engineered model in Google's Music Synthesizer for Android
(Apache-2.0). Provenance and the license notice are in
`docs/attribution.md` and `THIRD_PARTY_NOTICES.md`.

## Tests

`tools/test_syx2patch.py` (run by `ctest` as `syx2patch`) synthesizes seeded
random banks covering all 32 algorithms and every LFO wave. It round-trips
the packed format and checks the level, rate, frequency, feedback and
keyboard-scaling mappings. With the `ratsnest` binary, it also converts a
whole bank and runs `ratsnest check` on every file and `render` on a sample of them.
