# Twenty seeds

Each is a sentence, a feeling, and the handful of lines that get you there.
None is finished. Steal one whole, or take the sentence and ignore the rest.

Five of them are realized as complete, verified patches in `patches/etudes/` —
those are marked. The rest are yours.

---

### 1. Incommensurate *(realized: `patches/etudes/incommensurate.patch`)*
*Nothing in this patch shares a period with anything else.*
Five glacial LFOs at 53 / 88 / 141 / 233 / 303 s, each on a different
destination, over a just-tuned drone and a coin-gated bell. No grid.

### 2. Battement *(realized: `patches/etudes/battement.patch`)*
*Three intervals, six oscillators, no triggers at all.*
Detune each pair so its beat is a chosen tempo — 11 s, 5 s, 3 s. One
24-minute LFO nudges a single detune so no pulse is ever quite itself.

### 3. Locked loop *(realized: `patches/etudes/locked-loop.patch`)*
*One genome: pitch, rhythm and timbre from a single shift register.*
`turing` with `prob` under a 233-second LFO. The riff locks for minutes,
dissolves, re-forms as a cousin.

### 4. Coprime *(realized: `patches/etudes/coprime.patch`)*
*Five, seven, eleven, thirteen.*
Euclidean layers whose periods don't divide each other, and a 5-step pitch
sequence against a 7-step gate sequence.

### 5. Feedback garden *(realized: `patches/etudes/feedback-garden.patch`)*
*The dry signal is a rumour.*
Stab → saturation → filtered delay → plate → octave-up granular haze → back
into the delay. Every effect has a path home.

---

### 6. The room is filling with water
*A bell rings in a room that is slowly filling.*
`modal` bells struck sparsely; `reverb.decay` and `damp` both on a nine-minute
ramp, and a `svf.lp` cutoff falling from 8 kHz to 400 across the same arc. The
bells never change. The room swallows them.

```text
flood = lfo { wave: saw, freq: 0.0018hz }
flood -> hall.decay : 0.15
flood -> murk.cutoff : -3400
```

### 7. Two machines
*They agree on the tempo and on nothing else.*
One clock, two `div` chains, two voices, two `euclid`s at 9 and 16. Pan them
hard apart. Give one a bright `string`, the other a dark `modal`. Never let a
cable cross between them except the clock.

### 8. Remembering a rhythm
*A pattern that is trying to recall a pattern.*
`turing { prob: 0.25 }` driving gates, with `write` pulsed occasionally from a
`bern`. It approaches a groove, loses it, approaches a different one.

### 9. Eight notes, changing room
*The same eight notes, but the room keeps changing size.*
A fixed `stepseq` phrase you never edit. Everything else is `reverb.predelay`,
`decay`, `damp` and delay `time` under multi-minute ramps.

### 10. Tuning fork in a stairwell
*One pitch, examined.*
A single sine at 220 Hz into a 9 ms `delay` at 0.93 feedback. Sweep the delay
time by ±2 ms over four minutes. The comb becomes a chord.

```text
comb = delay { time: 0.009, feedback: 0.93, mix: 1, tone: 6000, slew: 0.001 }
sweep = lfo { wave: sine, freq: 0.004hz }
sweep -> comb.time : 0.002
```

### 11. Rain on a tin roof, badly remembered
*Percussion that isn't quite percussion.*
Pink `noise` → `vca` gated by a dense `bern` at `prob: 0.06` on a sixteenth
clock, into a `modal` with `structure: 0.1` (drum-like, compressed partials)
and short `damping`. Each drop rings the roof.

### 12. Cathedral of one chord
*Nine minutes, one chord, and you'll swear it modulates.*
Six `vco`s on a just major triad plus the 7/4. Introduce and remove one voice
at a time across the piece. The harmony never changes; the *chord* changes
constantly.

### 13. The tape is dying
*Something recorded once and played too many times.*
Any patch, then: a slow `random` on a short `delay.time` (wow), a fast `lfo` at
tiny depth on the same input (flutter), pink noise at −45 dB, `svf.lp` at
6 kHz, and `overdrive { model: clean, drive: 0.25, mix: 0.4 }`.

### 14. Sixteen bars, then never again
*The loop plays once. Everything after is its ghost.*
Record a groove into a `grains` buffer for the first 16 bars, freeze it
permanently, and mute the source. The rest of the piece is `position`,
`size` and `pitch` on the frozen buffer.

### 15. A machine that is trying to be gentle
*Motorik, but the drums are made of glass.*
The Apache beat — kick on every eighth, offbeat as `eighth AND NOT quarter` —
but every voice is a `modal` bank with high `structure` and long `damping`.
The rhythm is exact. The timbre is fragile.

### 16. Descent
*It only ever goes down.*
Every modulation source is a `saw` `lfo` with a negative cable depth. Pitch,
cutoff, density, tempo (via `clock.bpm`), reverb size. Twelve minutes of
falling that never arrives.

### 17. Call and no response
*Two voices; only one of them is listening.*
`bern` splits a clock: `a` strikes a bright pluck, `b` strikes a dark one. Put
a `follow` on the bright voice driving the dark voice's `damping`, but not the
reverse. One of them is being affected by a conversation it isn't part of.

### 18. Acid at half speed
*The squelch, but nobody is dancing.*
The full acid voice — resonant `ladder`, env squelch, accent on both level and
env depth — at 60 BPM, with a four-minute `func` slew on every pitch change so
nothing ever arrives cleanly.

### 19. Overheard through a wall
*You can hear that it's a good track. You can't hear the track.*
Build a full club patch, then put the entire master through
`svf { cutoff: 500, res: 0.3 }`, add a 40 ms `delay` at `mix: 0.3`, and drop
everything above 2 kHz. Automate the cutoff up 400 Hz once, at 3:40, as if a
door opened.

### 20. The last one out
*Subtract until there is one thing, then let it go.*
Take a finished eight-element patch. Write an outro lane that removes elements
in the exact reverse of the order they entered, one every eight bars. Leave the
first element alone for a full minute before it goes.

---

## Making one yours

Pick a seed, then apply exactly one card from `provocations.md` to it before
you start. The seed gives you a direction; the provocation stops you from
building the obvious version of it.

When it's done, write the sentence at the top of the file. If the sentence you
end up with isn't the one you started with, that's the patch working.
