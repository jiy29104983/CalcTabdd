"""Fixed-width integer fixtures from Python arbitrary-precision arithmetic.

The oracle evaluates generated syntax trees, not the C++ parser or machine-sized
arithmetic. Windows tests read the checked-in JSON. Use --check to verify that
regeneration is byte-identical.
"""
import argparse
import json
from pathlib import Path
import random


OPERATORS = ("+", "-", "*", "/", "%", "&", "^", "|", "<<", ">>", ">>>")


def interpreted(bits, width, signed):
    bits %= 1 << width
    return bits - (1 << width) if signed and bits >= 1 << (width - 1) else bits


def reference(node, width, signed):
    if node[0] == "literal":
        return node[2], 0
    operation = node[0]
    a, sources = reference(node[1], width, signed)
    low = -(1 << (width - 1)) if signed else 0
    high = (1 << (width - int(signed))) - 1
    if operation in ("positive", "negative", "complement"):
        value = {"positive": lambda: a, "negative": lambda: -a, "complement": lambda: ~a}[operation]()
        may_wrap = operation == "negative"
    else:
        b, right_sources = reference(node[2], width, signed)
        sources |= right_sources
        if operation in ("/", "%") and b == 0:
            raise ValueError("DivisionByZero")
        if operation in ("<<", ">>", ">>>") and not 0 <= b < width:
            raise ValueError("Domain")
        if operation in ("/", "%"):
            quotient = (abs(a) // abs(b)) * (-1 if (a < 0) != (b < 0) else 1)
            value = quotient if operation == "/" else a - quotient * b
        else:
            value = {
                "+": lambda: a + b, "-": lambda: a - b, "*": lambda: a * b,
                "&": lambda: a & b, "^": lambda: a ^ b, "|": lambda: a | b,
                "<<": lambda: a << b, ">>": lambda: a >> b,
                ">>>": lambda: (a % (1 << width)) >> b,
            }[operation]()
        may_wrap = operation in ("+", "-", "*", "/", "<<")
    if may_wrap and not low <= value <= high:
        sources |= {8: 1, 16: 2, 32: 4, 64: 8}[width]
    return interpreted(value, width, signed), sources


def expression(node):
    if node[0] == "literal":
        return node[1]
    if node[0] in ("positive", "negative", "complement"):
        return {"positive": "+", "negative": "-", "complement": "~"}[node[0]] + "(" + expression(node[1]) + ")"
    return "(" + expression(node[1]) + " " + node[0] + " " + expression(node[2]) + ")"


def literal(rng, width, signed):
    mask = (1 << width) - 1
    bits = rng.choice([0, 1, 2, mask, mask - 1, 1 << (width - 1), (1 << (width - 1)) - 1,
                       rng.getrandbits(width), rng.getrandbits(width)])
    value = interpreted(bits, width, signed)
    base = rng.choice((2, 8, 10, 16))
    if base == 10:
        token = str(abs(value))
    else:
        token = format(bits, {2: "b", 8: "o", 16: "X"}[base])
    if rng.getrandbits(1):
        raw = token
        token = "_".join(reversed([raw[max(0, i - 4):i] for i in range(len(raw), 0, -4)]))
    if base == 10 and value < 0:
        token = "(-" + token + ")"
    elif base != 10:
        token = {2: "0B", 8: "0o", 16: "0X"}[base] + token
    return ("literal", token, value)


def tree(rng, width, signed, depth):
    if depth == 0 or rng.randrange(4) == 0:
        return literal(rng, width, signed)
    if rng.randrange(4) == 0:
        return (rng.choice(("positive", "negative", "complement")), tree(rng, width, signed, depth - 1))
    operation = rng.choice(OPERATORS)
    left = tree(rng, width, signed, depth - 1)
    if operation in ("<<", ">>", ">>>") and rng.randrange(5):
        count = rng.choice((0, 1, width - 1, width, rng.randrange(width)))
        right = ("literal", str(count), count)
    else:
        right = tree(rng, width, signed, depth - 1)
    return (operation, left, right)


def vectors():
    rng = random.Random(20261025)
    rows = []
    for width in (8, 16, 32, 64):
        for signed in (False, True):
            mask = (1 << width) - 1
            nodes = []
            # Each operator crosses all important sign/width boundaries.
            edges = (0, 1, 2, mask, mask - 1, 1 << (width - 1), (1 << (width - 1)) - 1)
            for operation in OPERATORS:
                for a in edges:
                    b = rng.choice(edges) if operation not in ("<<", ">>", ">>>") else rng.choice((0, 1, width - 1, width))
                    nodes.append((operation, ("literal", hex(a), interpreted(a, width, signed)),
                                  ("literal", hex(b), interpreted(b, width, signed))))
            nodes.extend(tree(rng, width, signed, 4) for _ in range(160))
            for index, node in enumerate(nodes):
                row = dict(name=f'{"s" if signed else "u"}{width}-{index}', width=width, signed=signed,
                           expression=expression(node), error="None", text="", bits="", sources=0)
                try:
                    value, sources = reference(node, width, signed)
                    row.update(text=str(value), bits=format(value % (1 << width), "x"), sources=sources)
                except ValueError as error:
                    row["error"] = str(error)
                rows.append(row)
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    target = Path(__file__).parent / "data" / "programmer_vectors.json"
    rows = vectors()
    content = (json.dumps(rows, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    if args.check:
        assert target.read_bytes() == content, "Programmer fixtures differ from independent regeneration"
    else:
        target.write_bytes(content)
    print(f'{len(rows)} programmer vectors {"verified" if args.check else "written"}')


if __name__ == "__main__":
    main()
