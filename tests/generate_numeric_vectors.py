"""Independent bounded decimal function and binary conversion fixtures.

Uses Decimal/Fraction/integer isqrt, not the C++ algorithms. Binary conversions
are compared to Python's binary64 bit pattern and exact rational value. Windows
Qt tests read the checked-in JSON without needing this generator at runtime.
"""
import argparse
from decimal import Decimal, localcontext, ROUND_FLOOR, ROUND_CEILING, ROUND_HALF_UP
from fractions import Fraction
import json
import math
from pathlib import Path
import random
import struct
from generate_decimal_division_vectors import reference as division_reference


def normalized(value):
    sign, digits, exponent = value.as_tuple()
    coefficient = ''.join(map(str, digits))
    if not value:
        coefficient, exponent = '0', 0
    else:
        while coefficient.endswith('0'):
            coefficient, exponent = coefficient[:-1], exponent + 1
    return dict(coefficient=coefficient, exponent=exponent, negative=bool(sign))


def classify(value):
    if value and value.adjusted() > 999:
        return 'overflow'
    if value and value.adjusted() < -999:
        return 'underflow'
    if len(normalized(value)['coefficient']) > 50:
        return 'precision'
    return 'none'


def reference(operation, left, right='0'):
    a, b = Decimal(left), Decimal(right)
    assert classify(a) == classify(b) == 'none'
    row = dict(operation=operation, left=left, right=right, error='none', exact=True,
               inexact=False, coefficient='', exponent=0, negative=False)
    with localcontext() as context:
        context.prec = 20000
        context.Emax = 20000000
        context.Emin = -20000000
        if operation == 'compare':
            row['comparison'] = (a > b) - (a < b)
            return row
        if operation == 'remainder':
            if not b:
                row['error'] = 'division-by-zero'
                return row
            rational = Fraction(a) - int(Fraction(a) / Fraction(b)) * Fraction(b)
            value = a % b
            assert Fraction(value) == rational
        elif operation in ('floor', 'ceil', 'round'):
            rounding = {'floor': ROUND_FLOOR, 'ceil': ROUND_CEILING, 'round': ROUND_HALF_UP}[operation]
            value = a.to_integral_value(rounding=rounding)
            rational = Fraction(a)
            magnitude = abs(rational)
            if operation == 'floor': integer = rational.numerator // rational.denominator
            elif operation == 'ceil': integer = -((-rational.numerator) // rational.denominator)
            else:
                integer = magnitude.numerator // magnitude.denominator
                integer += 2 * (magnitude.numerator % magnitude.denominator) >= magnitude.denominator
                if rational < 0: integer = -integer
            assert Fraction(value) == integer
        elif operation == 'sqrt':
            if a < 0:
                row['error'] = 'domain'
                return row
            rational = Fraction(a)
            numerator, denominator = math.isqrt(rational.numerator), math.isqrt(rational.denominator)
            row['exact'] = numerator * numerator == rational.numerator and denominator * denominator == rational.denominator
            if not row['exact']: return row
            value = a.sqrt()
            assert Fraction(value) == Fraction(numerator, denominator)
        elif operation == 'power':
            if b != b.to_integral_value() or (a == 0 and b < 0):
                row['error'] = 'domain'
                return row
            if abs(b) > 10000:
                row['error'] = 'resource'
                return row
            exponent = int(b)
            positive = Decimal(1) if exponent == 0 else a ** abs(exponent)
            row['error'] = classify(positive)
            if row['error'] != 'none': return row
            if exponent < 0:
                quotient = division_reference('1', str(positive))
                row.update({key: quotient[key] for key in ('error', 'coefficient', 'exponent', 'negative', 'inexact')})
                return row
            value = positive
        elif operation == 'convert':
            binary = float(a)
            if not math.isfinite(binary): row['error'] = 'conversion-overflow'
            elif binary == 0 and a != 0: row['error'] = 'conversion-underflow'
            else:
                row['bits'] = struct.pack('>d', binary).hex()
                row['changed'] = Fraction.from_float(binary) != Fraction(a)
            return row
        else: raise AssertionError(operation)
        row['error'] = classify(value)
        if row['error'] == 'none': row.update(normalized(value))
    return row


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=Path(__file__).parent / 'data' / 'numeric_vectors.json')
    target = parser.parse_args().output
    rng = random.Random(2026100710)
    cases = []
    for a, b in [('0.3','0.1'),('-5.5','2'),('5.5','-2'),('-4','2'),('1e999','3'),
                 ('1e-999','1e999'),('1.0000000000000000000000000000000000000000000000001e-999','1e-999'),
                 ('1e999','1.0000000000000000000000000000000000000000000000001e-999'),
                 ('-0','3'),('1','-0'),('0','0')]: cases.append(('remainder',a,b))
    for _ in range(140):
        a = f'{rng.randrange(-10**40,10**40)}e{rng.randint(-500,500)}'
        b = f'{rng.randrange(1,10**30)}e{rng.randint(-500,500)}'
        cases.append(('remainder', a, b))
        cases.append(('compare', a, b))
    for a, b in [('0','-0'),('1e999','9e998'),('-1e999','-9e998'),('1.2','1.2001'),('1.2','1.200'),
                 ('-0','-1e-999'),('0','1e-999'),('-1e-999','-0'),('-1','1')]:
        cases.append(('compare',a,b))
    integers = ['-0','0','-0.0001','0.0001','0.5','-0.5','2.5','-2.5','1e999','-1e-999',
                '9'*49+'.5','-'+'9'*49+'.5','9007199254740993.5']
    integers += [f'{rng.randrange(-10**49,10**49)}e{rng.randint(-70,40)}' for _ in range(30)]
    for a in integers:
        for operation in ('floor','ceil','round'): cases.append((operation,a,'0'))
    for a in ['-0','0','0.09','1e400','1e-998','1e-999','2','-1','1e999','9e998','4e-998']:
        cases.append(('sqrt',a,'0'))
    for _ in range(80):
        root = rng.randrange(1,10**24)
        exponent = rng.randint(-490,470)
        cases.append(('sqrt',f'{root*root}e{2*exponent}','0'))
        cases.append(('sqrt',f'{root*root+1}e{2*exponent}','0'))
    for a,b in [('0.1','2'),('2','-3'),('3','-1'),('0','0'),('0','-1'),('2','167'),('2','-200'),
                ('10','-1000'),('1e999','1'),('1e-999','1'),('1','10000'),('1','10001'),('-1','9999'),
                ('-1','10001'),('2','1e999'),('2','0.5'),('10','999'),('0.1','999'),('-2','-3')]:
        cases.append(('power',a,b))
    for _ in range(100):
        cases.append(('power',str(Decimal(rng.randrange(-999,999)).scaleb(rng.randint(-2,2))),str(rng.randrange(-20,21))))
    conversions = ['0','-0','0.5','0.1','9007199254740993','9007199254740992','1e400','1e-400',
                   '1e999','1e-999','4.9406564584124654e-324','2.4703282292062327e-324',
                   '2.4703282292062328e-324','2.2250738585072014e-308','1.7976931348623157e308',
                   '1.7976931348623159e308','1.000000000000000111022302462515654042363166809082',
                   '1.0000000000000001110223024625156540423631668090821']
    for _ in range(100):
        conversions.append(f'{rng.randrange(-10**49,10**49)}e{rng.randint(-400,310)}')
    with localcontext() as context:
        context.prec = 100
        for exponent in range(-20,100,3): conversions.append(str(Decimal(2)**exponent))
    cases.extend(('convert',value,'0') for value in conversions)
    rows = [dict(name=f'{op}-{i:03d}', **reference(op,a,b)) for i,(op,a,b) in enumerate(cases)]
    target.write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'{len(rows)} independent numeric vectors written')


if __name__ == '__main__': main()
