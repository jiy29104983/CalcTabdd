#!/usr/bin/env python3
"""Independent Decimal reference for canonical decimal / binary64 display text."""
import argparse
from decimal import Decimal, localcontext, ROUND_HALF_EVEN
import json
import math
from pathlib import Path
import random
import struct


def canonical(value):
    if value.is_zero():
        return '-0' if value.is_signed() else '0'
    # Normalize without the default 28-digit context truncating the coefficient.
    with localcontext() as ctx:
        ctx.prec = max(1200, len(value.as_tuple().digits))
        value = value.normalize()
        if -6 <= value.adjusted() < 21:
            return format(value, 'f')
        return format(value, 'E').replace('E', 'e')


def generate():
    rows = []
    examples = ['0', '-0.000', '1.2300', '1000.00', '.3', '1e-6', '1e-7', '1e20', '1e21', '1.2300e21']
    for exponent in [-999, -998, -324, -100, -7, -6, -5, 0, 19, 20, 21, 22, 100, 998, 999]:
        for coefficient in ['1', '12345678901234567', '9' * 50, '1' + '0' * 48 + '1']:
            for sign in ['', '-']:
                examples.append(sign + coefficient + 'e' + str(exponent - len(coefficient) + 1))
    for literal in examples:
        rows.append(dict(name='decimal-' + str(len(rows)), kind='decimal', input=literal, text=canonical(Decimal(literal))))
    # Fixed half-to-even cases at 17 significant digits, exponent thresholds, extrema,
    # then deterministic random finite bit patterns. Expected text comes from Decimal.
    values = [0., -0., math.pi, math.e, 2.**-25, -2.**-25, 3*2.**-25, -3*2.**-25,
              float.fromhex('0x1.fffffffffffffp+1023'), float.fromhex('0x1p-1022'),
              float.fromhex('0x0.0000000000001p-1022')]
    for value in [1e-7, 1e-6, 1e20, 1e21, 1., 10., 1000000000000000.]:
        values.extend([value, math.nextafter(value, 0), math.nextafter(value, math.inf)])
    rng = random.Random(20261008)
    values.extend(struct.unpack('>d', rng.getrandbits(64).to_bytes(8, 'big'))[0] for _ in range(512))
    for value in values:
        if not math.isfinite(value):
            continue
        with localcontext() as ctx:
            ctx.prec = 17
            ctx.rounding = ROUND_HALF_EVEN
            # unary plus would remove the sign of -0 in Decimal.
            rounded = +Decimal.from_float(value) if value else Decimal('-0' if math.copysign(1., value) < 0 else '0')
        text = canonical(rounded)
        raw = struct.pack('>d', value).hex()
        assert struct.pack('>d', float(text)).hex() == raw
        rows.append(dict(name='binary-' + str(len(rows)), kind='binary64', input=raw, text=text))
    return rows


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).parent / 'data/numeric_text_vectors.json')
    args = parser.parse_args()
    rows = generate()
    args.output.write_text(json.dumps(rows, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'{len(rows)} independent formatting vectors written')
