# Your first tracker song

Write one bar with two A3 notes. The first is quiet, the second accented.
The score (`.trk`) says when the notes play. The patch (`.patch`) supplies
the clock and makes the sound.

After the [quickstart build](../README.md#prerequisites), run the commands
below from the repository root. Create a directory for the two files.

```sh
mkdir -p one-bar
```

Save this as `one-bar/one-bar.trk`.

```text
track lead note vel
pattern bar 16 lead
00 | A3 .25
02 | .
04 | off
08 | A3:4 .90
order bar
```

The pattern has 16 rows. Row 0 starts A3 at velocity 0.25. The dot on row 2
holds the note, just like an unwritten row. To make a rest, row 4 closes
the gate with `off`. Pitch and velocity hold. Row 8 plays A3 again at 0.90.
The `:4` gives it a length of four rows, so its gate closes on row 12.

Save this as `one-bar/one-bar.patch`. The tracker finds its score relative
to the file that declares it.

```text
time = transport { bpm: 120, division: 4 }
score = tracker { file: "one-bar.trk" }
tone = vco { freq: 220, wave: sine }
env = adsr { attack: .002, decay: .02, sustain: 1, release: .025 }
shaped = vca { gain: 0 }
accent = vca { gain: 0 }

time.clock -> score.clock
score.voct1 -> tone.v_oct
score.gate1 -> env.gate
tone -> shaped.in
env -> shaped.cv
shaped -> accent.in
score.vel1 -> accent.cv
accent -> out.in
```

The clock advances four rows per beat. The first amplifier shapes the sine
wave with the envelope. The second multiplies that sound by the track's
velocity. Both use `gain: 0` because the cable adds to the amplifier's
base gain, which defaults to 1.

Pass the **patch** to `check`, `render` or `play`. Checking it also parses
the score and reports score errors at their `.trk` line and column.

```sh
./build/ratsnest check one-bar/one-bar.patch
./build/ratsnest render one-bar/one-bar.patch -d 2.2s --stats 250ms \
  --tap gate=score.gate1 --tap voice=accent -o one-bar.wav
```

Listen to `one-bar.wav` in the repository root. The first note starts at
0 seconds and the louder one at 1 second. A bar at 120 BPM lasts 2 seconds
and this render includes another 0.2 seconds.

If the mix is silent, read the two tap tables. The `gate` tap should be
high for the first half of each second. The `voice` tap should peak near
−12 dBFS on the first note and −1 dBFS on the second. Its WAV,
`one-bar.voice.wav`, lets you listen at the final amplifier. A bare
`--tap score` reads `score.voct1`, the pitch control. A3 is exactly zero
on that output even while the oscillator is sounding at 220 Hz.

For more tracks, patterns and order lists, see the [tracker reference](modules.md#tracker).
