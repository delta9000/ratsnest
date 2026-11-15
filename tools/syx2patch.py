#!/usr/bin/env python3
"""Convert classic 6-operator FM SysEx voices (.syx) into ratsnest subpatches.

Stdlib only. Reads the voice data formats of the classic 1983 six-operator
instrument family -- the 32-voice packed bulk dump (4104 bytes, or the bare
4096-byte voice block) and the single-voice edit-buffer dump (163 bytes) --
and writes one `.sub` per voice built on the `fm` module, plus `play.patch`,
a MIDI-keyboard wrapper. Nothing is bundled: bring your own banks.

    python3 tools/syx2patch.py my-bank.syx -o patches/dx/my-bank
    python3 tools/syx2patch.py my-bank.syx -o out --voice 11
    mise run play -- patches/dx/my-bank/play.patch

Every voice parameter is mapped; see docs/fm-syx.md for the mapping, its
fidelity, and the few places that are approximate (LFO amplitude depth,
rate-scaling steps). The behavioural constants (envelope rate and level
curves, keyboard-scaling and velocity tables, pitch-envelope and LFO
tables) follow the reverse-engineered description in Google's Music
Synthesizer for Android (Apache-2.0); they are restated here as data, no
code was copied. Provenance: docs/attribution.md.
"""

import argparse
import math
import pathlib
import re
import sys
from decimal import Decimal

# ---------------------------------------------------------------------------
# Constants from the documented hardware behaviour (docs/attribution.md).

# Output/envelope level 0..19 -> internal attenuation step; 20..99 -> 28+L.
LEVEL_LUT = [0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41,
             42, 43, 45, 46]
# Exponential keyboard-scaling curve, per 3-semitone group.
EXP_SCALE = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39,
             47, 56, 66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238,
             250]
# Velocity response, indexed by velocity >> 1.
VELOCITY = [0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156,
            160, 163, 166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194,
            196, 198, 200, 202, 205, 206, 209, 211, 214, 216, 218, 220, 222,
            224, 225, 227, 229, 230, 232, 233, 235, 237, 238, 240, 241, 242,
            243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254]
# Pitch envelope: rate -> speed units, level -> 1/32-octave units.
PEG_RATE = [1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
            12, 12, 13, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20, 21, 22,
            23, 24, 25, 26, 27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42,
            44, 46, 47, 49, 51, 53, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72,
            74, 76, 79, 82, 85, 88, 91, 94, 98, 102, 106, 110, 115, 120, 125,
            130, 135, 141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211,
            232, 243, 254, 255]
PEG_LEVEL = [-128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46,
             -43, -41, -39, -37, -35, -33, -32, -31, -30, -29, -28, -27, -26,
             -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13,
             -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4,
             5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
             22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 38, 40,
             43, 46, 49, 53, 58, 65, 73, 82, 92, 103, 115, 127]
PMS_TABLE = [0, 10, 20, 33, 55, 92, 153, 255]
# Envelope clocking: one rate unit step per 64 samples at 44.1 kHz.
ENV_BLOCKS_PER_S = 44100.0 / 64.0
DB_SPAN_OCT = 96.0 / 6.020599913279624     # the `t` reference distance
STEP_DB = 6.020599913279624 / 8.0          # one output-level step, 0.75 dB
LFO_UNIT_HZ = 25190424.0 / 2.0 ** 32
AMS_DEPTH = [0.0, 0.25, 0.5, 1.0]          # approximate; see docs/fm-syx.md
REF_NOTE = 60                              # rate scaling neutral here (C4)

