#include "expression_engine.h"

#include <QVector>
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
    NumericValue value;
    SourceRange range;
};

class Parser
{
public:
    Parser(QString text, const NumericValue &answer, QVector<ExpressionParameter> *parameters = nullptr)
        : m_text(std::move(text)), m_answer(answer), m_parameters(parameters)
    {
        // 逐代码单元替换，错误范围始终对应原始公式。
        m_text.replace(QChar(0x00d7), QLatin1Char('*'));
        m_text.replace(QChar(0x00f7), QLatin1Char('/'));
        m_text.replace(QChar(0x2212), QLatin1Char('-'));
    }

    CalculationResult parse()
    {
        CalculationResult result;
        try
        {
            if (m_text.size() > 4096)
                fail(QStringLiteral("公式过长（最多 4096 个 UTF-16 代码单元）"), CalculationError::Limit, {4096, m_text.size() - 4096});
            skipSpace();
            if (m_position == m_text.size()) failHere(QStringLiteral("请输入公式"));
            const NumericValue value = expression();
            skipSpace();
            if (m_position != m_text.size()) failHere(QStringLiteral("此处需要运算符，或存在多余字符"));
            if (!m_parameters)
            {
                result.value = value;
                result.text = value.text();
            }
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
    NumericValue binary(char operation, const NumericValue &left, const NumericValue &right,
                        SourceRange leftRange, SourceRange rightRange, SourceRange operationRange) const
    {
        if (m_parameters) return {};
        NumericValue result;
        const auto status = NumericValue::operate(operation, left, right, result);
        if (status.error == NumericError::DivisionByZero && operation == '%')
            fail(QStringLiteral("取余的除数不能为 0"), CalculationError::DivisionByZero, rightRange);
        if (status.error == NumericError::Domain && operation == '^')
            fail(left.isZero() ? QStringLiteral("乘方的底数为 0 时，指数不能为负数")
                               : QStringLiteral("实数乘方中，负底数的指数必须为整数"), CalculationError::Domain, rightRange);
        numericCheck(status, status.argument == 0 ? leftRange : status.argument == 1 ? rightRange : operationRange);
        return result;
    }
    void numericCheck(NumericStatus status, SourceRange range) const
    {
        switch (status.error)
        {
        case NumericError::None: return;
        case NumericError::Syntax: fail(QStringLiteral("无效的十进制数值"), CalculationError::Syntax, range);
        case NumericError::PrecisionLimit: fail(QStringLiteral("十进制精度超限（最多 50 位规范化有效数字）"), CalculationError::Limit, range);
        case NumericError::ResourceLimit: fail(QStringLiteral("数值资源超限（整数幂指数绝对值最多 10000）"), CalculationError::Limit, range);
        case NumericError::Domain: fail(QStringLiteral("运算超出实数定义域"), CalculationError::Domain, range);
        case NumericError::DivisionByZero: fail(QStringLiteral("除数不能为 0"), CalculationError::DivisionByZero, range);
        case NumericError::Overflow: fail(QStringLiteral("运算数值上溢"), CalculationError::Overflow, range);
        case NumericError::Underflow: fail(QStringLiteral("运算数值下溢，非零结果不能变为 0"), CalculationError::Underflow, range);
        case NumericError::ConversionOverflow: fail(QStringLiteral("转换为 double 时数值上溢"), CalculationError::Overflow, range);
        case NumericError::ConversionUnderflow: fail(QStringLiteral("转换为 double 时非零数值下溢为 0"), CalculationError::Underflow, range);
        case NumericError::UnknownName: fail(QStringLiteral("未知函数或常量"), CalculationError::UnknownName, range);
        case NumericError::ArgumentCount: fail(QStringLiteral("函数参数数量不正确"), CalculationError::ArgumentCount, range);
        }
    }
    NumericValue expression(int minimumPrecedence = 1)
    {
        skipSpace();
        const int start = m_position;
        NumericValue value = unary();
        for (;;)
        {
            const SourceRange leftRange = rangeFrom(start);
            skipSpace();
            const SourceRange operation{m_position, 1};
            const char symbol = m_position < m_text.size() ? m_text.at(m_position).toLatin1() : '\0';
            const int precedence = symbol == '+' || symbol == '-' ? 1
                : symbol == '*' || symbol == '/' || symbol == '%' ? 2 : 0;
            if (precedence < minimumPrecedence) return value;
            ++m_position;
            skipSpace();
            const int rightStart = m_position;
            const NumericValue right = expression(precedence + 1);
            value = binary(symbol, value, right, leftRange, rangeFrom(rightStart), operation);
        }
    }
    NumericValue unary()
    {
        skipSpace();
        if (++m_depth > 128) failHere(QStringLiteral("公式嵌套过深"), CalculationError::Limit);
        NumericValue value;
        if (take(QLatin1Char('+'))) value = unary();
        else if (take(QLatin1Char('-')))
        {
            value = unary();
            value = value.negated();
        }
        else value = power();
        --m_depth;
        return value;
    }
    NumericValue power()
    {
        skipSpace();
        const int baseStart = m_position;
        const NumericValue value = primary();
        const SourceRange baseRange = rangeFrom(baseStart);
        if (!take(QLatin1Char('^'))) return value;
        const SourceRange operation{m_position - 1, 1};
        skipSpace();
        const int start = m_position;
        const NumericValue exponent = unary();
        return binary('^', value, exponent, baseRange, rangeFrom(start), operation);
    }
    NumericValue primary()
    {
        if (take(QLatin1Char('(')))
        {
            const NumericValue value = expression();
            if (!take(QLatin1Char(')'))) failHere(QStringLiteral("缺少右括号 )"));
            return value;
        }
        skipSpace();
        const int start = m_position;
        if (m_position < m_text.size() && (m_parameters ? identifierStart(m_text.at(m_position)) : m_text.at(m_position).isLetter()))
        {
            while (m_position < m_text.size() && ((m_parameters
                ? identifierStart(m_text.at(m_position)) : m_text.at(m_position).isLetter())
                || isDigit(m_text.at(m_position)))) ++m_position;
            const SourceRange nameRange{start, m_position - start};
            const QString name = m_text.mid(start, nameRange.length).toLower();
            if (name == QStringLiteral("pi") || name == QStringLiteral("e"))
            {
                if (m_parameters) return {};
                NumericValue result;
                numericCheck(NumericValue::constant(name, result), nameRange);
                return result;
            }
            if (name == QStringLiteral("ans")) return m_answer;
            const int minimum = NumericValue::minimumArgumentCount(name);
            const int maximum = NumericValue::maximumArgumentCount(name);
            skipSpace();
            const bool call = m_position < m_text.size() && m_text.at(m_position) == QLatin1Char('(');
            // 新增函数仅在调用位置占用名称，既有会话中的同名参数仍可恢复。
            if (m_parameters && !call && !NumericValue::isLegacyFunction(name))
            {
                m_parameters->append({m_text.mid(start, nameRange.length), start, nameRange.length});
                return {};
            }
            if (minimum == 0)
                fail(QStringLiteral("未知函数或常量：%1").arg(name), CalculationError::UnknownName, nameRange);
            if (!take(QLatin1Char('('))) failHere(QStringLiteral("%1 函数缺少左括号 (").arg(name));
            const QString expected = minimum == maximum ? QStringLiteral("%1 个参数").arg(minimum)
                : QStringLiteral("%1 至 %2 个参数").arg(minimum).arg(maximum);
            QVector<Argument> arguments;
            if (!take(QLatin1Char(')')))
            {
                do
                {
                    skipSpace();
                    if (arguments.size() >= maximum)
                        failHere(QStringLiteral("%1 需要 %2，参数过多").arg(name, expected), CalculationError::ArgumentCount);
                    if (m_position == m_text.size() || m_text.at(m_position) == QLatin1Char(',') || m_text.at(m_position) == QLatin1Char(')'))
                        failHere(QStringLiteral("%1 缺少第 %2 个参数").arg(name).arg(arguments.size() + 1), CalculationError::ArgumentCount);
                    const int argumentStart = m_position;
                    const NumericValue value = expression();
                    arguments.append({value, rangeFrom(argumentStart)});
                } while (take(QLatin1Char(',')));
                if (!take(QLatin1Char(')'))) failHere(QStringLiteral("%1 函数缺少右括号 )").arg(name));
            }
            if (arguments.size() < minimum)
                fail(QStringLiteral("%1 需要 %2，实际为 %3 个").arg(name, expected).arg(arguments.size()),
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
        if (m_parameters) return {};
        const SourceRange number{start, m_position - start};
        NumericValue result;
        const auto status = NumericValue::parse(m_text.mid(start, number.length), result);
        if (status.error == NumericError::Overflow)
            fail(QStringLiteral("数值字面量过大，规范化指数最多 999"), CalculationError::Overflow, number);
        if (status.error == NumericError::Underflow)
            fail(QStringLiteral("数值字面量过小，规范化指数至少 -999，不能下溢为 0"), CalculationError::Underflow, number);
        numericCheck(status, number);
        return result;
    }
    static bool isDigit(QChar ch) { return ch >= QLatin1Char('0') && ch <= QLatin1Char('9'); }
    NumericValue function(const QString &name, const QVector<Argument> &args, SourceRange nameRange) const
    {
        if (m_parameters) return {};
        if (name == QStringLiteral("pow")) return binary('^', args[0].value, args[1].value, args[0].range, args[1].range, nameRange);
        QVector<NumericValue> values;
        for (const auto &argument : args) values.append(argument.value);
        NumericValue result;
        const auto status = NumericValue::function(name, values, result);
        if (status.error == NumericError::ResourceLimit && name == QStringLiteral("round") && args.size() == 2)
            fail(QStringLiteral("round(x,n) 的小数位数超限（-999 至 999）"), CalculationError::Limit, args.at(1).range);
        if (status.error == NumericError::Domain && name == QStringLiteral("sqrt"))
            fail(QStringLiteral("sqrt(x) 的参数必须大于或等于 0"), CalculationError::Domain, args.first().range);
        if (status.error == NumericError::Domain && name == QStringLiteral("round") && args.size() == 2)
            fail(QStringLiteral("round(x,n) 的小数位数 n 必须是 -999 至 999 的整数"), CalculationError::Domain, args.at(1).range);
        if (status.error == NumericError::Domain && name == QStringLiteral("log") && status.argument == 1)
            fail(QStringLiteral("log(x,base) 的底数必须大于 0 且不能等于 1"), CalculationError::Domain, args.at(1).range);
        if (status.error == NumericError::Domain && (name == QStringLiteral("ln") || name == QStringLiteral("log") || name == QStringLiteral("log2")))
            fail(QStringLiteral("%1(x) 的参数必须大于 0；0 和负数没有实数对数").arg(name), CalculationError::Domain, args.first().range);
        if (status.error == NumericError::Domain && (name == QStringLiteral("asin") || name == QStringLiteral("acos")))
            fail(QStringLiteral("%1(x) 的参数必须在 -1 至 1 之间").arg(name), CalculationError::Domain, args.first().range);
        if (status.error == NumericError::Domain && name == QStringLiteral("atan2"))
            fail(QStringLiteral("atan2(y,x) 的两个参数不能同时为 0"), CalculationError::Domain, nameRange);
        numericCheck(status, status.argument >= 0 ? args.at(status.argument).range : nameRange);
        return result;
    }

    QString m_text;
    NumericValue m_answer;
    QVector<ExpressionParameter> *m_parameters;
    int m_position = 0;
    int m_depth = 0;
};
}

CalculationResult ExpressionEngine::evaluate(const QString &expression, const NumericValue &answer)
{
    return Parser(expression, answer).parse();
}

CalculationResult ExpressionEngine::inspect(const QString &expression, QVector<ExpressionParameter> &parameters)
{
    QVector<ExpressionParameter> found;
    const auto result = Parser(expression, NumericValue(), &found).parse();
    if (result.ok) parameters = std::move(found);
    return result;
}

bool ExpressionEngine::isReservedName(const QString &name)
{
    const QString lower = name.toLower();
    // 保留原有名称约束；扩展函数通过括号区分，避免使旧定义或参数失效。
    return NumericValue::isLegacyFunction(lower) || lower == QStringLiteral("pi")
        || lower == QStringLiteral("e") || lower == QStringLiteral("ans");
}
