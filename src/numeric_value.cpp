#include "numeric_value.h"

#include <QLocale>
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <limits>

namespace {
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53
              && std::numeric_limits<double>::max_exponent == 1024, "Requires IEEE 754 binary64");

NumericStatus decimalStatus(DecimalError error)
{
    switch (error)
    {
    case DecimalError::None: return {};
    case DecimalError::Syntax: return {NumericError::Syntax};
    case DecimalError::PrecisionLimit: return {NumericError::PrecisionLimit};
    case DecimalError::Overflow: return {NumericError::Overflow};
    case DecimalError::Underflow: return {NumericError::Underflow};
    case DecimalError::ResourceLimit: return {NumericError::ResourceLimit};
    case DecimalError::DivisionByZero: return {NumericError::DivisionByZero};
    case DecimalError::Domain: return {NumericError::Domain};
    }
    return {NumericError::Domain};
}

// 保存调用方异常状态／舍入模式及 errno，单次检查不泄漏科学库状态。
class FloatingEnvironment
{
public:
    FloatingEnvironment() : m_errno(errno), m_saved(std::feholdexcept(&m_environment) == 0) { errno = 0; }
    ~FloatingEnvironment()
    {
        if (m_saved) std::fesetenv(&m_environment);
        errno = m_errno;
    }
    bool underflow() const { return (m_saved && std::fetestexcept(FE_UNDERFLOW)) || errno == ERANGE; }
private:
    int m_errno;
    bool m_saved;
    std::fenv_t m_environment;
};

void multiplyDigits(QByteArray &digits, int factor)
{
    int carry = 0;
    for (int i = digits.size() - 1; i >= 0; --i)
    {
        const int product = (digits.at(i) - '0') * factor + carry;
        digits[i] = char('0' + product % 10);
        carry = product / 10;
    }
    if (carry) digits.prepend(char('0' + carry));
}

bool sameExactValue(const DecimalValue &decimal, double binary)
{
    if (decimal.isZero()) return binary == 0;
    if (binary == 0 || decimal.isNegative() != std::signbit(binary)) return false;
    // 从二进制有效数和二进制指数构造其精确十进制表示；最多 1074 次小整数乘法。
    // 不使用 15/17 位打印值比较，避免把 0.1 的回显误判为无损转换。
    int exponent = 0;
    const double fraction = std::frexp(std::abs(binary), &exponent);
    const auto integer = static_cast<quint64>(std::ldexp(fraction, 53));
    exponent -= 53;
    QByteArray digits = QByteArray::number(integer);
    int decimalExponent = 0;
    if (exponent >= 0)
        for (int i = 0; i < exponent; ++i) multiplyDigits(digits, 2);
    else
    {
        // 先去掉二进制尾零，非正规数也不会多做无用的 53 位展开。
        quint64 reduced = integer;
        while (exponent < 0 && reduced % 2 == 0) { reduced /= 2; ++exponent; }
        digits = QByteArray::number(reduced);
        decimalExponent = exponent;
        for (int i = 0; i < -exponent; ++i) multiplyDigits(digits, 5);
    }
    while (digits.endsWith('0')) { digits.chop(1); ++decimalExponent; }
    return decimal.coefficient() == digits && decimal.exponent() == decimalExponent;
}

NumericStatus convert(const NumericValue &value, int argument, double &output, unsigned &sources)
{
    bool changed = false;
    auto status = value.toBinary(output, changed);
    if (!status.ok()) { status.argument = argument; return status; }
    sources |= value.sources() | NumericValue::Approximate;
    if (changed) sources |= NumericValue::ConversionLoss;
    return {};
}
}

NumericStatus NumericValue::parse(const QString &literal, NumericValue &output)
{
    DecimalValue decimal;
    const auto status = decimalStatus(DecimalValue::parse(literal, decimal));
    if (status.ok()) output = NumericValue(decimal);
    return status;
}

NumericStatus NumericValue::finishBinary(double value, unsigned sources, bool zeroUnderflow, NumericValue &output)
{
    if (std::isnan(value)) return {NumericError::Domain};
    if (!std::isfinite(value)) return {NumericError::Overflow};
    if (value == 0 && zeroUnderflow) return {NumericError::Underflow};
    NumericValue result;
    result.m_isBinary = true;
    result.m_binary = value;
    result.m_sources = sources | Approximate;
    output = result;
    return {};
}