# The 32 algorithms as modulator->carrier edges (operator numbers 1..6),
# the feedback operator, and the operator whose output feeds it (the same
# op except algorithms 4 and 6, whose loops span operators).
ALGORITHMS = {
    1: ([(2, 1), (4, 3), (5, 4), (6, 5)], 6, 6),
    2: ([(2, 1), (4, 3), (5, 4), (6, 5)], 2, 2),
    3: ([(2, 1), (3, 2), (5, 4), (6, 5)], 6, 6),
    4: ([(2, 1), (3, 2), (5, 4), (6, 5)], 6, 4),
    5: ([(2, 1), (4, 3), (6, 5)], 6, 6),
    6: ([(2, 1), (4, 3), (6, 5)], 6, 5),
    7: ([(2, 1), (4, 3), (5, 3), (6, 5)], 6, 6),
    8: ([(2, 1), (4, 3), (5, 3), (6, 5)], 4, 4),
    9: ([(2, 1), (4, 3), (5, 3), (6, 5)], 2, 2),
    10: ([(2, 1), (3, 2), (5, 4), (6, 4)], 3, 3),
    11: ([(2, 1), (3, 2), (5, 4), (6, 4)], 6, 6),
    12: ([(2, 1), (4, 3), (5, 3), (6, 3)], 2, 2),
    13: ([(2, 1), (4, 3), (5, 3), (6, 3)], 6, 6),
    14: ([(2, 1), (4, 3), (5, 4), (6, 4)], 6, 6),
    15: ([(2, 1), (4, 3), (5, 4), (6, 4)], 2, 2),
    16: ([(2, 1), (3, 1), (5, 1), (4, 3), (6, 5)], 6, 6),
    17: ([(2, 1), (3, 1), (5, 1), (4, 3), (6, 5)], 2, 2),
    18: ([(2, 1), (3, 1), (4, 1), (5, 4), (6, 5)], 3, 3),
    19: ([(2, 1), (3, 2), (6, 4), (6, 5)], 6, 6),
    20: ([(3, 1), (3, 2), (5, 4), (6, 4)], 3, 3),
    21: ([(3, 1), (3, 2), (6, 4), (6, 5)], 3, 3),
    22: ([(2, 1), (6, 3), (6, 4), (6, 5)], 6, 6),
    23: ([(3, 2), (6, 4), (6, 5)], 6, 6),
    24: ([(6, 3), (6, 4), (6, 5)], 6, 6),
    25: ([(6, 4), (6, 5)], 6, 6),
    26: ([(3, 2), (5, 4), (6, 4)], 6, 6),
    27: ([(3, 2), (5, 4), (6, 4)], 3, 3),
    28: ([(2, 1), (4, 3), (5, 4)], 5, 5),
    29: ([(4, 3), (6, 5)], 6, 6),
    30: ([(4, 3), (5, 4)], 5, 5),
    31: ([(6, 5)], 6, 6),
    32: ([], 6, 6),
}

LFO_WAVES = ["tri", "saw down", "saw up", "square", "sine", "sample & hold"]

# Unpacked (155-byte) voice layout: six 21-byte operator blocks, op 6 first.
OP_FIELDS = ["r1", "r2", "r3", "r4", "l1", "l2", "l3", "l4", "bp", "ld",
             "rd", "lc", "rc", "rs", "ams", "kvs", "ol", "mode", "coarse",
             "fine", "detune"]
VOICE_FIELDS = ["pr1", "pr2", "pr3", "pr4", "pl1", "pl2", "pl3", "pl4",
                "algo", "fb", "sync", "lfo_speed", "lfo_delay", "lfo_pmd",
                "lfo_amd", "lfo_sync", "lfo_wave", "pms", "transpose"]


# ---------------------------------------------------------------------------
# SysEx parsing.

class SyxError(ValueError):
    pass


def unpack_voice(b):
    """128-byte packed bulk voice -> 155-byte unpacked list (+ name)."""
    out = []
    for op in range(6):
        p = b[op * 17:(op + 1) * 17]
        out += list(p[0:11])
        out += [p[11] & 3, (p[11] >> 2) & 3, p[12] & 7, p[13] & 3,
                (p[13] >> 2) & 7, p[14], p[15] & 1, (p[15] >> 1) & 31,
                p[16], (p[12] >> 3) & 15]
    v = b[102:]
    out += list(v[0:9]) + [v[9] & 7, (v[9] >> 3) & 1] + list(v[10:14])
    out += [v[14] & 1, (v[14] >> 1) & 7, (v[14] >> 4) & 7, v[15]]
    out += list(v[16:26])
    return out


