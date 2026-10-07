"""Regenerate bounded-decimal golden vectors using independent Python decimal arithmetic.

Only required when updating fixtures; Windows Qt tests read the checked-in JSON.
All arithmetic is performed at 2100 digits with Inexact trapped, then the product
limits are applied to the exact answer. No floating-point input conversion occurs.
"""
from decimal import Decimal, Inexact, localcontext
import json
from pathlib import Path
import random


def normalized(value):
    sign, digits, exponent = value.as_tuple()
    digits = ''.join(map(str, digits))
    if value.is_zero():
        return sign, '0', 0
    while digits.endswith('0'):
        digits = digits[:-1]
        exponent += 1
    return sign, digits, exponent


def main():
    rng = random.Random(20261007)
    rows = []
    with localcontext() as context:
        context.prec = 2100
        context.Emin = -10000
        context.Emax = 10000
        context.traps[Inexact] = True
        for index in range(120):
            values = []
            shared_adjusted = rng.choice([-999, -100, -1, 0, 49, 500, 999])
            for operand in range(2):
                count = rng.randint(1, 50)
                digits = str(rng.randint(1, 9)) + ''.join(str(rng.randint(0, 9)) for _ in range(count - 1))
                adjusted = shared_adjusted if index % 2 else rng.randint(-999, 999)
                text = ('-' if rng.getrandbits(1) else '') + digits + 'e' + str(adjusted - count + 1)
                values.append(Decimal(text))
            for operator in '+-*':
                a, b = values
                answer = a + b if operator == '+' else a - b if operator == '-' else a * b
                sign, digits, exponent = normalized(answer)
                adjusted = exponent + len(digits) - 1
                error = 'none'
                if not answer.is_zero():
                    if adjusted > 999:
                        error = 'overflow'
                    elif adjusted < -999:
                        error = 'underflow'
                    elif len(digits) > 50:
                        error = 'precision'
                rows.append(dict(left=str(a), right=str(b), operation=operator, error=error,
                                 coefficient=digits if error == 'none' else '',
                                 exponent=exponent if error == 'none' else 0,
                                 negative=bool(sign) if error == 'none' else False))
    target = Path(__file__).parent / 'data' / 'decimal_vectors.json'
    target.write_text(json.dumps(rows, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'{len(rows)} vectors written')


if __name__ == '__main__':
    main()
