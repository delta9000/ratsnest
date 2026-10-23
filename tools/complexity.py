#!/usr/bin/env python3
"""Complexity score for the ratsnest C++ core. Stdlib only.

Measures src/ (tests excluded) and prints one score plus its parts:

  score = decisions + 10 * hotspots + loc / 50 + 5 * modules

  decisions  total decision points (if/for/while/case/catch/&&/||/?) over
             every function body: the sum of (cyclomatic complexity - 1)
  hotspots   functions whose cyclomatic complexity exceeds 15 or whose
             body exceeds 80 lines
  loc        non-blank, non-comment lines
  modules    entries in the module registry (each is surface area a patch
             author has to learn)

`--check` compares against docs/complexity.json and fails when the score
grows past the recorded budget. Raising the budget is a deliberate edit to
that file, in the same commit as the change that needs it. `--update`
rewrites the baseline from the current tree.
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
BASELINE = ROOT / "docs" / "complexity.json"
CC_LIMIT = 15
LEN_LIMIT = 80
# A growing tree may spend this fraction of the budget without a baseline
# edit; anything more needs one.
SLACK = 0.02

DECISION = re.compile(r"\b(if|for|while|case|catch)\b|&&|\|\||\?")
KEYWORDS = {"if", "for", "while", "switch", "catch", "return", "sizeof",
            "decltype", "alignof", "static_assert", "noexcept", "requires"}


def strip(text):
    """Blank out comments, string and char literals, keeping line breaks."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif text.startswith('R"', i):
            m = re.match(r'R"([^(]*)\(', text[i:])
            end = text.find(")" + m.group(1) + '"', i) if m else -1
            j = n if end < 0 else end + len(m.group(1)) + 2
            out.append('""' + "\n" * text.count("\n", i, j))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def functions(code):
    """Yield (name, body) for each function definition body.

    A '{' opens a function body when the text before it ends with a
    parameter list ')' optionally followed by qualifiers, an initializer
    list, or a trailing return type, and the name before '(' is not a
    control keyword. Lambdas inside a body count toward their enclosing
    function.
    """
    sig = re.compile(
        r"([A-Za-z_~][\w:<>~]*|operator\s*[^\s(]+)\s*\([^;{}]*\)\s*"
        r"(?:const|override|final|noexcept|mutable|->\s*[\w:<>*&\s]+|\s)*"
        r"(?::[^;{}]*)?$")
    depth, i, n = 0, 0, len(code)
    while i < n:
        c = code[i]
        if c == "{":
            head = code[max(0, i - 400):i]
            m = sig.search(head)
            name = m.group(1).split("::")[-1] if m else None
            if m and name not in KEYWORDS:
                d, j = 1, i + 1
                while j < n and d:
                    d += {"{": 1, "}": -1}.get(code[j], 0)
                    j += 1
                yield m.group(1), code[i:j]
                i = j
                continue
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1


def measure():
    loc, decisions, hotspots, funcs = 0, 0, [], 0
    for path in sorted(SRC.rglob("*")):
        if path.suffix not in {".cpp", ".h"}:
            continue
        code = strip(path.read_text())
        loc += sum(1 for line in code.splitlines() if line.strip())
        for name, body in functions(code):
            funcs += 1
            d = len(DECISION.findall(body))
            decisions += d
            lines = sum(1 for line in body.splitlines() if line.strip())
            if d + 1 > CC_LIMIT or lines > LEN_LIMIT:
                hotspots.append((d + 1, lines,
                                 f"{path.relative_to(ROOT)}:{name}"))
    registry = (SRC / "dsp" / "registry.cpp").read_text()
    modules = len(re.findall(r"&\w+_info\b", registry))
    score = decisions + 10 * len(hotspots) + loc // 50 + 5 * modules
    return {"score": score, "decisions": decisions,
            "hotspots": len(hotspots), "loc": loc, "modules": modules,
            "functions": funcs}, sorted(hotspots, reverse=True)


def main(argv):
    m, hot = measure()
    print(f"complexity score {m['score']}  "
          f"(decisions {m['decisions']}, hotspots {m['hotspots']}, "
          f"loc {m['loc']}, modules {m['modules']}, "
          f"functions {m['functions']})")
    if "--hotspots" in argv:
        for cc, lines, where in hot:
            print(f"  cc {cc:4d}  lines {lines:4d}  {where}")
    if "--update" in argv:
        BASELINE.write_text(json.dumps(m, indent=2) + "\n")
        print(f"wrote {BASELINE.relative_to(ROOT)}")
        return 0
    if "--check" in argv:
        base = json.loads(BASELINE.read_text())
        budget = int(base["score"] * (1 + SLACK))
        delta = m["score"] - base["score"]
        print(f"baseline {base['score']}, budget {budget}, delta {delta:+d}")
        if m["score"] > budget:
            print("FAIL: complexity grew past budget. Simplify, or raise the "
                  "baseline deliberately with --update and say why in the "
                  "commit.")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