def decode_voice(raw):
    """Unpacked list -> dict with ops[0] = operator 1."""
    ops = []
    for i in range(6):
        block = raw[i * 21:(i + 1) * 21]
        ops.append(dict(zip(OP_FIELDS, block)))
    ops.reverse()                         # stored op 6 first
    voice = dict(zip(VOICE_FIELDS, raw[126:145]))
    voice["ops"] = ops
    voice["name"] = bytes(c & 0x7F for c in raw[145:155]).decode(
        "ascii", "replace").rstrip()
    return voice


def checksum_ok(data, cks):
    return ((-sum(data)) & 0x7F) == cks


def messages(blob):
    """Split a .syx blob into F0..F7 messages."""
    out, start = [], None
    for i, c in enumerate(blob):
        if c == 0xF0:
            start = i
        elif c == 0xF7 and start is not None:
            out.append(blob[start:i + 1])
            start = None
    return out


def parse_syx(blob, warn=print):
    """Return the voices in a .syx file (bulk banks and single voices)."""
    if len(blob) == 4096 and blob[0] != 0xF0:          # bare voice block
        return [decode_voice(unpack_voice(blob[i:i + 128]))
                for i in range(0, 4096, 128)]
    voices = []
    for msg in messages(blob):
        if len(msg) < 8 or msg[1] != 0x43:
            continue                                      # not this family
        fmt, data, cks = msg[3], msg[6:-2], msg[-2]
        if fmt == 9 and len(data) == 4096:
            if not checksum_ok(data, cks):
                warn("warning: bulk dump checksum mismatch; converting anyway")
            voices += [decode_voice(unpack_voice(data[i:i + 128]))
                       for i in range(0, 4096, 128)]
        elif fmt == 0 and len(data) == 155:
            if not checksum_ok(data, cks):
                warn("warning: voice dump checksum mismatch; converting anyway")
            voices.append(decode_voice(list(data)))
    if not voices:
        raise SyxError("no 6-operator voice data found (expected a 32-voice "
                       "bulk dump or a single-voice dump)")
    return voices


# ---------------------------------------------------------------------------
# Parameter mapping.

def scale_level(x):
    """Output/envelope level 0..99 -> internal step."""
    x = max(0, min(99, x))
    return x + 28 if x >= 20 else LEVEL_LUT[x]


def env_amp(el, ol):
    """Linear amplitude of envelope level `el` at output level `ol`
    (full scale = 1, the fm module's level-1 operator); 0 below silence."""
    actual = max(16, ((scale_level(el) >> 1) << 6) + (scale_level(ol) << 5)
                 - 4256)
    oct_ = (actual - 3840) / 256.0
    return 0.0 if oct_ <= -14.9 else 2.0 ** oct_