NumericStatus NumericValue::fromBinary(double value, NumericValue &output)
{
    return finishBinary(value, 0, false, output);
}

NumericStatus NumericValue::constant(const QString &name, NumericValue &output)
{
    const FloatingEnvironment environment;
    if (name == QStringLiteral("pi")) return fromBinary(std::acos(-1.0), output);
    if (name == QStringLiteral("e")) return fromBinary(std::exp(1.0), output);
    return {NumericError::UnknownName};
}

NumericStatus NumericValue::toBinary(double &output, bool &changed) const
{
    if (m_isBinary) { output = m_binary; changed = false; return {}; }
    const FloatingEnvironment environment;
    const QString literal = (m_decimal.isNegative() ? QStringLiteral("-") : QString())
        + QString::fromLatin1(m_decimal.coefficient()) + QLatin1Char('e') + QString::number(m_decimal.exponent());
    bool ok = false;
    const double value = QLocale::c().toDouble(literal, &ok);
    // 输入由已验证的数值构造；允许转换器同时报告 ERANGE 的有限非零次正规值。
    if (!std::isfinite(value)) return {NumericError::ConversionOverflow};
    if (value == 0 && !m_decimal.isZero()) return {NumericError::ConversionUnderflow};
    if (!ok && value == 0 && m_decimal.isZero()) return {NumericError::Syntax};
    const bool loss = !sameExactValue(m_decimal, value);
    output = value;
    changed = loss;
    return {};
}

bool NumericValue::isZero() const { return m_isBinary ? m_binary == 0 : m_decimal.isZero(); }
bool NumericValue::isNegative() const { return m_isBinary ? std::signbit(m_binary) : m_decimal.isNegative(); }
bool NumericValue::isInteger() const
{
    if (!m_isBinary) return m_decimal.isInteger();
    const FloatingEnvironment environment;
    return std::trunc(m_binary) == m_binary;
}

NumericValue NumericValue::negated() const
{
    NumericValue result = *this;
    if (m_isBinary) result.m_binary = -m_binary;
    else result.m_decimal = m_decimal.negated();
    return result;
}

NumericStatus NumericValue::operate(char operation, const NumericValue &left, const NumericValue &right,
                                    NumericValue &output)
{
    if ((operation == '/' || operation == '%') && right.isZero()) return {NumericError::DivisionByZero, 1};
    if (operation == '^')
    {
        if (left.isZero() && right.isNegative() && !right.isZero()) return {NumericError::Domain, 1};
        if (left.isNegative() && !left.isZero() && !right.isInteger()) return {NumericError::Domain, 1};
    }
    unsigned sources = left.m_sources | right.m_sources;
    if (!left.m_isBinary && !right.m_isBinary && (operation != '^' || right.isInteger()))
    {
        NumericValue result;
        bool rounded = false;
        DecimalError error;
        switch (operation)
        {
        case '+': error = DecimalValue::add(left.m_decimal, right.m_decimal, result.m_decimal); break;
        case '-': error = DecimalValue::subtract(left.m_decimal, right.m_decimal, result.m_decimal); break;
        case '*': error = DecimalValue::multiply(left.m_decimal, right.m_decimal, result.m_decimal); break;
        case '/': error = DecimalValue::divide(left.m_decimal, right.m_decimal, result.m_decimal, rounded); break;
        case '%': error = DecimalValue::remainder(left.m_decimal, right.m_decimal, result.m_decimal); break;
        case '^': error = DecimalValue::integerPower(left.m_decimal, right.m_decimal, result.m_decimal, rounded); break;
        default: return {NumericError::Syntax};
        }
        auto status = decimalStatus(error);
        if (operation == '^' && error == DecimalError::ResourceLimit) status.argument = 1;
        if (!status.ok()) return status;
        result.m_sources = sources | (rounded ? unsigned(Rounded) : 0u);
        output = result;
        return {};
    }
    double a = 0, b = 0;
    auto status = convert(left, 0, a, sources);
    if (!status.ok()) return status;
    status = convert(right, 1, b, sources);
    if (!status.ok()) return status;
    const FloatingEnvironment environment;
    double value;
    bool nonzero = false;
    switch (operation)
    {
    case '+': value = a + b; break;
    case '-': value = a - b; break;
    case '*': value = a * b; nonzero = a != 0 && b != 0; break;
    case '/': value = a / b; nonzero = a != 0; break;
    case '%': value = std::fmod(a, b); break;
    case '^': value = std::pow(a, b); nonzero = a != 0; break;
    default: return {NumericError::Syntax};
    }
    return finishBinary(value, sources, nonzero || environment.underflow(), output);
}

