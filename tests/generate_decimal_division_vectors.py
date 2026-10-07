"""Generate independent division fixtures; Qt tests need only the checked-in JSON.

Decimal computes the quotient with 50 significant digits and ROUND_HALF_EVEN.
Fraction independently checks exact range, the nearest representable result and
halfway parity using integer division, including all digits of the remainder.
This generator never uses floating-point input or the C++ implementation.
"""
from decimal import Decimal, Inexact, ROUND_HALF_EVEN, localcontext
from fractions import Fraction
import json
from pathlib import Path
import random


def power10(exponent):
    return Fraction(10 ** exponent) if exponent >= 0 else Fraction(1, 10 ** -exponent)


def reference(left, right):
    a, b = Decimal(left), Decimal(right)
    negative = a.is_signed() != b.is_signed()
    result = dict(left=left, right=right, error='none', coefficient='', exponent=0,
                  negative=negative, inexact=False)
    if b.is_zero():
        result['error'] = 'division-by-zero'
        return result
    exact = Fraction(a) / Fraction(b)
    if exact:
        magnitude = abs(exact)
        if magnitude < power10(-999):
            result['error'] = 'underflow'
            return result
        if magnitude >= power10(1000):
            result['error'] = 'overflow'
            return result
    with localcontext() as context:
        context.prec = 50
        context.rounding = ROUND_HALF_EVEN
        context.Emin = -10000
        context.Emax = 10000
        context.clear_flags()
        answer = a / b
        changed = context.flags[Inexact]
    # Independently derive rounding from the exact rational, not Decimal's flags.
    if exact:
        exponent = len(str(magnitude.numerator)) - len(str(magnitude.denominator))
        if magnitude < power10(exponent):
            exponent -= 1
        quantum = power10(exponent - 49)
        units = magnitude / quantum
        lower, remainder = divmod(units.numerator, units.denominator)
        twice = 2 * remainder
        up = twice > units.denominator or (twice == units.denominator and lower % 2)
        expected = (lower + int(up)) * quantum
        assert abs(Fraction(answer)) == expected
    assert changed == (Fraction(answer) != exact)
    assert answer.is_signed() == negative
    if not answer.is_zero() and answer.adjusted() > 999:
        result['error'] = 'overflow'
        return result
    assert answer.is_zero() or answer.adjusted() >= -999
    sign, digits, exponent = answer.as_tuple()
    digits = ''.join(map(str, digits))
    if answer.is_zero():
        digits, exponent = '0', 0
    else:
        while digits.endswith('0'):
            digits, exponent = digits[:-1], exponent + 1
    assert len(digits) <= 50
    result.update(coefficient=digits, exponent=exponent, negative=bool(sign), inexact=changed)
    return result


def main():
    rng = random.Random(2026100709)
    pairs = []
    # Full coefficient lengths and exponent extremes, plus dense normal-range cases.
    for index in range(240):
        values = []
        for operand in range(2):
            length = rng.randint(1, 50)
            coefficient = rng.randrange(10 ** (length - 1), 10 ** length)
            adjusted = rng.randint(-999, 999) if index < 120 else rng.randint(-450, 450)
            sign = '-' if rng.getrandbits(1) else ''
            values.append(f'{sign}{coefficient}e{adjusted - length + 1}')
        pairs.append(tuple(values))
    # Exact finite quotients on either side of the 50-digit precision boundary.
    for base, last in [(2, 165), (5, 71)]:
        for exponent in range(1, last + 1, 2):
            pairs.append(('1', str(base ** exponent)))
    # Halfway points and both sides; signs and scaling must not alter rounding.
    for index in range(80):
        n = rng.randrange(2 * 10**49, 10**50)
        scale = rng.choice([-999, -50, 0, 50, 950])
        pairs.append((f'{"-" if index % 2 else ""}{n}e{scale}',
                      f'{"-" if index % 3 else ""}{rng.choice([2, 4, 8, 16])}e{scale}'))
    for a in ['0', '-0', '1e-999', '-1e-999', '1e999', '-1e999']:
        for b in ['0', '-0', '1e-999', '-1e-999', '1e999', '-1e999']:
            pairs.append((a, b))
    for a, b in pairs:
        for text in (a, b):
            value = Decimal(text)
            assert len(value.as_tuple().digits) <= 50
            assert value.is_zero() or -999 <= value.adjusted() <= 999
    rows = [dict(name=f'oracle-{i:03}', **reference(a, b)) for i, (a, b) in enumerate(pairs)]
    assert {row['error'] for row in rows} == {'none', 'overflow', 'underflow', 'division-by-zero'}
    assert {row['inexact'] for row in rows if row['error'] == 'none'} == {False, True}
    target = Path(__file__).parent / 'data' / 'decimal_division_vectors.json'
    target.write_text(json.dumps(rows, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'{len(rows)} division vectors written; Decimal and Fraction agree')


if __name__ == '__main__':
    main()
