---
name: ratsnest-inspiration
description: Use when starting a ratsnest patch with no brief, when the request is a mood or an image rather than a genre, when asked to "surprise me" or "make something", or when a patch is technically fine and creatively dead and needs a way out of the rut.
---

# Starting from nothing

Most patches fail before the first cable, because the author started with
modules instead of with a claim. This skill is about the claim.

## Write the sentence first

Before any module, write one sentence in the patch header that could be wrong.

> *A bell rings in a room that is slowly filling with water.*
>
> *Two machines agree on the tempo and on nothing else.*
>
> *The same eight notes, but the room keeps changing size.*
>
> *A rhythm that is trying to remember a rhythm.*

Then the whole patch is a test of that sentence, and every decision has an
answer. "Should the reverb decay be automated?" — well, is the room filling with
water? Yes. Automate it.

A sentence that can't be wrong ("a nice ambient track") gives you nothing.

## Three ways in

**From a constraint.** Pick one and obey it absolutely:

- One oscillator. Everything else is filters, resonators and space.
- No `vco` at all — only `modal`, `string`, `noise` and `grains`.
- No arrangement lanes. Change must emerge or not happen.
- Five modules. Not six.
- Everything derives from one `turing` register: pitch, rhythm, timbre, level.
- One chord, nine minutes.
- Every parameter that *can* be modulated *must* be.
- The whole patch is mono until the last two modules.

**From a mechanism.** Pick something in the palette you don't understand yet —
`modal { ratios: ... }`, `grains.reverse`, `logic { op: xor }`, `string`'s
dispersion above 0.9, `alloc` + `voice` groups, `follow` driving something that
isn't a duck — and build the patch that exists to show it off.

**From a sound you can describe but not name.** Write down five adjectives, then
map each to a parameter, not a module. "Brittle" is high `brightness` and low
`damping`. "Swollen" is `fold` under a slow LFO. "Distant" is a long predelay
and a high-passed dry. Adjectives are automation targets.

## The deck

`references/provocations.md` is a deck of ratsnest-specific provocations —
Oblique Strategies for a text-file synth. Draw one when stuck. Draw two and
apply both if the first felt too comfortable.

`references/artist-lenses.md` builds patches from the documented working methods
of Radigue, Eno, Ciani, Barbieri, Cortini, Roach, Davachi, Basic Channel,
Boards of Canada and others — not "sound like X" but "work the way X worked",
which is the part that transfers.

`references/seed-sketches.md` is twenty short compositions: a title, a sentence,
a feeling, and the six lines of skeleton that get you there. Steal one whole.

## Rules for keeping it alive

**Follow the accident.** When a patch does something you didn't intend and it's
better, the correct move is to delete the thing you intended. Note it in the
header comment so it survives the next edit.

**One thing changes slowly.** If you can't name the single parameter that is
different at 4:00 than at 0:30, the patch isn't finished. Nominate it, name it
in the comment, and give it the widest range you dare.

**Leave one thing broken.** A detuned voice, an unquantized note, a delay that's
slightly off the grid, a hat that drops one in seven. The ear finds the flaw and
decides a person made this.

**Subtract before you add.** When it isn't working, the answer is almost never a
new voice. Mute one element and render again. Do it three times before adding
anything.

**Render longer than you think.** Feedback nets bloom, free-ringing voices
accumulate, glacial LFOs haven't done anything yet at 30 seconds. Render six
minutes before deciding a generative patch is boring.

**Make the second one.** A patch is a template. Change the seed, change the
scale, change the clock, keep everything else — you get a sibling in ten
minutes, and siblings are how a body of work happens.

## What to do with a finished patch

Give it a name that is the sentence, not the technique. `permafrost`, not
`freeze_relay_test`. `fathom`, not `dark_ambient_2`. The name is a promise about
the sound, and the next person to open the file — you, in a month — needs it.
