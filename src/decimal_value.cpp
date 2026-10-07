#include "decimal_value.h"

#include <algorithm>
#include <utility>

constexpr int DecimalValue::maximumDigits;
constexpr int DecimalValue::minimumAdjustedExponent;
constexpr int DecimalValue::maximumAdjustedExponent;
constexpr int DecimalValue::maximumLiteralLength;

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