def rate_scale_delta(note, rs):
    x = min(31, max(0, note // 3 - 7))
    return (rs * x) >> 3


def env_seconds(rate, rs):
    """Seconds for a falling segment to cross 96 dB at `rate` (0..99),
    including the rate scaling at REF_NOTE."""
    q = min(63, ((max(0, min(99, rate)) * 41) >> 6)
            + rate_scale_delta(REF_NOTE, rs))
    inc = (4 + (q & 3)) * 2 ** (8 + (q >> 2))           # per 64 samples
    oct_per_s = inc / 2.0 ** 24 * ENV_BLOCKS_PER_S
    return DB_SPAN_OCT / oct_per_s


def scale_curve(group, depth, curve):
    if curve in (0, 3):                                  # -LIN, +LIN
        scale = (group * depth * 329) >> 12
    else:                                                # -EXP, +EXP
        scale = (EXP_SCALE[min(group, len(EXP_SCALE) - 1)] * depth * 329) >> 15
    return -scale if curve < 2 else scale


def key_offset_db(note, op):
    """Keyboard level scaling at MIDI `note`, dB, including the ceiling."""
    offset = note - op["bp"] - 17
    if offset >= 0:
        ks = scale_curve(offset // 3, op["rd"], op["rc"])
    else:
        ks = scale_curve((-offset) // 3, op["ld"], op["lc"])
    base = scale_level(op["ol"])
    total = max(0, min(127, base + ks))
    return (total - base) * STEP_DB


def voct(note):
    return (note - 57) / 12.0                          # 0 = A3, as `midi`


def key_table(op):
    """Flat (v_oct, dB) knots reproducing the per-note steps exactly at
    every integer note; empty when the op has no level scaling."""
    vals = [key_offset_db(n, op) for n in range(128)]
    if not any(vals):
        return []
    knots = [(voct(0), vals[0])]
    for n in range(1, 128):
        if vals[n] != vals[n - 1]:
            knots += [(voct(n - 1), vals[n - 1]), (voct(n), vals[n])]
    knots.append((voct(127), vals[127]))
    dedup = []
    for k in knots:
        if not dedup or dedup[-1] != k:
            dedup.append(k)
    return [x for k in dedup for x in k]


def velocity_curve():
    """dB per unit of `vel_depth` (sensitivity 8) at the 64 table steps."""
    pts = []
    for j, t in enumerate(VELOCITY):
        pts += [2 * j / 127.0, (t - 239) / 16.0 * 6.020599913279624]
    pts += [1.0, pts[-1]]
    return pts


def op_frequency(op):
    """('ratio', r) or ('fixed', hz)."""
    det = op["detune"] - 7
    if op["mode"] == 0:
        coarse = 0.5 if op["coarse"] == 0 else float(op["coarse"])
        ratio = coarse * (1.0 + op["fine"] / 100.0)
        ratio *= 2.0 ** (det * 12606 / 2.0 ** 24)
        return "ratio", ratio
    hz = 10.0 ** ((op["coarse"] & 3) + op["fine"] / 100.0)
    if det > 0:
        hz *= 2.0 ** (det * 13457 / 2.0 ** 24)
    return "fixed", hz


def feedback_amount(fb):
    """fm `fb` reproducing a deviation of 2*pi*2^(fb-7) * output (rad)."""
    return 0.0 if fb == 0 else 2.0 * math.pi * 2.0 ** (fb - 7) / 2.5


def lfo_hz(speed):
    sr = 1 if speed == 0 else (165 * speed) >> 6
    sr *= 11 if sr < 160 else 11 + ((sr - 160) >> 4)
    return LFO_UNIT_HZ * sr


def lfo_delay(delay):
    """(hold seconds, ramp seconds) before the LFO reaches full depth."""
    if delay == 0:
        return 0.0, 0.0
    a = 99 - delay
    a = (16 + (a & 15)) << (1 + (a >> 4))
    a2 = max(0x80, a & 0xFF80)
    per_s = 25190424.0
    return 2.0 ** 31 / (per_s * a), 2.0 ** 31 / (per_s * a2)


# ---------------------------------------------------------------------------
# Patch text.

def num(x):
    """Fixed-point decimal (the DSL has no exponent syntax), 6 significant."""
    if x == 0:
        return "0"
    d = Decimal(f"{x:.6g}")
    s = format(d, "f")
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    return s


def arr(xs, per_line=0, indent=""):
    items = [num(x) for x in xs]
    if not per_line or len(items) <= per_line:
        return "[" + ", ".join(items) + "]"
    lines = [", ".join(items[i:i + per_line])
             for i in range(0, len(items), per_line)]
    return "[" + (",\n" + indent).join(lines) + "]"


def slug(name, index):
    s = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") or "voice"
    return f"{index:02d}-{s}"


def fm_block(v):
    """The `fm { ... }` config for one voice, as text lines."""
    ops = v["ops"]
    edges, fb_op, fb_src = ALGORITHMS[v["algo"] + 1]
    freqs = [op_frequency(op) for op in ops]
    ratios = [f if kind == "ratio" else 1.0 for kind, f in freqs]
    fixed = [f if kind == "fixed" else 0.0 for kind, f in freqs]
    fb = [0.0] * 6
    fb_from = [0] * 6
    fb[fb_op - 1] = feedback_amount(v["fb"])
    if fb_src != fb_op:
        fb_from[fb_op - 1] = fb_src
    lines = [
        "algo: custom",
        "route: " + arr([x for e in edges for x in e]) if edges else None,
        "curve: db",
        "sync: " + ("on" if v["sync"] else "off"),
        "freq: " + num(220.0 * 2.0 ** ((v["transpose"] - 24) / 12.0)),
        "levels: [1, 1, 1, 1, 1, 1]",
        "ratios: " + arr(ratios),
        "fixed: " + arr(fixed) if any(fixed) else None,
        "fb: " + arr(fb) if any(fb) else None,
        "fb_from: " + arr(fb_from) if any(fb_from) else None,
    ]
    for s in range(4):
        lines.append(f"t{s + 1}: " + arr(
            [env_seconds(op[f"r{s + 1}"], op["rs"]) for op in ops]))
    for s in range(4):
        lines.append(f"l{s + 1}: " + arr(
            [env_amp(op[f"l{s + 1}"], op["ol"]) for op in ops]))
    if any(op["rs"] for op in ops):
        lines.append("key_break: " + num(voct(REF_NOTE)))
        lines.append("key_rate: " + arr([op["rs"] / 8.0 for op in ops]))
    for k, op in enumerate(ops):
        table = key_table(op)
        if table:
            lines.append(f"keys{k + 1}: " + arr(table, 8, "        "))
    if any(op["kvs"] for op in ops):
        lines.append("vel_curve: " + arr(velocity_curve(), 8, "        "))
        lines.append("vel_depth: " + arr([op["kvs"] / 8.0 for op in ops]))
    peg = [PEG_LEVEL[v[f"pl{s + 1}"]] / 32.0 for s in range(4)]
    if any(peg):
        lines.append("pitch_l: " + arr(peg))
        lines.append("pitch_t: " + arr(
            [21.3 / PEG_RATE[v[f"pr{s + 1}"]] for s in range(4)]))
    return [ln for ln in lines if ln]


def lfo_section(v):
    """LFO patch lines (empty when the voice has no LFO depth)."""
    ops = v["ops"]
    pitch = ((v["lfo_pmd"] * 165) >> 6) * PMS_TABLE[v["pms"]] / 65536.0
    amp = [v["lfo_amd"] / 99.0 * AMS_DEPTH[op["ams"]] for op in ops]
    if pitch == 0 and not any(amp):
        return []
    wave = v["lfo_wave"] if v["lfo_wave"] < 6 else 0
    rl = ["", f"# LFO: {LFO_WAVES[wave]}, speed {v['lfo_speed']} "
              f"({num(lfo_hz(v['lfo_speed']))} Hz), delay {v['lfo_delay']}, "
              f"PMD {v['lfo_pmd']} x PMS {v['pms']}, AMD {v['lfo_amd']}."]
    base = {0: "tri", 1: "saw", 2: "saw", 3: "square", 4: "sine", 5: "square"}[wave]
    phase = 0.5 if wave == 4 else 0
    rl.append(f"lfo = lfo {{ freq: {num(lfo_hz(v['lfo_speed']))}, "
              f"wave: {base}, phase: {num(phase)} }}")
    if v["lfo_sync"]:
        rl.append("gate -> lfo.reset")
    src, sign = "lfo", -1.0 if wave == 1 else 1.0
    if wave == 5:                                       # sample & hold
        rl += ["lfo_rnd = noise { seed: 7 }", "lfo_sh = sh {}",
               "lfo_rnd -> lfo_sh.in", "lfo -> lfo_sh.trig"]
        src = "lfo_sh"
    hold, ramp = lfo_delay(v["lfo_delay"])
    if hold + ramp > 0:
        k = (hold + ramp) / ramp
        rl += [f"lfo_env = adsr {{ attack: {num(hold + ramp)}, decay: 0, "
               "sustain: 1, release: 600, curve: lin }",
               "gate -> lfo_env.gate",
               "lfo_dly = math { op: max, b: 0 }",
               f"lfo_env -> lfo_dly.a : {num(k)}",
               f"{num(1.0 - k)} -> lfo_dly.a"]
        ramp_src = "lfo_dly"
    else:
        ramp_src = None
    if pitch:
        rl.append("lfo_pm = vca { gain: 0 }" if ramp_src else
                  "lfo_pm = vca { gain: 1 }")
        rl.append(f"{src} -> lfo_pm.in : {num(sign)}")
        if ramp_src:
            rl.append(f"{ramp_src} -> lfo_pm.cv")
        rl.append(f"lfo_pm -> op.v_oct : {num(pitch)}")
    if any(amp):
        rl.append("lfo_am = vca { gain: 0 }" if ramp_src else
                  "lfo_am = vca { gain: 1 }")
        rl += [f"{src} -> lfo_am.in : {num(sign)}", "1 -> lfo_am.in"]
        if ramp_src:
            rl.append(f"{ramp_src} -> lfo_am.cv")
        for k, a in enumerate(amp):
            if a:
                rl.append(f"lfo_am -> op.lvl{k + 1} : {num(-0.5 * a)}")
    return rl


def voice_sub(v, source, index):
    name = v["name"] or f"voice {index}"
    edges, fb_op, fb_src = ALGORITHMS[v["algo"] + 1]
    body = fm_block(v)
    head = [
        f"# {name} -- 6-op FM voice {index} of {source}, converted by",
        "# tools/syx2patch.py (docs/fm-syx.md). Regenerate rather than edit.",
        f"# Algorithm {v['algo'] + 1}, feedback {v['fb']} on op {fb_op}"
        + (f" from op {fb_src}" if fb_src != fb_op else "")
        + f", transpose {v['transpose'] - 24:+d} semitones.",
        "#",
        "#   v = sub { file: \"" + slug(name, index) + ".sub\" }",
        "#   mid.voct1 -> v.voct    mid.gate1 -> v.gate    mid.vel1 -> v.vel",
        "#",
        "# Inputs `voct` (v/oct, 0 = A3), `gate`, `vel` (0..1, latched at the",
        "# gate). Output `main`: the carriers' straight sum x `level`.",
        "",
        "level = param { default: 0.25 }",
        "voct = input {}",
        "gate = input {}",
        "vel = input { default: 1 }",
        "main = output {}",
        "",
        "op = fm { " + (",\n         ".join(body)) + " }",
        "voct -> op.v_oct",
        "gate -> op.gate",
        "vel -> op.vel",
        "op -> main.in : level",
    ]
    demo = [
        "",
        "# Audition: mise run play -- <this file>",
        "demo.clk = clock { bpm: 100 }",
        "demo.seq = stepseq { steps: [C3v, G3v, C4v, E4v, G4v, C5v, G4v, E4v] }",
        "demo.clk -> demo.seq.clock",
        "demo.seq.cv -> voct",
        "demo.seq.gate -> gate",
        "main -> demo.out.in",
    ]
    return "\n".join(head + lfo_section(v) + demo) + "\n"


def play_patch(first):
    return f"""# play -- a MIDI keyboard on one converted voice (8-voice polyphony,
# pitch bend +-2). Point `file:` at any voice in this folder.
#   mise run play -- <this folder>/play.patch
mid = midi {{ voices: 8, bend: 2 }}
keys = voice {{ notes: mid }}
keys.v = sub {{ file: "{first}" }}
mid.voct -> keys.v.voct
mid.gate -> keys.v.gate
mid.vel -> keys.v.vel
keys.v -> out.in : 0.5
"""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("syx", type=pathlib.Path)
    ap.add_argument("-o", "--out", type=pathlib.Path, required=True,
                    help="output folder (created)")
    ap.add_argument("--voice", type=int, help="convert only voice N (1-based)")
    args = ap.parse_args(argv)
    try:
        voices = parse_syx(args.syx.read_bytes(),
                           warn=lambda m: print(m, file=sys.stderr))
    except (OSError, SyxError) as e:
        print(f"syx2patch: {e}", file=sys.stderr)
        return 1
    picks = list(enumerate(voices, 1))
    if args.voice:
        if not 1 <= args.voice <= len(voices):
            print(f"syx2patch: --voice must be 1..{len(voices)}", file=sys.stderr)
            return 1
        picks = [picks[args.voice - 1]]
    args.out.mkdir(parents=True, exist_ok=True)
    names = []
    for i, v in picks:
        fname = slug(v["name"] or f"voice {i}", i) + ".sub"
        (args.out / fname).write_text(voice_sub(v, args.syx.name, i))
        names.append(fname)
        print(f"wrote {args.out / fname}")
    (args.out / "play.patch").write_text(play_patch(names[0]))
    print(f"wrote {args.out / 'play.patch'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
