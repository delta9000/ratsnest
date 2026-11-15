#!/usr/bin/env python3
"""Tests for tools/syx2patch.py. Stdlib only; run by ctest.

    python3 tools/test_syx2patch.py [path/to/ratsnest]

No voice data ships with the repo: every bank here is synthesized from
seeded random parameters, packed into the bulk-dump format, converted, and
(when a ratsnest binary is given) checked and rendered.
"""

import pathlib
import random
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import syx2patch as sp  # noqa: E402

RATSNEST = None

RANGES = {"r1": 99, "r2": 99, "r3": 99, "r4": 99, "l1": 99, "l2": 99,
          "l3": 99, "l4": 99, "bp": 99, "ld": 99, "rd": 99, "lc": 3, "rc": 3,
          "rs": 7, "ams": 3, "kvs": 7, "ol": 99, "mode": 1, "coarse": 31,
          "fine": 99, "detune": 14}
VRANGES = {"pr1": 99, "pr2": 99, "pr3": 99, "pr4": 99, "pl1": 99, "pl2": 99,
           "pl3": 99, "pl4": 99, "algo": 31, "fb": 7, "sync": 1,
           "lfo_speed": 99, "lfo_delay": 99, "lfo_pmd": 99, "lfo_amd": 99,
           "lfo_sync": 1, "lfo_wave": 5, "pms": 7, "transpose": 48}


def random_unpacked(rng, algo, name):
    raw = []
    for _ in range(6):                      # op 6 first
        raw += [rng.randint(0, RANGES[f]) for f in sp.OP_FIELDS]
    v = [rng.randint(0, VRANGES[f]) for f in sp.VOICE_FIELDS]
    v[sp.VOICE_FIELDS.index("algo")] = algo
    v[sp.VOICE_FIELDS.index("lfo_wave")] = algo % 6
    return raw + v + list(name.ljust(10)[:10].encode())


def pack(u):
    """Unpacked 155 -> packed 128 (the inverse of sp.unpack_voice)."""
    out = []
    for op in range(6):
        p = u[op * 21:(op + 1) * 21]
        out += p[0:11]
        out += [p[11] | (p[12] << 2), p[13] | (p[20] << 3),
                p[14] | (p[15] << 2), p[16], p[17] | (p[18] << 1), p[19]]
    v = u[126:]
    out += v[0:9] + [v[9] | (v[10] << 3)] + v[11:15]
    out += [v[15] | (v[16] << 1) | (v[17] << 4), v[18]] + v[19:29]
    assert len(out) == 128
    return out


def bulk(voices):
    data = [b for u in voices for b in pack(u)]
    return bytes([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00] + data
                 + [(-sum(data)) & 0x7F, 0xF7])


def single(u):
    return bytes([0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B] + u
                 + [(-sum(u)) & 0x7F, 0xF7])


