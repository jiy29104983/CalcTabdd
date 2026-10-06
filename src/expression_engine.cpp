#include "expression_engine.h"

#include <QLocale>
#include <QVector>
#include <cmath>
#include <utility>

namespace {
struct SourceRange
{
    int start;
    int length;
};

struct ParseError
{
    QString text;
    CalculationError kind;
    SourceRange range;
};

struct Argument
{
    double value;
    SourceRange range;
};

class Parser
{
public:
    Parser(QString text, double answer, QVector<ExpressionParameter> *parameters = nullptr)
        : m_text(std::move(text)), m_answer(answer), m_parameters(parameters)
    {
        // 逐代码单元替换，错误范围始终对应原始公式。
        m_text.replace(QChar(0x00d7), QLatin1Char('*'));
        m_text.replace(QChar(0x00f7), QLatin1Char('/'));
        m_text.replace(QChar(0x2212), QLatin1Char('-'));
    }

    double parse()
    {
        if (m_text.size() > 4096)
            fail(QStringLiteral("公式过长（最多 4096 个 UTF-16 代码单元）"), CalculationError::Limit, {4096, m_text.size() - 4096});
        skipSpace();
        if (m_position == m_text.size())
            failHere(QStringLiteral("请输入公式"));
        const double value = expression();
        skipSpace();
        if (m_position != m_text.size())
            failHere(QStringLiteral("此处需要运算符，或存在多余字符"));
        return value;
    }

