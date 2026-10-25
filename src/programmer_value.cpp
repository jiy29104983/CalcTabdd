#include "programmer_value.h"

#include <QStringList>
#include <limits>

bool ProgrammerConfig::isValid() const
{
    return bitWidth == 8 || bitWidth == 16 || bitWidth == 32 || bitWidth == 64;
}

quint64 ProgrammerValue::mask(ProgrammerConfig config)
{
    return config.bitWidth == 64 ? std::numeric_limits<quint64>::max()
                                : (quint64(1) << config.bitWidth) - 1;
}

quint64 ProgrammerValue::magnitudeLimit(ProgrammerConfig config, bool negative)
{
    if (!config.isSigned) return negative ? 0 : mask(config);
    const quint64 signBit = quint64(1) << (config.bitWidth - 1);
    return negative ? signBit : signBit - 1;
}

unsigned ProgrammerValue::wrapSource(ProgrammerConfig config)
{
    switch (config.bitWidth)
    {
    case 8: return Wrap8;
    case 16: return Wrap16;
    case 32: return Wrap32;
    default: return Wrap64;
    }
}

ProgrammerError ProgrammerValue::fromBits(quint64 bits, ProgrammerConfig config, ProgrammerValue &output)
{
    if (!config.isValid()) return ProgrammerError::InvalidConfig;
    if (bits > mask(config)) return ProgrammerError::Overflow;
    ProgrammerValue result;
    result.m_bits = bits;
    result.m_config = config;
    output = result;
    return ProgrammerError::None;
}

bool ProgrammerValue::isNegative() const
{
    return m_config.isSigned && (m_bits & (quint64(1) << (m_config.bitWidth - 1)));
}

quint64 ProgrammerValue::magnitude() const
{
    return isNegative() ? (quint64(0) - m_bits) & mask(m_config) : m_bits;
}

QString ProgrammerValue::text() const
{
    return (isNegative() ? QStringLiteral("-") : QString()) + QString::number(magnitude());
}

QString ProgrammerValue::sourceText() const
{
    QStringList descriptions;
    for (int width : {8, 16, 32, 64})
        if (m_sources & wrapSource({width, false}))
            descriptions.append(QStringLiteral("含 %1 位回绕").arg(width));
    return descriptions.join(QStringLiteral("；"));
}

bool ProgrammerValue::operator==(const ProgrammerValue &other) const
{
    return m_bits == other.m_bits && m_config == other.m_config && m_sources == other.m_sources;
}

ProgrammerError ProgrammerValue::converted(ProgrammerConfig config, ProgrammerValue &output) const
{
    if (!config.isValid()) return ProgrammerError::InvalidConfig;
    const quint64 absolute = magnitude();
    if (absolute > magnitudeLimit(config, isNegative())) return ProgrammerError::Overflow;
    ProgrammerValue result = *this;
    result.m_config = config;
    result.m_bits = (isNegative() ? quint64(0) - absolute : absolute) & mask(config);
    output = result;
    return ProgrammerError::None;
}

ProgrammerError ProgrammerValue::operate(IntegerUnary operation, const ProgrammerValue &value,
                                         ProgrammerValue &output)
{
    ProgrammerValue result = value;
    bool wrapped = false;
    switch (operation)
    {
    case IntegerUnary::Plus: break;
    case IntegerUnary::Negate:
        result.m_bits = (quint64(0) - value.m_bits) & mask(value.m_config);
        wrapped = value.magnitude() > magnitudeLimit(value.m_config, !value.isNegative());
        break;
    case IntegerUnary::Complement:
        result.m_bits = ~value.m_bits & mask(value.m_config);
        break;
    default: return ProgrammerError::InvalidOperation;
    }
    if (wrapped) result.m_sources |= wrapSource(value.m_config);
    output = result;
    return ProgrammerError::None;
}

ProgrammerError ProgrammerValue::operate(IntegerBinary operation, const ProgrammerValue &left,
                                         const ProgrammerValue &right, ProgrammerValue &output)
{
    if (left.m_config != right.m_config) return ProgrammerError::InvalidConfig;
    const ProgrammerConfig config = left.m_config;
    const quint64 bitMask = mask(config), a = left.m_bits, b = right.m_bits;
    const quint64 am = left.magnitude(), bm = right.magnitude();
    const bool an = left.isNegative(), bn = right.isNegative();
    ProgrammerValue result = left;
    result.m_sources |= right.m_sources;
    bool wrapped = false;
    switch (operation)
    {
    case IntegerBinary::Add:
        result.m_bits = (a + b) & bitMask;
        wrapped = config.isSigned ? an == bn && result.isNegative() != an : a > bitMask - b;
        break;
    case IntegerBinary::Subtract:
        result.m_bits = (a - b) & bitMask;
        wrapped = config.isSigned ? an != bn && result.isNegative() != an : a < b;
        break;
    case IntegerBinary::Multiply:
        result.m_bits = (a * b) & bitMask;
        wrapped = bm != 0 && am > magnitudeLimit(config, an != bn) / bm;
        break;
    case IntegerBinary::Divide:
    case IntegerBinary::Remainder:
    {
        if (!bm) return ProgrammerError::DivisionByZero;
        const bool division = operation == IntegerBinary::Divide;
        const quint64 absolute = division ? am / bm : am % bm;
        const bool negative = division ? an != bn : an;
        result.m_bits = (negative ? quint64(0) - absolute : absolute) & bitMask;
        wrapped = division && absolute > magnitudeLimit(config, negative);
        break;
    }
    case IntegerBinary::And: result.m_bits = a & b; break;
    case IntegerBinary::Xor: result.m_bits = a ^ b; break;
    case IntegerBinary::Or: result.m_bits = a | b; break;
    case IntegerBinary::LeftShift:
    case IntegerBinary::RightShift:
    case IntegerBinary::LogicalRightShift:
    {
        if (bn || b >= quint64(config.bitWidth)) return ProgrammerError::ShiftRange;
        const int shift = int(b); // 已限定为 0..63；不执行宽度等于 64 的移位。
        if (operation == IntegerBinary::LeftShift)
        {
            result.m_bits = (a << shift) & bitMask;
            wrapped = am > (magnitudeLimit(config, an) >> shift);
        }
        else
        {
            result.m_bits = a >> shift;
            if (operation == IntegerBinary::RightShift && an && shift != 0)
                result.m_bits |= bitMask ^ (bitMask >> shift);
        }
        break;
    }
    default: return ProgrammerError::InvalidOperation;
    }
    if (wrapped) result.m_sources |= wrapSource(config);
    output = result;
    return ProgrammerError::None;
}
