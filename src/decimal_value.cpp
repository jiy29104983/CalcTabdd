#include "decimal_value.h"

#include <algorithm>
#include <utility>

constexpr int DecimalValue::maximumDigits;
constexpr int DecimalValue::minimumAdjustedExponent;
constexpr int DecimalValue::maximumAdjustedExponent;
constexpr int DecimalValue::maximumLiteralLength;
constexpr int DecimalValue::maximumIntegerPower;

namespace {
bool digit(QChar value)
{
    return value >= QLatin1Char('0') && value <= QLatin1Char('9');
}

QByteArray stripLeadingZeros(QByteArray digits)
{
    int start = 0;
    while (start + 1 < digits.size() && digits.at(start) == '0') ++start;
    return digits.mid(start);
}

int compareMagnitude(const QByteArray &left, const QByteArray &right)
{
    return left.size() == right.size() ? left.compare(right) : (left.size() > right.size() ? 1 : -1);
}

QByteArray addMagnitude(const QByteArray &left, const QByteArray &right)
{
    const int size = qMax(left.size(), right.size());
    QByteArray result(size + 1, '0');
    int carry = 0;
    for (int offset = 0; offset < size; ++offset)
    {
        const int a = offset < left.size() ? left.at(left.size() - 1 - offset) - '0' : 0;
        const int b = offset < right.size() ? right.at(right.size() - 1 - offset) - '0' : 0;
        const int sum = a + b + carry;
        result[size - offset] = char('0' + sum % 10);
        carry = sum / 10;
    }
    result[0] = char('0' + carry);
    return stripLeadingZeros(std::move(result));
}

// 有界小整数乘法，供开方的逐位试商使用。
QByteArray multiplySmall(const QByteArray &value, int factor)
{
    QByteArray result(value.size(), '0');
    int carry = 0;
    for (int i = value.size() - 1; i >= 0; --i)
    {
        const int product = (value.at(i) - '0') * factor + carry;
        result[i] = char('0' + product % 10);
        carry = product / 10;
    }
    if (carry) result.prepend(QByteArray::number(carry));
    return stripLeadingZeros(std::move(result));
}

// 两个无前导零的正整数，要求 left >= right。
QByteArray subtractMagnitude(const QByteArray &left, const QByteArray &right)
{
    QByteArray result(left.size(), '0');
    int borrow = 0;
    for (int offset = 0; offset < left.size(); ++offset)
    {
        const int b = offset < right.size() ? right.at(right.size() - 1 - offset) - '0' : 0;
        int difference = left.at(left.size() - 1 - offset) - '0' - b - borrow;
        borrow = difference < 0 ? 1 : 0;
        if (borrow) difference += 10;
        result[left.size() - 1 - offset] = char('0' + difference);
    }
    return stripLeadingZeros(std::move(result));
}
}

DecimalError DecimalValue::finish(QByteArray coefficient, int exponent, bool negative, DecimalValue &output)
{
    coefficient = stripLeadingZeros(std::move(coefficient));
    if (coefficient == "0") exponent = 0;
    else
    {
        while (coefficient.endsWith('0'))
        {
            coefficient.chop(1);
            ++exponent;
        }
        const int adjusted = exponent + coefficient.size() - 1;
        if (adjusted > maximumAdjustedExponent) return DecimalError::Overflow;
        if (adjusted < minimumAdjustedExponent) return DecimalError::Underflow;
        if (coefficient.size() > maximumDigits) return DecimalError::PrecisionLimit;
    }
    DecimalValue value;
    value.m_coefficient = std::move(coefficient);
    value.m_exponent = exponent;
    value.m_negative = negative;
    output = std::move(value);
    return DecimalError::None;
}

