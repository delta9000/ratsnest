# Euclidean Rhythm Generator — Design

## Goal

Add the Phase 3 `euclid` module: evenly distribute a chosen number of hits
across a cycle and expose both one-sample triggers and clock-width gates.

## Interface

`euclid` has inputs `clock` (0), `reset` (0), `steps` (16), `hits` (4), and
`rot` (0), plus outputs `trig` and `gate`. Parameters are live audio-rate
inputs so cables can modulate them. `steps` rounds and clamps to 1..64;
`hits` rounds and clamps to 0..steps; rotation rounds and wraps by steps.

On a clock rising edge the module evaluates the current step. A hit produces
`trig = 1` for exactly that sample. `gate` follows the incoming clock level for
the full high phase when the current step is a hit. Reset clears both outputs
and makes the next coincident or subsequent rising clock edge step zero.

## Pattern

Use the constant-time bucket test
`((step + rotation) * hits) % steps < hits`. This produces canonical evenly
spaced Euclidean rhythms without allocation or a stored pattern. With
`steps=8`, `hits=3`, and `rot=0`, hit steps are 0, 3, and 6. Zero hits stays
silent; hits equal to steps fires every step.

## State and Verification

Runtime state is the current step, whether its gate is active, and prior clock
and reset levels. All state migrates across hot reload. Tests cover registration,
known patterns, trigger/gate widths, bounds and rotation, coincident reset and
clock behavior, live parameter changes, and load-bearing state migration. The
module is added last in registry/header order and demonstrated in the dub-techno
patch by replacing its straight hat clock with a rotated Euclidean rhythm.
