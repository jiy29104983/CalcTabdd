#include "expression_engine.h"

#include <QLocale>
#include <QVector>
#include <cmath>
#include <stdexcept>

namespace {
struct ParseError
{
    QString text;
    int position;
};

class Parser
{
public:
    Parser(QString text, double answer) : m_text(std::move(text)), m_answer(answer)
    {
        m_text.replace(QChar(0x00d7), QLatin1Char('*'));
        m_text.replace(QChar(0x00f7), QLatin1Char('/'));
        m_text.replace(QChar(0x2212), QLatin1Char('-'));
    }

    double parse()
    {
        if (m_text.size() > 4096)
            fail(QStringLiteral("公式过长（最多 4096 个字符）"));
        if (m_text.trimmed().isEmpty())
            fail(QStringLiteral("请输入公式"));
        const double value = expression();
        skipSpace();
        if (m_position != m_text.size())
            fail(QStringLiteral("此处需要运算符，或存在多余字符"));
        return finite(value);
    }
    int position() const { return m_position; }

private:
    [[noreturn]] void fail(const QString &text) const
    {
        throw ParseError{text, m_position};
    }
    double finite(double value) const
    {
        if (std::isnan(value))
            fail(QStringLiteral("运算超出实数定义域"));
        if (!std::isfinite(value))
            fail(QStringLiteral("数值溢出"));
        return value;
    }
    void skipSpace()
    {
        while (m_position < m_text.size() && m_text.at(m_position).isSpace())
            ++m_position;
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
            if (take(QLatin1Char('+'))) value = finite(value + term());
            else if (take(QLatin1Char('-'))) value = finite(value - term());
            else return value;
        }
    }
    double term()
    {
        double value = unary();
        for (;;)
        {
            if (take(QLatin1Char('*'))) value = finite(value * unary());
            else if (take(QLatin1Char('/')))
            {
                const double divisor = unary();
                if (divisor == 0) fail(QStringLiteral("除数不能为 0"));
                value = finite(value / divisor);
            }
            else if (take(QLatin1Char('%')))
            {
                const double divisor = unary();
                if (divisor == 0) fail(QStringLiteral("取余的除数不能为 0"));
                value = finite(std::fmod(value, divisor));
            }
            else return value;
        }
    }
    double unary()
    {
        if (++m_depth > 128) fail(QStringLiteral("公式嵌套过深"));
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
        if (take(QLatin1Char('^'))) return finite(std::pow(value, unary()));
        return value;
    }
    double primary()
    {
        if (take(QLatin1Char('(')))
        {
            const double value = expression();
            if (!take(QLatin1Char(')'))) fail(QStringLiteral("缺少右括号 )"));
            return value;
        }
        skipSpace();
        const int start = m_position;
        if (m_position < m_text.size() && m_text.at(m_position).isLetter())
        {
            while (m_position < m_text.size() && m_text.at(m_position).isLetter()) ++m_position;
            const QString name = m_text.mid(start, m_position - start).toLower();
            if (name == QStringLiteral("pi")) return std::acos(-1.0);
            if (name == QStringLiteral("e")) return std::exp(1.0);
            if (name == QStringLiteral("ans")) return m_answer;
            if (!take(QLatin1Char('('))) fail(QStringLiteral("未知常量，或函数缺少括号"));
            QVector<double> arguments;
            if (!take(QLatin1Char(')')))
            {
                do
                {
                    if (arguments.size() >= 2) fail(QStringLiteral("函数最多接受两个参数"));
                    arguments.append(expression());
                } while (take(QLatin1Char(',')));
                if (!take(QLatin1Char(')'))) fail(QStringLiteral("函数缺少右括号 )"));
            }
            return function(name, arguments);
        }
        bool digits = false;
        while (m_position < m_text.size() && isDigit(m_text.at(m_position))) { digits = true; ++m_position; }
        if (m_position < m_text.size() && m_text.at(m_position) == QLatin1Char('.'))
        {
            ++m_position;
            while (m_position < m_text.size() && isDigit(m_text.at(m_position))) { digits = true; ++m_position; }
        }
        if (!digits) fail(QStringLiteral("此处需要数字、常量或函数"));
        if (m_position < m_text.size() && m_text.at(m_position).toLower() == QLatin1Char('e'))
        {
            ++m_position;
            if (m_position < m_text.size() && (m_text.at(m_position) == QLatin1Char('+') || m_text.at(m_position) == QLatin1Char('-'))) ++m_position;
            const int exponent = m_position;
            while (m_position < m_text.size() && isDigit(m_text.at(m_position))) ++m_position;
            if (exponent == m_position) fail(QStringLiteral("科学计数法缺少指数"));
        }
        bool ok = false;
        const double value = QLocale::c().toDouble(m_text.mid(start, m_position - start), &ok);
        if (!ok) fail(QStringLiteral("数值超出支持范围"));
        return finite(value);
    }
    static bool isDigit(QChar ch) { return ch >= QLatin1Char('0') && ch <= QLatin1Char('9'); }
    double function(const QString &name, const QVector<double> &args)
    {
        if (name == QStringLiteral("min") || name == QStringLiteral("max") || name == QStringLiteral("pow"))
        {
            if (args.size() != 2) fail(QStringLiteral("该函数需要两个参数"));
            if (name == QStringLiteral("min")) return qMin(args[0], args[1]);
            if (name == QStringLiteral("max")) return qMax(args[0], args[1]);
            return finite(std::pow(args[0], args[1]));
        }
        if (args.size() != 1) fail(QStringLiteral("该函数需要一个参数"));
        const double value = args.first();
        if (name == QStringLiteral("sqrt")) return finite(std::sqrt(value));
        if (name == QStringLiteral("abs")) return std::abs(value);
        if (name == QStringLiteral("sin")) return finite(std::sin(value));
        if (name == QStringLiteral("cos")) return finite(std::cos(value));
        if (name == QStringLiteral("tan")) return finite(std::tan(value));
        if (name == QStringLiteral("ln")) return finite(std::log(value));
        if (name == QStringLiteral("log")) return finite(std::log10(value));
        if (name == QStringLiteral("exp")) return finite(std::exp(value));
        if (name == QStringLiteral("floor")) return std::floor(value);
        if (name == QStringLiteral("ceil")) return std::ceil(value);
        if (name == QStringLiteral("round")) return std::round(value);
        fail(QStringLiteral("未知函数：%1").arg(name));
    }

    QString m_text;
    double m_answer;
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
        result.errorPosition = error.position;
    }
    return result;
}