DecimalError DecimalValue::parse(const QString &text, DecimalValue &output)
{
    if (text.size() > maximumLiteralLength) return DecimalError::ResourceLimit;
    int position = 0;
    bool negative = false;
    if (position < text.size() && (text.at(position) == QLatin1Char('+') || text.at(position) == QLatin1Char('-')))
        negative = text.at(position++) == QLatin1Char('-');
    QByteArray coefficient;
    while (position < text.size() && digit(text.at(position))) coefficient.append(text.at(position++).toLatin1());
    int fractionalDigits = 0;
    if (position < text.size() && text.at(position) == QLatin1Char('.'))
    {
        ++position;
        while (position < text.size() && digit(text.at(position)))
        {
            coefficient.append(text.at(position++).toLatin1());
            ++fractionalDigits;
        }
    }
    if (coefficient.isEmpty()) return DecimalError::Syntax;
    int exponent = 0;
    bool exponentNegative = false;
    if (position < text.size() && (text.at(position) == QLatin1Char('e') || text.at(position) == QLatin1Char('E')))
    {
        ++position;
        if (position < text.size() && (text.at(position) == QLatin1Char('+') || text.at(position) == QLatin1Char('-')))
            exponentNegative = text.at(position++) == QLatin1Char('-');
        const int start = position;
        // token 最多 4096 字符，尾数规范化最多移动 4096 位。
        // 饱和到更大的界限足以判定非零数越界；不解析巨型整数或按指数分配内存。
        const int exponentSaturation = maximumLiteralLength + maximumAdjustedExponent + 1;
        while (position < text.size() && digit(text.at(position)))
        {
            exponent = qMin(exponentSaturation, exponent * 10 + text.at(position++).unicode() - '0');
        }
        if (position == start) return DecimalError::Syntax;
    }
    if (position != text.size()) return DecimalError::Syntax;
    if (exponentNegative) exponent = -exponent;
    return finish(std::move(coefficient), exponent - fractionalDigits, negative, output);
}

DecimalValue DecimalValue::negated() const
{
    DecimalValue value = *this;
    value.m_negative = !value.m_negative;
    return value;
}

DecimalError DecimalValue::add(const DecimalValue &left, const DecimalValue &right, DecimalValue &output)
{
    if (left.isZero() && right.isZero())
        return finish("0", 0, left.m_negative && right.m_negative, output);
    if (left.isZero()) { output = right; return DecimalError::None; }
    if (right.isZero()) { output = left; return DecimalError::None; }
    const int exponent = qMin(left.m_exponent, right.m_exponent);
    // 最远对齐：最高位置 999，最低位置 -999-(50-1)，最多 2048 位；加法另有一位进位。
    QByteArray a = left.m_coefficient + QByteArray(left.m_exponent - exponent, '0');
    QByteArray b = right.m_coefficient + QByteArray(right.m_exponent - exponent, '0');
    if (left.m_negative == right.m_negative)
        return finish(addMagnitude(a, b), exponent, left.m_negative, output);
    const int comparison = a.size() == b.size() ? a.compare(b) : (a.size() > b.size() ? 1 : -1);
    if (comparison == 0) return finish("0", 0, false, output);
    return comparison > 0
        ? finish(subtractMagnitude(a, b), exponent, left.m_negative, output)
        : finish(subtractMagnitude(b, a), exponent, right.m_negative, output);
}

DecimalError DecimalValue::subtract(const DecimalValue &left, const DecimalValue &right, DecimalValue &output)
{
    return add(left, right.negated(), output);
}

DecimalError DecimalValue::multiply(const DecimalValue &left, const DecimalValue &right, DecimalValue &output)
{
    if (left.isZero() || right.isZero()) return finish("0", 0, left.m_negative != right.m_negative, output);
    // 系数各最多 50 位；完整乘积最多 100 位，规范化之后才检查精度。
    QByteArray result(left.m_coefficient.size() + right.m_coefficient.size(), '0');
    for (int i = left.m_coefficient.size() - 1; i >= 0; --i)
    {
        int carry = 0;
        for (int j = right.m_coefficient.size() - 1; j >= 0; --j)
        {
            const int value = (left.m_coefficient.at(i) - '0') * (right.m_coefficient.at(j) - '0')
                + result.at(i + j + 1) - '0' + carry;
            result[i + j + 1] = char('0' + value % 10);
            carry = value / 10;
        }
        result[i] = char('0' + carry);
    }
    return finish(std::move(result), left.m_exponent + right.m_exponent,
                  left.m_negative != right.m_negative, output);
}