class Mapping(unittest.TestCase):
    def test_pack_round_trip(self):
        rng = random.Random(1)
        for algo in range(32):
            u = random_unpacked(rng, algo, f"RT{algo}")
            self.assertEqual(sp.unpack_voice(pack(u)), u)

    def test_bulk_and_single_parse(self):
        rng = random.Random(2)
        us = [random_unpacked(rng, a, f"V{a}") for a in range(32)]
        voices = sp.parse_syx(bulk(us))
        self.assertEqual(len(voices), 32)
        self.assertEqual(voices[5]["name"], "V5")
        self.assertEqual(voices[5]["algo"], 5)
        one = sp.parse_syx(single(us[7]))
        self.assertEqual(one[0]["ops"], voices[7]["ops"])   # op 1 first
        self.assertEqual(one[0]["ops"][0], dict(zip(sp.OP_FIELDS, us[7][105:126])))
        bare = sp.parse_syx(bulk(us)[6:-2])
        self.assertEqual(bare[31]["name"], "V31")

    def test_bad_data_is_an_error(self):
        with self.assertRaises(sp.SyxError):
            sp.parse_syx(b"\xf0\x41\x10\x00\xf7")

    def test_algorithms_acyclic_and_feedback_ops_valid(self):
        for n, (edges, fb_op, fb_src) in sp.ALGORITHMS.items():
            self.assertTrue(all(a != b for a, b in edges), n)
            self.assertIn(fb_op, range(1, 7))
            self.assertEqual(fb_src != fb_op, n in (4, 6), n)

    def test_levels(self):
        self.assertAlmostEqual(sp.env_amp(99, 99), 1.0)
        # each output-level step is 0.75 dB (6 dB per 8 steps) above 20
        self.assertAlmostEqual(sp.env_amp(99, 91), 0.5)
        self.assertEqual(sp.env_amp(0, 0), 0.0)

    def test_envelope_rate_semantics(self):
        self.assertLess(sp.env_seconds(99, 0), 0.01)       # near-instant
        self.assertGreater(sp.env_seconds(0, 0), 60.0)     # very slow
        # rate scaling at the reference note only ever speeds a rate up
        self.assertLessEqual(sp.env_seconds(50, 7), sp.env_seconds(50, 0))

    def test_frequencies(self):
        op = dict(mode=0, coarse=0, fine=0, detune=7)
        self.assertEqual(sp.op_frequency(op), ("ratio", 0.5))
        op = dict(mode=0, coarse=2, fine=50, detune=7)
        self.assertEqual(sp.op_frequency(op), ("ratio", 3.0))
        kind, hz = sp.op_frequency(dict(mode=1, coarse=2, fine=0, detune=7))
        self.assertEqual(kind, "fixed")
        self.assertAlmostEqual(hz, 100.0)

    def test_key_table_hits_every_note(self):
        op = dict(bp=39, ld=40, rd=99, lc=1, rc=0, ol=90)
        table = sp.key_table(op)
        pts = list(zip(table[0::2], table[1::2]))
        for n in range(128):
            x = sp.voct(n)
            self.assertAlmostEqual(interp(pts, x), sp.key_offset_db(n, op),
                                   places=6, msg=n)
        flat = dict(bp=39, ld=0, rd=0, lc=0, rc=0, ol=90)
        self.assertEqual(sp.key_table(flat), [])

    def test_feedback_full_scale(self):
        self.assertEqual(sp.feedback_amount(0), 0.0)
        self.assertAlmostEqual(sp.feedback_amount(7) * 2.5, 2 * 3.141592653589793)

    def test_numbers_have_no_exponent(self):
        for x in (3.05e-5, 1e-7, 123456789.0, -0.000123):
            self.assertNotIn("e", sp.num(x).lower())


def interp(pts, x):
    if x <= pts[0][0]:
        return pts[0][1]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        if x <= x1:
            return y1 if x1 == x0 else y0 + (y1 - y0) * (x - x0) / (x1 - x0)
    return pts[-1][1]


@unittest.skipUnless(RATSNEST or len(sys.argv) > 1, "no ratsnest binary given")
class EndToEnd(unittest.TestCase):
    def test_every_algorithm_and_lfo_converts_checks_and_renders(self):
        rng = random.Random(3)
        us = [random_unpacked(rng, a, f"E2E {a + 1}") for a in range(32)]
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / "bank.syx"
            src.write_bytes(bulk(us))
            out = pathlib.Path(tmp) / "bank"
            self.assertEqual(sp.main([str(src), "-o", str(out)]), 0)
            files = sorted(out.glob("*.sub")) + [out / "play.patch"]
            self.assertEqual(len(files), 33)
            for f in files:
                r = subprocess.run([RATSNEST, "check", str(f)],
                                   capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, f"{f.name}: {r.stdout}{r.stderr}")
            wav = pathlib.Path(tmp) / "take.wav"
            for f in files[:32:7]:
                r = subprocess.run([RATSNEST, "render", str(f), "-d", "2s",
                                    "-o", str(wav)], capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, f"{f.name}: {r.stdout}{r.stderr}")
                self.assertGreater(wav.stat().st_size, 1000)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        RATSNEST = sys.argv.pop(1)
    unittest.main()
