#include "numeric_value.h"

#include <QLocale>
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <limits>
#include <cstring>
#include <QStringList>

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

quint64 binaryBits(double value)
{
    quint64 bits;
    static_assert(sizeof(bits) == sizeof(value), "Requires 64-bit double");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void multiplyDigits(QByteArray &digits, quint32 factor)
{
    quint64 carry = 0;
    for (int i = digits.size() - 1; i >= 0; --i)
    {
        const quint64 product = quint64(digits.at(i) - '0') * factor + carry;
        digits[i] = char('0' + product % 10);
        carry = product / 10;
    }
    if (carry) digits.prepend(QByteArray::number(carry));
}

void normalizeDigits(QByteArray &digits, int &exponent)
{
    while (digits.size() > 1 && digits.endsWith('0')) { digits.chop(1); ++exponent; }
}

// 从位模式展开精确有限十进制数，不依赖平台浮点舍入模式、区域或格式库的半值规则。
// 最大 1074 次乘5，按9次合并；有界展开仅用于格式化与转换损失判断。
void exactBinaryDigits(double value, QByteArray &digits, int &decimalExponent)
{
    const quint64 bits = binaryBits(value);
    const int storedExponent = int((bits >> 52) & 0x7ff);
    quint64 integer = bits & ((quint64(1) << 52) - 1);
    if (storedExponent) integer |= quint64(1) << 52;
    int exponent = storedExponent ? storedExponent - 1023 - 52 : -1074;
    decimalExponent = 0;
    if (!integer) { digits = "0"; return; }
    while (exponent < 0 && integer % 2 == 0) { integer /= 2; ++exponent; }
    digits = QByteArray::number(integer);
    if (exponent >= 0)
    {
        while (exponent >= 29) { multiplyDigits(digits, quint32(1) << 29); exponent -= 29; }
        if (exponent) multiplyDigits(digits, quint32(1) << exponent);
    }
    else
    {
        decimalExponent = exponent;
        while (exponent <= -9) { multiplyDigits(digits, 1953125); exponent += 9; }
        while (exponent++ < 0) multiplyDigits(digits, 5);
    }
    normalizeDigits(digits, decimalExponent);
}

QString formatDigits(QByteArray digits, int exponent, bool negative)
{
    if (digits == "0") return negative ? QStringLiteral("-0") : QStringLiteral("0");
    normalizeDigits(digits, exponent);
    const int adjusted = exponent + digits.size() - 1;
    QByteArray body;
    if (adjusted >= -6 && adjusted < 21)
    {
        const int point = digits.size() + exponent;
        if (point <= 0) body = "0." + QByteArray(-point, '0') + digits;
        else if (exponent >= 0) body = digits + QByteArray(exponent, '0');
        else { body = digits; body.insert(point, '.'); }
    }
    else
    {
        body = digits;
        if (body.size() > 1) body.insert(1, '.');
        body += 'e';
        if (adjusted >= 0) body += '+';
        body += QByteArray::number(adjusted);
    }
    if (negative) body.prepend('-');
    return QString::fromLatin1(body);
}

bool sameExactValue(const DecimalValue &decimal, double binary)
{
    if (decimal.isZero()) return binary == 0;
    if (binary == 0 || decimal.isNegative() != std::signbit(binary)) return false;
    QByteArray digits;
    int exponent;
    exactBinaryDigits(binary, digits, exponent);
    return decimal.coefficient() == digits && decimal.exponent() == exponent;
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

int NumericValue::minimumArgumentCount(const QString &name)
{
    for (const auto *function : {"min", "max", "pow", "atan2", "hypot"})
        if (name == QLatin1String(function)) return 2;
    for (const auto *function : {"sqrt", "abs", "sin", "cos", "tan", "ln", "log", "exp", "floor", "ceil", "round",
                                 "trunc", "asin", "acos", "atan", "cbrt", "rad", "deg", "log2", "sum", "avg"})
        if (name == QLatin1String(function)) return 1;
    return 0;
}

int NumericValue::maximumArgumentCount(const QString &name)
{
    if (name == QStringLiteral("sum") || name == QStringLiteral("avg")
        || name == QStringLiteral("min") || name == QStringLiteral("max")) return 256;
    if (name == QStringLiteral("round") || name == QStringLiteral("log")) return 2;
    return minimumArgumentCount(name);
}

int NumericValue::argumentCount(const QString &name) { return minimumArgumentCount(name); }

bool NumericValue::isLegacyFunction(const QString &name)
{
    for (const auto *function : {"sqrt", "abs", "sin", "cos", "tan", "ln", "log", "exp", "floor", "ceil", "round",
                                 "min", "max", "pow"})
        if (name == QLatin1String(function)) return true;
    return false;
}

NumericStatus NumericValue::function(const QString &name, const QVector<NumericValue> &arguments, NumericValue &output)
{
    const int minimum = minimumArgumentCount(name);
    if (!minimum) return {NumericError::UnknownName};
    const int count = arguments.size();
    if (count < minimum || count > maximumArgumentCount(name)) return {NumericError::ArgumentCount};
    if (name == QStringLiteral("pow")) return operate('^', arguments.at(0), arguments.at(1), output);
    const auto &first = arguments.first();
    unsigned sources = 0;
    bool binary = false;
    for (const auto &argument : arguments) { sources |= argument.m_sources; binary |= argument.m_isBinary; }

    // 聚合按输入顺序逐步求值，每一步仍遵守原有精度和数值范围限制。
    if (name == QStringLiteral("sum") || name == QStringLiteral("avg"))
    {
        NumericValue result = first;
        for (int i = 1; i < count; ++i)
        {
            const auto status = operate('+', result, arguments.at(i), result);
            if (!status.ok()) return {status.error, status.argument == 1 ? i : -1};
        }
        if (name == QStringLiteral("avg"))
        {
            NumericValue divisor;
            parse(QString::number(count), divisor);
            const auto status = operate('/', result, divisor, result);
            if (!status.ok()) return {status.error, -1};
        }
        output = result;
        return {};
    }

    int places = 0;
    if (name == QStringLiteral("round") && count == 2)
    {
        if (!arguments.at(1).isInteger()) return {NumericError::Domain, 1};
        bool ok = false;
        places = arguments.at(1).integerText().toInt(&ok);
        if (!ok || places < -999 || places > 999) return {NumericError::ResourceLimit, 1};
    }
    DecimalValue one;
    DecimalValue::parse(QStringLiteral("1"), one);
    const auto equalsOne = [&one](const NumericValue &value) {
        return value.m_isBinary ? value.m_binary == 1 : DecimalValue::compare(value.m_decimal, one) == 0;
    };
    if (name == QStringLiteral("sqrt") && first.isNegative() && !first.isZero()) return {NumericError::Domain, 0};
    if ((name == QStringLiteral("ln") || name == QStringLiteral("log") || name == QStringLiteral("log2"))
        && (first.isNegative() || first.isZero())) return {NumericError::Domain, 0};
    if (name == QStringLiteral("log") && count == 2)
    {
        const auto &base = arguments.at(1);
        if (base.isNegative() || base.isZero() || equalsOne(base)) return {NumericError::Domain, 1};
    }
    if (name == QStringLiteral("asin") || name == QStringLiteral("acos"))
    {
        const bool outside = first.m_isBinary ? std::abs(first.m_binary) > 1
            : DecimalValue::compare(first.m_decimal.absolute(), one) > 0;
        if (outside) return {NumericError::Domain, 0};
    }
    if (name == QStringLiteral("atan2") && first.isZero() && arguments.at(1).isZero())
        return {NumericError::Domain, -1};

    if (!binary)
    {
        NumericValue result;
        bool exact = true;
        DecimalError error = DecimalError::None;
        if (name == QStringLiteral("abs")) result.m_decimal = first.m_decimal.absolute();
        else if (name == QStringLiteral("min") || name == QStringLiteral("max"))
        {
            result = first;
            for (int i = 1; i < count; ++i)
            {
                const int comparison = DecimalValue::compare(result.m_decimal, arguments.at(i).m_decimal);
                if (name == QStringLiteral("min") ? comparison > 0 : comparison < 0) result = arguments.at(i);
            }
        }
        else if (name == QStringLiteral("round")) error = DecimalValue::round(first.m_decimal, places, result.m_decimal);
        else if (name == QStringLiteral("floor") || name == QStringLiteral("ceil") || name == QStringLiteral("trunc"))
            error = DecimalValue::integral(first.m_decimal, name == QStringLiteral("floor") ? DecimalValue::IntegralRounding::Floor
                : name == QStringLiteral("ceil") ? DecimalValue::IntegralRounding::Ceiling : DecimalValue::IntegralRounding::TowardZero,
                result.m_decimal);
        else if (name == QStringLiteral("sqrt")) error = DecimalValue::squareRootExact(first.m_decimal, result.m_decimal, exact);
        else if (name == QStringLiteral("cbrt")) error = DecimalValue::cubeRootExact(first.m_decimal, result.m_decimal, exact);
        else exact = false;
        if (error != DecimalError::None) return decimalStatus(error);
        if (exact) { result.m_sources = sources; output = result; return {}; }
    }

    double a = 0, b = 0;
    auto status = convert(first, 0, a, sources);
    if (!status.ok()) return status;
    if (name == QStringLiteral("min") || name == QStringLiteral("max"))
    {
        double value = a;
        for (int i = 1; i < count; ++i)
        {
            status = convert(arguments.at(i), i, b, sources);
            if (!status.ok()) return status;
            // 首次相等值保留其位模式，尤其是 +0/-0；所有参数的来源仍合并。
            if (name == QStringLiteral("min") ? value > b : value < b) value = b;
        }
        return finishBinary(value, sources, false, output);
    }
    if (count == 2 && name != QStringLiteral("round"))
    {
        status = convert(arguments.at(1), 1, b, sources);
        if (!status.ok()) return status;
    }
    const FloatingEnvironment environment;
    double value;
    bool nonzero = false;
    if (name == QStringLiteral("sqrt")) value = std::sqrt(a);
    else if (name == QStringLiteral("cbrt")) value = std::cbrt(a);
    else if (name == QStringLiteral("abs")) value = std::abs(a);
    else if (name == QStringLiteral("sin")) value = std::sin(a);
    else if (name == QStringLiteral("cos")) value = std::cos(a);
    else if (name == QStringLiteral("tan")) value = std::tan(a);
    else if (name == QStringLiteral("asin")) value = std::asin(a);
    else if (name == QStringLiteral("acos")) value = std::acos(a);
    else if (name == QStringLiteral("atan")) value = std::atan(a);
    else if (name == QStringLiteral("atan2")) { value = std::atan2(a, b); nonzero = a != 0; }
    else if (name == QStringLiteral("hypot")) { value = std::hypot(a, b); nonzero = a != 0 || b != 0; }
    else if (name == QStringLiteral("rad")) { value = a * (std::acos(-1.0) / 180.0); nonzero = a != 0; }
    else if (name == QStringLiteral("deg")) { value = a * (180.0 / std::acos(-1.0)); nonzero = a != 0; }
    else if (name == QStringLiteral("ln")) value = std::log(a);
    else if (name == QStringLiteral("log2")) value = std::log2(a);
    else if (name == QStringLiteral("log") && count == 1) value = std::log10(a);
    else if (name == QStringLiteral("log"))
    {
        // 十进制底数接近 1 时不能先转成 binary64 的 1 再除以 log(1)。
        const auto logarithm = [&one, &sources](const NumericValue &input, double converted, double &result) {
            if (!input.m_isBinary && converted >= 0.5 && converted <= 1.5)
            {
                DecimalValue difference;
                auto checked = decimalStatus(DecimalValue::subtract(input.m_decimal, one, difference));
                if (!checked.ok()) return checked;
                double delta = 0;
                checked = convert(NumericValue(difference), -1, delta, sources);
                if (!checked.ok()) return checked;
                result = std::log1p(delta);
            }
            else result = std::log(converted);
            return NumericStatus{};
        };
        double numerator = 0, denominator = 0;
        status = logarithm(first, a, numerator);
        if (!status.ok()) return {status.error, 0};
        status = logarithm(arguments.at(1), b, denominator);
        if (!status.ok()) return {status.error, 1};
        value = numerator / denominator;
        nonzero = !equalsOne(first);
    }
    else if (name == QStringLiteral("exp")) { value = std::exp(a); nonzero = true; }
    else if (name == QStringLiteral("floor")) value = std::floor(a);
    else if (name == QStringLiteral("ceil")) value = std::ceil(a);
    else if (name == QStringLiteral("trunc")) value = std::trunc(a);
    else
    {
        // 按保存的 binary64 实际值量化，不先乘 10^n，避免溢出和中点翻转。
        QByteArray digits;
        int exponent = 0;
        exactBinaryDigits(a, digits, exponent);
        const int targetExponent = -places;
        if (digits == "0" || exponent >= targetExponent) value = a;
        else
        {
            const int kept = digits.size() - (targetExponent - exponent);
            const bool up = kept >= 0 && digits.at(kept) >= '5';
            digits = kept > 0 ? digits.left(kept) : QByteArray("0");
            if (up)
            {
                int i = digits.size() - 1;
                while (i >= 0 && digits.at(i) == '9') digits[i--] = '0';
                if (i < 0) digits.prepend('1');
                else digits[i] = char(digits.at(i) + 1);
            }
            const QString literal = (std::signbit(a) ? QStringLiteral("-") : QString())
                + QString::fromLatin1(digits) + QLatin1Char('e') + QString::number(targetExponent);
            value = QLocale::c().toDouble(literal);
            // 有意舍入到零不属于下溢；非零量化值转换失败仍应报告。
            nonzero = digits != "0";
        }
        return finishBinary(value, sources, nonzero && value == 0, output);
    }
    return finishBinary(value, sources, nonzero || environment.underflow(), output);
}

QString NumericValue::text() const
{
    if (!m_isBinary) return formatDigits(m_decimal.coefficient(), m_decimal.exponent(), m_decimal.isNegative());
    QByteArray digits;
    int exponent;
    exactBinaryDigits(m_binary, digits, exponent);
    constexpr int precision = 17;
    if (digits.size() > precision)
    {
        const char first = digits.at(precision);
        bool remainder = false;
        for (int i = precision + 1; i < digits.size(); ++i) remainder |= digits.at(i) != '0';
        const bool roundUp = first > '5' || (first == '5' && (remainder || (digits.at(precision - 1) - '0') % 2));
        exponent += digits.size() - precision;
        digits.truncate(precision);
        if (roundUp)
        {
            int position = digits.size() - 1;
            while (position >= 0 && digits.at(position) == '9') digits[position--] = '0';
            if (position < 0) digits.prepend('1');
            else digits[position] = char(digits.at(position) + 1);
        }
    }
    return formatDigits(digits, exponent, std::signbit(m_binary));
}

QString NumericValue::integerText() const
{
    if (!isInteger()) return {};
    if (isZero()) return QStringLiteral("0");
    QByteArray digits;
    int exponent;
    if (m_isBinary) exactBinaryDigits(m_binary, digits, exponent);
    else { digits = m_decimal.coefficient(); exponent = m_decimal.exponent(); }
    digits.append(QByteArray(exponent, '0'));
    if (isNegative()) digits.prepend('-');
    return QString::fromLatin1(digits);
}

QString NumericValue::sourceText() const
{
    QStringList names;
    if (m_sources & Rounded) names.append(QStringLiteral("含除法舍入"));
    if (m_sources & Approximate) names.append(QStringLiteral("含近似计算"));
    if (m_sources & ConversionLoss) names.append(QStringLiteral("含转换损失"));
    return names.isEmpty() ? QString() : QStringLiteral("来源：") + names.join(QStringLiteral("；"));
}

QString NumericValue::binaryHex() const
{
    return m_isBinary ? QString::number(binaryBits(m_binary), 16).rightJustified(16, QLatin1Char('0')) : QString();
}

bool NumericValue::restoreDecimal(const QString &text, unsigned sources, NumericValue &output)
{
    if (sources & ~unsigned(Rounded) || text.size() > 58) return false;
    NumericValue restored;
    if (!parse(text, restored).ok() || restored.text() != text) return false;
    restored.m_sources = sources;
    output = restored;
    return true;
}

bool NumericValue::restoreBinary(const QString &hex, unsigned sources, NumericValue &output)
{
    if (hex.size() != 16 || !(sources & Approximate) || (sources & ~unsigned(Rounded | Approximate | ConversionLoss))) return false;
    for (const auto c : hex)
        if (!(c >= QLatin1Char('0') && c <= QLatin1Char('9')) && !(c >= QLatin1Char('a') && c <= QLatin1Char('f'))) return false;
    const quint64 bits = hex.toULongLong(nullptr, 16);
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    NumericValue restored;
    if (!fromBinary(value, restored).ok()) return false;
    restored.m_sources = sources;
    output = restored;
    return true;
}

bool NumericValue::operator==(const NumericValue &other) const
{
    if (m_isBinary != other.m_isBinary || m_sources != other.m_sources) return false;
    if (m_isBinary) return binaryBits(m_binary) == binaryBits(other.m_binary);
    return m_decimal.coefficient() == other.m_decimal.coefficient() && m_decimal.exponent() == other.m_decimal.exponent()
        && m_decimal.isNegative() == other.m_decimal.isNegative();
}