DecimalError DecimalValue::divide(const DecimalValue &left, const DecimalValue &right,
                                  DecimalValue &output, bool &inexact)
{
    if (right.isZero()) return DecimalError::DivisionByZero;
    const bool negative = left.m_negative != right.m_negative;
    if (left.isZero())
    {
        const auto error = finish("0", 0, negative, output);
        if (error == DecimalError::None) inexact = false;
        return error;
    }

    const auto &a = left.m_coefficient;
    const auto &b = right.m_coefficient;
    int quotientExponent = a.size() - b.size();
    // 先比较等长整数，精确确定 floor(log10(a/b))，不经浮点或舍入。
    const int comparison = quotientExponent >= 0
        ? compareMagnitude(a, b + QByteArray(quotientExponent, '0'))
        : compareMagnitude(a + QByteArray(-quotientExponent, '0'), b);
    if (comparison < 0) --quotientExponent;
    const int adjusted = left.m_exponent - right.m_exponent + quotientExponent;
    if (adjusted > maximumAdjustedExponent) return DecimalError::Overflow;
    if (adjusted < minimumAdjustedExponent) return DecimalError::Underflow;

    // 只按系数长度缩放到 [1,10)，与数值指数差无关。除数最多 50 位，余数临时最多 51 位。
    QByteArray remainder = a;
    QByteArray divisor = b;
    if (quotientExponent < 0) remainder.append(QByteArray(-quotientExponent, '0'));
    else divisor.append(QByteArray(quotientExponent, '0'));
    QByteArray coefficient;
    coefficient.reserve(maximumDigits + 1);
    for (int i = 0; i < maximumDigits; ++i)
    {
        // 每位开始时 remainder < 10*divisor，至多减 9 次。
        int nextDigit = 0;
        while (compareMagnitude(remainder, divisor) >= 0)
        {
            remainder = subtractMagnitude(remainder, divisor);
            ++nextDigit;
        }
        coefficient.append(char('0' + nextDigit));
        if (remainder == "0" || i + 1 == maximumDigits) break;
        remainder.append('0');
    }

    const bool changed = remainder != "0";
    const int exponent = adjusted - coefficient.size() + 1;
    if (changed)
    {
        // 用完整余数比较半个 ulp，覆盖首个舍弃位为 5 但后面仍非零的情况，避免二次舍入。
        const int halfway = compareMagnitude(addMagnitude(remainder, remainder), divisor);
        if (halfway > 0 || (halfway == 0 && (coefficient.back() - '0') % 2 != 0))
            coefficient = addMagnitude(coefficient, "1");
    }
    DecimalValue value;
    const auto error = finish(std::move(coefficient), exponent, negative, value);
    if (error != DecimalError::None) return error;
    output = std::move(value);
    inexact = changed;
    return DecimalError::None;
}

DecimalValue DecimalValue::absolute() const
{
    DecimalValue value = *this;
    value.m_negative = false;
    return value;
}

int DecimalValue::compare(const DecimalValue &left, const DecimalValue &right)
{
    if (left.isZero() && right.isZero()) return 0;
    if (left.m_negative != right.m_negative) return left.m_negative ? -1 : 1;
    const int sign = left.m_negative ? -1 : 1;
    if (left.isZero()) return -sign;
    if (right.isZero()) return sign;
    const int a = left.m_exponent + left.m_coefficient.size();
    const int b = right.m_exponent + right.m_coefficient.size();
    if (a != b) return (a < b ? -1 : 1) * sign;
    const int digits = qMax(left.m_coefficient.size(), right.m_coefficient.size());
    for (int i = 0; i < digits; ++i)
    {
        const char x = i < left.m_coefficient.size() ? left.m_coefficient.at(i) : '0';
        const char y = i < right.m_coefficient.size() ? right.m_coefficient.at(i) : '0';
        if (x != y) return (x < y ? -1 : 1) * sign;
    }
    return 0;
}