    static bool reserved(const QString &name)
    {
        const QString lower = name.toLower();
        return argumentCount(lower) != 0 || lower == QStringLiteral("pi")
            || lower == QStringLiteral("e") || lower == QStringLiteral("ans");
    }

private:
    static bool identifierStart(QChar ch)
    {
        return (ch >= QLatin1Char('a') && ch <= QLatin1Char('z'))
            || (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) || ch == QLatin1Char('_');
    }
    [[noreturn]] void fail(const QString &text, CalculationError kind, SourceRange range) const
    {
        // 不截断 Unicode 代理对；全部公开范围均在原公式内。
        int start = qBound(0, range.start, m_text.size());
        int end = qBound(start, range.start + range.length, m_text.size());
        if (start > 0 && start < m_text.size() && m_text.at(start).isLowSurrogate() && m_text.at(start - 1).isHighSurrogate()) --start;
        if (end > 0 && end < m_text.size() && m_text.at(end - 1).isHighSurrogate() && m_text.at(end).isLowSurrogate()) ++end;
        throw ParseError{text, kind, {start, end - start}};
    }
    [[noreturn]] void failHere(const QString &text, CalculationError kind = CalculationError::Syntax) const
    {
        fail(text, kind, {m_position, m_position < m_text.size() ? 1 : 0});
    }
    SourceRange rangeFrom(int start) const
    {
        int end = m_position;
        while (end > start && m_text.at(end - 1).isSpace()) --end;
        return {start, end - start};
    }
    double finite(double value, SourceRange range) const
    {
        if (m_parameters) return 1;
        if (std::isnan(value))
            fail(QStringLiteral("运算超出实数定义域"), CalculationError::Domain, range);
        if (!std::isfinite(value))
            fail(QStringLiteral("数值溢出，结果超出 double 可表示范围"), CalculationError::Overflow, range);
        return value;
    }
    void skipSpace()
    {
        while (m_position < m_text.size() && m_text.at(m_position).isSpace()) ++m_position;
    }
    bool take(QChar character)
    {
        skipSpace();
        if (m_position < m_text.size() && m_text.at(m_position) == character)
        {
            ++m_position;
            return true;
        }
        return false;
    }
    double expression()
    {
        double value = term();
        for (;;)
        {
            skipSpace();
            const SourceRange operation{m_position, 1};
            if (take(QLatin1Char('+')))
            {
                const double right = term();
                value = m_parameters ? 1 : finite(value + right, operation);
            }
            else if (take(QLatin1Char('-')))
            {
                const double right = term();
                value = m_parameters ? 1 : finite(value - right, operation);
            }
            else return value;
        }
    }
    double term()
    {
        double value = unary();
        for (;;)
        {
            skipSpace();
            const SourceRange operation{m_position, 1};
            if (take(QLatin1Char('*')))
            {
                const double right = unary();
                value = m_parameters ? 1 : finite(value * right, operation);
            }
            else if (take(QLatin1Char('/')))
            {
                skipSpace();
                const int start = m_position;
                const double divisor = unary();
                if (!m_parameters && divisor == 0)
                    fail(QStringLiteral("除数不能为 0"), CalculationError::DivisionByZero, rangeFrom(start));
                value = m_parameters ? 1 : finite(value / divisor, operation);
            }
            else if (take(QLatin1Char('%')))
            {
                skipSpace();
                const int start = m_position;
                const double divisor = unary();
                if (!m_parameters && divisor == 0)
                    fail(QStringLiteral("取余的除数不能为 0"), CalculationError::DivisionByZero, rangeFrom(start));
                value = m_parameters ? 1 : finite(std::fmod(value, divisor), operation);
            }
            else return value;
        }
    }
    double unary()
    {
        skipSpace();
        if (++m_depth > 128) failHere(QStringLiteral("公式嵌套过深"), CalculationError::Limit);
        double value;
        if (take(QLatin1Char('+'))) value = unary();
        else if (take(QLatin1Char('-'))) value = -unary();
        else value = power();
        --m_depth;
        return value;
    }
    double power()
    {
        const double value = primary();
        if (!take(QLatin1Char('^'))) return value;
        const SourceRange operation{m_position - 1, 1};
        skipSpace();
        const int start = m_position;
        const double exponent = unary();
        return powerValue(value, exponent, rangeFrom(start), operation);
    }
    double powerValue(double base, double exponent, SourceRange exponentRange, SourceRange operation) const
    {
        if (m_parameters) return 1;
        if (base == 0 && exponent < 0)
            fail(QStringLiteral("乘方的底数为 0 时，指数不能为负数"), CalculationError::Domain, exponentRange);
        if (base < 0 && std::trunc(exponent) != exponent)
            fail(QStringLiteral("实数乘方中，负底数的指数必须为整数"), CalculationError::Domain, exponentRange);
        return finite(std::pow(base, exponent), operation);
    }
    double primary()
    {
        if (take(QLatin1Char('(')))
        {
            const double value = expression();
            if (!take(QLatin1Char(')'))) failHere(QStringLiteral("缺少右括号 )"));
            return value;
        }
        skipSpace();
        const int start = m_position;
        if (m_position < m_text.size() && (m_parameters ? identifierStart(m_text.at(m_position)) : m_text.at(m_position).isLetter()))
        {
            while (m_position < m_text.size() && (m_parameters
                ? identifierStart(m_text.at(m_position)) || isDigit(m_text.at(m_position))
                : m_text.at(m_position).isLetter())) ++m_position;
            const SourceRange nameRange{start, m_position - start};
            const QString name = m_text.mid(start, nameRange.length).toLower();
            if (name == QStringLiteral("pi")) return m_parameters ? 1 : std::acos(-1.0);
            if (name == QStringLiteral("e")) return m_parameters ? 1 : std::exp(1.0);
            if (name == QStringLiteral("ans")) return finite(m_answer, nameRange);
            const int count = argumentCount(name);
            if (count == 0 && m_parameters)
            {
                skipSpace();
                if (m_position < m_text.size() && m_text.at(m_position) == QLatin1Char('('))
                    fail(QStringLiteral("未知函数：%1").arg(name), CalculationError::UnknownName, nameRange);
                m_parameters->append({m_text.mid(start, nameRange.length), start, nameRange.length});
                return 1;
            }
            if (count == 0)
                fail(QStringLiteral("未知函数或常量：%1").arg(name), CalculationError::UnknownName, nameRange);
            if (!take(QLatin1Char('('))) failHere(QStringLiteral("%1 函数缺少左括号 (").arg(name));
            QVector<Argument> arguments;
            if (!take(QLatin1Char(')')))
            {
                do
                {
                    skipSpace();
                    if (arguments.size() >= count)
                        failHere(QStringLiteral("%1 需要 %2 个参数，参数过多").arg(name).arg(count), CalculationError::ArgumentCount);
                    if (m_position == m_text.size() || m_text.at(m_position) == QLatin1Char(',') || m_text.at(m_position) == QLatin1Char(')'))
                        failHere(QStringLiteral("%1 缺少第 %2 个参数").arg(name).arg(arguments.size() + 1), CalculationError::ArgumentCount);
                    const int argumentStart = m_position;
                    const double value = expression();
                    arguments.append({value, rangeFrom(argumentStart)});
                } while (take(QLatin1Char(',')));
                if (!take(QLatin1Char(')'))) failHere(QStringLiteral("%1 函数缺少右括号 )").arg(name));
            }
            if (arguments.size() != count)
                fail(QStringLiteral("%1 需要 %2 个参数，实际为 %3 个").arg(name).arg(count).arg(arguments.size()),
                     CalculationError::ArgumentCount, {m_position - 1, 1});
            return function(name, arguments, nameRange);
        }
        bool digits = false;
        while (m_position < m_text.size() && isDigit(m_text.at(m_position))) { digits = true; ++m_position; }
        if (m_position < m_text.size() && m_text.at(m_position) == QLatin1Char('.'))
        {
            ++m_position;
            while (m_position < m_text.size() && isDigit(m_text.at(m_position))) { digits = true; ++m_position; }
        }
        if (!digits)
            fail(QStringLiteral("此处需要数字、常量或函数"), CalculationError::Syntax, {start, start < m_text.size() ? 1 : 0});
        if (m_position < m_text.size() && m_text.at(m_position).toLower() == QLatin1Char('e'))
        {
            const int exponentStart = m_position++;
            if (m_position < m_text.size() && (m_text.at(m_position) == QLatin1Char('+') || m_text.at(m_position) == QLatin1Char('-'))) ++m_position;
            const int exponent = m_position;
            while (m_position < m_text.size() && isDigit(m_text.at(m_position))) ++m_position;
            if (exponent == m_position)
                fail(QStringLiteral("科学计数法缺少指数"), CalculationError::Syntax, {exponentStart, m_position - exponentStart});
        }
        if (m_parameters) return 1;
        bool ok = false;
        const SourceRange number{start, m_position - start};
        const double value = QLocale::c().toDouble(m_text.mid(start, number.length), &ok);
        if (!ok)
        {
            // 数字语法已在上面验证；Qt 转换溢出返回无穷，下溢返回 0。
            if (value == 0)
                fail(QStringLiteral("数值字面量过小，转换为 double 时下溢为 0"), CalculationError::Underflow, number);
            fail(QStringLiteral("数值字面量过大，超出 double 可表示范围"), CalculationError::Overflow, number);
        }
        return finite(value, number);
    }
    static bool isDigit(QChar ch) { return ch >= QLatin1Char('0') && ch <= QLatin1Char('9'); }
    static int argumentCount(const QString &name)
    {
        if (name == QStringLiteral("min") || name == QStringLiteral("max") || name == QStringLiteral("pow")) return 2;
        for (const auto *function : {"sqrt", "abs", "sin", "cos", "tan", "ln", "log", "exp", "floor", "ceil", "round"})
            if (name == QLatin1String(function)) return 1;
        return 0;
    }
    double function(const QString &name, const QVector<Argument> &args, SourceRange nameRange) const
    {
        if (m_parameters) return 1;
        if (name == QStringLiteral("min")) return qMin(args[0].value, args[1].value);
        if (name == QStringLiteral("max")) return qMax(args[0].value, args[1].value);
        if (name == QStringLiteral("pow")) return powerValue(args[0].value, args[1].value, args[1].range, nameRange);
        const double value = args.first().value;
        const SourceRange range = args.first().range;
        if (name == QStringLiteral("sqrt"))
        {
            if (value < 0) fail(QStringLiteral("sqrt(x) 的参数必须大于或等于 0"), CalculationError::Domain, range);
            return finite(std::sqrt(value), nameRange);
        }
        if (name == QStringLiteral("ln") || name == QStringLiteral("log"))
        {
            if (value <= 0) fail(QStringLiteral("%1(x) 的参数必须大于 0；0 和负数没有实数对数").arg(name), CalculationError::Domain, range);
            return finite(name == QStringLiteral("ln") ? std::log(value) : std::log10(value), nameRange);
        }
        if (name == QStringLiteral("abs")) return std::abs(value);
        if (name == QStringLiteral("sin")) return finite(std::sin(value), nameRange);
        if (name == QStringLiteral("cos")) return finite(std::cos(value), nameRange);
        if (name == QStringLiteral("tan")) return finite(std::tan(value), nameRange);
        if (name == QStringLiteral("exp")) return finite(std::exp(value), nameRange);
        if (name == QStringLiteral("floor")) return std::floor(value);
        if (name == QStringLiteral("ceil")) return std::ceil(value);
        if (name == QStringLiteral("round")) return std::round(value);
        fail(QStringLiteral("未知函数：%1").arg(name), CalculationError::UnknownName, nameRange);
    }

    QString m_text;
    double m_answer;
    QVector<ExpressionParameter> *m_parameters;
    int m_position = 0;
    int m_depth = 0;
};
}

CalculationResult ExpressionEngine::evaluate(const QString &expression, double answer)
{
    CalculationResult result;
    try
    {
        Parser parser(expression, answer);
        result.value = parser.parse();
        result.ok = true;
        result.text = QString::number(result.value == 0 ? 0 : result.value, 'g', 15);
    }
    catch (const ParseError &error)
    {
        result.text = error.text;
        result.error = error.kind;
        result.errorPosition = error.range.start;
        result.errorLength = error.range.length;
    }
    return result;
}

CalculationResult ExpressionEngine::inspect(const QString &expression, QVector<ExpressionParameter> &parameters)
{
    CalculationResult result;
    QVector<ExpressionParameter> found;
    try
    {
        Parser parser(expression, 0, &found);
        parser.parse();
        parameters = found;
        result.ok = true;
    }
    catch (const ParseError &error)
    {
        result.text = error.text;
        result.error = error.kind;
        result.errorPosition = error.range.start;
        result.errorLength = error.range.length;
    }
    return result;
}

bool ExpressionEngine::isReservedName(const QString &name)
{
    return Parser::reserved(name);
}
