# The deck

Draw one. Apply it literally before you decide whether it was a good idea.
If it felt comfortable, draw a second and apply both.

## Structure

1. Delete the arrangement grid. Whatever change survives is the piece.
2. Make the intro the outro. Build the patch backwards from silence.
3. The breakdown is the track. Everything else is context for it.
4. Give one section a different number of bars than the others. Six, not eight.
5. Nothing enters after the first thirty seconds. Only things leave.
6. Two pieces at once: an eight-bar loop and a nine-minute arc, no relation.
7. The piece is a single crossfade between two patches that share a clock.
8. Put a bar of silence somewhere you'd never dare.
9. Start at the drop. Earn it retroactively.
10. Let the grid wrap on purpose. The intro coming back is the form.

## Time

11. Every rhythmic element on a coprime `euclid`: 5, 7, 11, 13.
12. Halve the tempo and double every envelope. Same notes, different music.
13. One element is not clocked at all. Free-running, ignoring everything.
14. Derive the clock from a `follow` on another voice.
15. Two clocks at 119 and 120 BPM. Let them walk apart for ten minutes.
16. Use `logic { op: xor }` of two divs as the only rhythm source.
17. Swing by putting a `func` slew on the clock, not on the notes.
18. The kick is the only thing on the grid. Everything else is drift.
19. `turing.bit1..bit8` supply *all* the gates. One genome, whole rhythm section.
20. Reverse the roles: the hats are the melody, the lead is the rhythm.

## Pitch

21. Five notes. Not six. Shrink the `quant` scale until it hurts.
22. Just intonation only: 0.58496, 0.32193, 0.80735. Let it lock.
23. One chord for the whole piece. The timbre does the harmony.
24. Detune by 1 Hz, not by cents. Compose the beat rate as a tempo.
25. Take the melody out of `switch { mode: seq }` stepping through chord lanes.
26. Transpose the whole patch by a bar-clocked `quant.root`. Nothing else changes.
27. Put the bass an octave *above* the pad.
28. A pentatonic phrase that never changes, over chords that never stop changing.
29. `stepseq` for pitch at 5 steps, `stepseq` for gates at 7. Let them phase.
30. Whole tone. No gravity. See what the ear does with it.

## Timbre

31. No `vco`. Only `modal`, `string`, `noise`, `grains`.
32. One `vco`. Everything else in the patch is a filter or a space.
33. Modulate `fm.lvl3` instead of a filter cutoff. Timbre as automation.
34. `modal { ratios: [...] }` with numbers you made up. Listen before judging.
35. `fold` under an LFO instead of a filter envelope. Brightness from amplitude.
36. Excite the resonator with the *other* voice, not with noise.
37. `string { damping: 0.97 }` — the pluck that never stops. Now it's a drone.
38. Ring-modulate two parts of the patch that have no business meeting.
39. Everything through one `overdrive { model: muff }`. Commit.
40. `grains { size: 0.008, jitter: 1 }` on your cleanest element.

## Space

41. The delay is the instrument. The dry signal is at 0.15.
42. Feed the reverb back into the delay and the delay back into the reverb.
43. Automate `reverb.decay`, not the send level.
44. Freeze something. Never unfreeze it.
45. Two delays at 3/8 and 1/4. Let the echoes tangle.
46. Everything mono until the last two modules.
47. Pan by frequency: low centre, mid narrow, air hard left and right.
48. Put the send *before* the filter so the space is brighter than the source.
49. `delay { time: 0.003, feedback: 0.8 }` — a resonator, not an echo.
50. The only reverb is the room of one `modal` with high `damping`.

## Process

51. Five modules. Not six.
52. Every input that can be modulated must be modulated.
53. No cable may have a depth of exactly 1.
54. Use a module you've never used. Build the patch around it.
55. Change one seed. Render both. Keep the one you didn't expect.
56. Copy a patch you like, delete half of it, finish it differently.
57. Write the `--stats` timeline you want first, then patch until it matches.
58. Set every envelope to the wrong shape (slow attack on the drum, snap on the pad).
59. Take the best-sounding element out.
60. Render ten minutes and use the last three.

## When genuinely stuck

61. The problem is upstream of where you're looking. Go one module earlier.
62. You are adding when you should be subtracting. Mute something.
63. The patch is fine; the render is too short. Try six minutes.
64. You have modulated levels but not timbres. Nothing is *becoming* anything.
65. Nothing is broken. Break one thing.
66. Say out loud what the piece is about. If you can't, that's the bug.