int NumericValue::argumentCount(const QString &name)
{
    if (name == QStringLiteral("min") || name == QStringLiteral("max") || name == QStringLiteral("pow")) return 2;
    for (const auto *function : {"sqrt", "abs", "sin", "cos", "tan", "ln", "log", "exp", "floor", "ceil", "round"})
        if (name == QLatin1String(function)) return 1;
    return 0;
}

NumericStatus NumericValue::function(const QString &name, const QVector<NumericValue> &arguments, NumericValue &output)
{
    const int count = argumentCount(name);
    if (!count) return {NumericError::UnknownName};
    if (arguments.size() != count) return {NumericError::ArgumentCount};
    if (name == QStringLiteral("pow")) return operate('^', arguments.at(0), arguments.at(1), output);
    const auto &first = arguments.first();
    if (name == QStringLiteral("sqrt") && first.isNegative() && !first.isZero()) return {NumericError::Domain, 0};
    if ((name == QStringLiteral("ln") || name == QStringLiteral("log")) && (first.isNegative() || first.isZero()))
        return {NumericError::Domain, 0};
    unsigned sources = 0;
    bool binary = false;
    for (const auto &argument : arguments) { sources |= argument.m_sources; binary |= argument.m_isBinary; }
    if (!binary)
    {
        NumericValue result;
        bool exact = true;
        DecimalError error = DecimalError::None;
        if (name == QStringLiteral("abs")) result.m_decimal = first.m_decimal.absolute();
        else if (name == QStringLiteral("min") || name == QStringLiteral("max"))
        {
            const int comparison = DecimalValue::compare(first.m_decimal, arguments.at(1).m_decimal);
            result = (name == QStringLiteral("min") ? comparison <= 0 : comparison >= 0) ? first : arguments.at(1);
        }
        else if (name == QStringLiteral("floor") || name == QStringLiteral("ceil") || name == QStringLiteral("round"))
            error = DecimalValue::integral(first.m_decimal, name == QStringLiteral("floor") ? DecimalValue::IntegralRounding::Floor
                : name == QStringLiteral("ceil") ? DecimalValue::IntegralRounding::Ceiling : DecimalValue::IntegralRounding::HalfAwayFromZero,
                result.m_decimal);
        else if (name == QStringLiteral("sqrt")) error = DecimalValue::squareRootExact(first.m_decimal, result.m_decimal, exact);
        else exact = false;
        if (error != DecimalError::None) return decimalStatus(error);
        if (exact) { result.m_sources = sources; output = result; return {}; }
    }
    double a = 0, b = 0;
    auto status = convert(first, 0, a, sources);
    if (!status.ok()) return status;
    if (count == 2)
    {
        status = convert(arguments.at(1), 1, b, sources);
        if (!status.ok()) return status;
    }
    const FloatingEnvironment environment;
    double value;
    if (name == QStringLiteral("min")) value = a <= b ? a : b;
    else if (name == QStringLiteral("max")) value = a >= b ? a : b;
    else if (name == QStringLiteral("sqrt")) value = std::sqrt(a);
    else if (name == QStringLiteral("abs")) value = std::abs(a);
    else if (name == QStringLiteral("sin")) value = std::sin(a);
    else if (name == QStringLiteral("cos")) value = std::cos(a);
    else if (name == QStringLiteral("tan")) value = std::tan(a);
    else if (name == QStringLiteral("ln")) value = std::log(a);
    else if (name == QStringLiteral("log")) value = std::log10(a);
    else if (name == QStringLiteral("exp")) value = std::exp(a);
    else if (name == QStringLiteral("floor")) value = std::floor(a);
    else if (name == QStringLiteral("ceil")) value = std::ceil(a);
    else value = std::round(a);
    return finishBinary(value, sources, name == QStringLiteral("exp") || environment.underflow(), output);
}