DecimalError DecimalValue::remainder(const DecimalValue &left, const DecimalValue &right, DecimalValue &output)
{
    if (right.isZero()) return DecimalError::DivisionByZero;
    if (left.isZero() || compare(left.absolute(), right.absolute()) < 0)
    {
        output = left;
        return DecimalError::None;
    }
    const int exponent = qMin(left.m_exponent, right.m_exponent);
    // 指数范围固定，对齐最多 2048 位；不存储商，不使用有舍入的除法。
    const QByteArray dividend = left.m_coefficient + QByteArray(left.m_exponent - exponent, '0');
    const QByteArray divisor = right.m_coefficient + QByteArray(right.m_exponent - exponent, '0');
    QByteArray remainder("0");
    for (const char digit : dividend)
    {
        remainder = stripLeadingZeros(remainder + digit);
        // 上一步余数 < divisor，因此本位至多减 9 次。
        while (compareMagnitude(remainder, divisor) >= 0)
            remainder = subtractMagnitude(remainder, divisor);
    }
    return finish(std::move(remainder), exponent, left.m_negative, output);
}

DecimalError DecimalValue::integral(const DecimalValue &value, IntegralRounding rounding, DecimalValue &output)
{
    if (value.isInteger()) { output = value; return DecimalError::None; }
    const int integerDigits = value.m_coefficient.size() + value.m_exponent;
    QByteArray integer = integerDigits > 0 ? value.m_coefficient.left(integerDigits) : QByteArray("0");
    // 规范化系数不以零结尾，因此截断的部分必含非零位。
    const bool increase = (rounding == IntegralRounding::Floor && value.m_negative)
        || (rounding == IntegralRounding::Ceiling && !value.m_negative)
        || (rounding == IntegralRounding::HalfAwayFromZero && integerDigits >= 0
            && value.m_coefficient.at(integerDigits) >= '5');
    if (increase) integer = addMagnitude(integer, "1");
    return finish(std::move(integer), 0, value.m_negative, output);
}

DecimalError DecimalValue::integerPower(const DecimalValue &base, const DecimalValue &power,
                                      DecimalValue &output, bool &inexact)
{
    if (!power.isInteger()) return DecimalError::Domain;
    if (base.isZero() && power.m_negative && !power.isZero()) return DecimalError::Domain;
    DecimalValue maximum;
    parse(QString::number(maximumIntegerPower), maximum);
    if (compare(power.absolute(), maximum) > 0) return DecimalError::ResourceLimit;
    int count = power.m_coefficient.toInt();
    for (int i = 0; i < power.m_exponent; ++i) count *= 10;
    DecimalValue one;
    parse(QStringLiteral("1"), one);
    DecimalValue result = one;
    DecimalValue factor = base;
    while (count > 0)
    {
        if (count % 2)
        {
            const auto error = multiply(result, factor, result);
            if (error != DecimalError::None) return error;
        }
        count /= 2;
        if (count)
        {
            const auto error = multiply(factor, factor, factor);
            if (error != DecimalError::None) return error;
        }
    }
    bool changed = false;
    if (power.m_negative && !power.isZero())
    {
        const auto error = divide(one, result, result, changed);
        if (error != DecimalError::None) return error;
    }
    output = result;
    inexact = changed;
    return DecimalError::None;
}

DecimalError DecimalValue::squareRootExact(const DecimalValue &value, DecimalValue &output, bool &exact)
{
    if (value.m_negative && !value.isZero()) return DecimalError::Domain;
    if (value.isZero()) { output = value; exact = true; return DecimalError::None; }
    QByteArray digits = value.m_coefficient;
    int exponent = value.m_exponent;
    if (exponent % 2 != 0) { digits.append('0'); --exponent; }
    if (digits.size() % 2 != 0) digits.prepend('0');
    QByteArray root("0"), remainder("0");
    // 每次处理两位：(20*root+d)*d <= remainder，至多 26 次、每次至多 10 个候选。
    for (int i = 0; i < digits.size(); i += 2)
    {
        remainder = stripLeadingZeros(remainder + digits.mid(i, 2));
        const QByteArray twentyRoot = multiplySmall(root, 20);
        int next = 9;
        QByteArray candidate;
        for (;; --next)
        {
            candidate = multiplySmall(addMagnitude(twentyRoot, QByteArray::number(next)), next);
            if (compareMagnitude(candidate, remainder) <= 0) break;
        }
        remainder = subtractMagnitude(remainder, candidate);
        root = stripLeadingZeros(root + char('0' + next));
    }
    if (remainder != "0") { exact = false; return DecimalError::None; }
    const auto error = finish(std::move(root), exponent / 2, false, output);
    if (error == DecimalError::None) exact = true;
    return error;
}
