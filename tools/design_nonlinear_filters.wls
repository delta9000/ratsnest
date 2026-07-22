(* Reproduce the clean nonlinear oversampling filters. Wolfram Language 14.x. *)
ClearAll[design, sos, roundTripImpulse];

design[wp0_, ws0_, stopDb_] := Module[{s, wp, ws, analog},
  wp = N[wp0, 60];
  ws = N[ws0, 60];
  analog = EllipticFilterModel[
    {"Lowpass", {2 Tan[wp Pi/2], 2 Tan[ws Pi/2]},
     {N[1/1000, 60], stopDb}}, s];
  ToDiscreteTimeModel[analog, 1, Method -> "BilinearTransform"]
];

(* Pair conjugates in ascending angle and normalize every section to unity DC. *)
sos[filter_] := Module[{zeros, poles},
  zeros = SortBy[Select[Flatten@TransferFunctionZeros[filter], Im[#] > 0 &], Arg];
  poles = SortBy[Select[Flatten@TransferFunctionPoles[filter], Im[#] > 0 &], Arg];
  MapThread[Function[{zero, pole}, Module[{b1, b2, a1, a2, gain},
    b1 = -2 Re[zero]; b2 = Abs[zero]^2;
    a1 = -2 Re[pole]; a2 = Abs[pole]^2;
    gain = (1 + a1 + a2)/(1 + b1 + b2);
    N[{gain, gain b1, gain b2, a1, a2}, 17]
  ]], {zeros, poles}]
];

roundTripImpulse[filter_, rate_] := Module[{input, up, down, machine = N[filter]},
  input = N@PadRight[{1}, rate 256];
  up = RecurrenceFilter[machine, rate input];
  down = RecurrenceFilter[machine, up][[1 ;; ;; rate]];
  {First@Ordering[Abs[down], -1] - 1, Max[Abs[down]]}
];

filter8x = design[12/100, 145/1000, 80];
<|
  "8xSOS" -> sos[filter8x],
  "8xImpulse" -> roundTripImpulse[filter8x, 8]
|>
