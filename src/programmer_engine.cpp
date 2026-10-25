#include "programmer_engine.h"

#include <limits>

namespace {
struct SourceRange { int start; int length; };
struct ParseError { QString text; CalculationError kind; SourceRange range; };
struct Operand
{
    ProgrammerValue value;
    SourceRange range;
    // 正十进制的最小负数幅值只能被直接的一元负号消费；括号可以原样传递它。
    bool minimumLiteral = false;
    SourceRange literalRange{0, 0};
};
struct Operator { IntegerBinary kind; int precedence; int length; };

class Parser
{
public:
    Parser(const QString &text, ProgrammerConfig config, const ProgrammerValue &answer)
        : m_text(text), m_config(config), m_answer(answer) {}

    ProgrammerResult parse()
    {
        ProgrammerResult result;
        try
        {
            if (!m_config.isValid())
                fail(QStringLiteral("位宽必须为 8、16、32 或 64"), CalculationError::Domain, {0, 0});
            if (m_text.size() > ProgrammerEngine::maximumLength)
                fail(QStringLiteral("公式过长（最多 4096 个 UTF-16 代码单元）"), CalculationError::Limit,
                     {ProgrammerEngine::maximumLength, m_text.size() - ProgrammerEngine::maximumLength});
            skipSpace();
            if (m_position == m_text.size()) failHere(QStringLiteral("请输入整数公式"));
            const Operand value = expression();
            checkLiteral(value);
            skipSpace();
            if (m_position != m_text.size()) failHere(QStringLiteral("此处需要运算符，或存在多余字符"));
            result.value = value.value;
            result.text = value.value.text();
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
    [[noreturn]] void fail(const QString &text, CalculationError kind, SourceRange range) const
    {
        int start = qBound(0, range.start, m_text.size());
        int end = qBound(start, range.start + range.length, m_text.size());
        if (start > 0 && start < m_text.size() && m_text.at(start).isLowSurrogate()
            && m_text.at(start - 1).isHighSurrogate()) --start;
        if (end > 0 && end < m_text.size() && m_text.at(end - 1).isHighSurrogate()
            && m_text.at(end).isLowSurrogate()) ++end;
        throw ParseError{text, kind, {start, end - start}};
    }
    [[noreturn]] void failHere(const QString &text, CalculationError kind = CalculationError::Syntax) const
    {
        fail(text, kind, {m_position, m_position < m_text.size() ? 1 : 0});
    }
    void skipSpace()
    {
        while (m_position < m_text.size() && m_text.at(m_position).isSpace()) ++m_position;
    }
    QChar current() const { return m_position < m_text.size() ? m_text.at(m_position) : QChar(); }
    SourceRange rangeFrom(int start) const
    {
        int end = m_position;
        while (end > start && m_text.at(end - 1).isSpace()) --end;
        return {start, end - start};
    }
    static int digit(QChar ch)
    {
        if (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) return ch.unicode() - '0';
        if (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) return ch.unicode() - 'a' + 10;
        if (ch >= QLatin1Char('A') && ch <= QLatin1Char('F')) return ch.unicode() - 'A' + 10;
        return -1;
    }
    void enter()
    {
        if (++m_depth > ProgrammerEngine::maximumDepth)
            failHere(QStringLiteral("公式嵌套过深（最多 128 层）"), CalculationError::Limit);
    }
    void checkLiteral(const Operand &value) const
    {
        if (value.minimumLiteral)
            fail(QStringLiteral("十进制字面量超出当前有符号数值范围"), CalculationError::Overflow, value.literalRange);
    }
    Operator nextOperator() const
    {
        const QString tail = m_text.mid(m_position, 3);
        if (tail.startsWith(QStringLiteral("**")))
            fail(QStringLiteral("程序员模式不支持乘方；^ 表示异或"), CalculationError::Syntax, {m_position, 2});
        for (const QString &unsupported : {QStringLiteral("&&"), QStringLiteral("||"), QStringLiteral("=="),
                                         QStringLiteral("!="), QStringLiteral("<="), QStringLiteral(">=")})
            if (tail.startsWith(unsupported))
                fail(QStringLiteral("程序员模式不支持比较、布尔逻辑或赋值"), CalculationError::Syntax, {m_position, 2});
        if (tail.startsWith(QStringLiteral(">>>"))) return {IntegerBinary::LogicalRightShift, 4, 3};
        if (tail.startsWith(QStringLiteral(">>"))) return {IntegerBinary::RightShift, 4, 2};
        if (tail.startsWith(QStringLiteral("<<"))) return {IntegerBinary::LeftShift, 4, 2};
        switch (current().unicode())
        {
        case '|': return {IntegerBinary::Or, 1, 1};
        case '^': return {IntegerBinary::Xor, 2, 1};
        case '&': return {IntegerBinary::And, 3, 1};
        case '+': return {IntegerBinary::Add, 5, 1};
        case '-': return {IntegerBinary::Subtract, 5, 1};
        case '*': return {IntegerBinary::Multiply, 6, 1};
        case '/': return {IntegerBinary::Divide, 6, 1};
        case '%': return {IntegerBinary::Remainder, 6, 1};
        case '<': case '>': case '=': case '!':
            failHere(QStringLiteral("程序员模式不支持比较、布尔逻辑或赋值"));
        default: return {IntegerBinary::Add, 0, 0};
        }
    }
    Operand expression(int minimumPrecedence = 1)
    {
        skipSpace();
        const int start = m_position;
        Operand left = unary();
        for (;;)
        {
            skipSpace();
            const Operator operation = nextOperator();
            if (operation.precedence < minimumPrecedence) return left;
            checkLiteral(left);
            m_position += operation.length;
            const Operand right = expression(operation.precedence + 1);
            checkLiteral(right);
            ProgrammerValue value;
            const auto status = ProgrammerValue::operate(operation.kind, left.value, right.value, value);
            if (status == ProgrammerError::DivisionByZero)
                fail(QStringLiteral("除数不能为 0"), CalculationError::DivisionByZero, right.range);
            if (status == ProgrammerError::ShiftRange)
                fail(QStringLiteral("移位量必须在 0 至 %1 之间").arg(m_config.bitWidth - 1),
                     CalculationError::Domain, right.range);
            left = {value, rangeFrom(start)};
        }
    }
    Operand unary()
    {
        skipSpace();
        const int start = m_position;
        const QChar symbol = current();
        if (symbol == QLatin1Char('+') || symbol == QLatin1Char('-') || symbol == QLatin1Char('~'))
        {
            enter();
            ++m_position;
            Operand value = unary();
            --m_depth;
            if (symbol == QLatin1Char('-') && value.minimumLiteral)
                value.minimumLiteral = false;
            else
            {
                checkLiteral(value);
                const auto operation = symbol == QLatin1Char('+') ? IntegerUnary::Plus
                    : symbol == QLatin1Char('-') ? IntegerUnary::Negate : IntegerUnary::Complement;
                ProgrammerValue::operate(operation, value.value, value.value);
            }
            value.range = rangeFrom(start);
            return value;
        }
        return primary();
    }
    Operand primary()
    {
        skipSpace();
        const int start = m_position;
        if (current() == QLatin1Char('('))
        {
            enter();
            ++m_position;
            Operand value = expression();
            skipSpace();
            if (current() != QLatin1Char(')')) failHere(QStringLiteral("缺少右括号 )"));
            ++m_position;
            --m_depth;
            value.range = rangeFrom(start);
            return value;
        }
        if (current() == QLatin1Char('_'))
            failHere(QStringLiteral("_ 只能位于两个合法进制数字之间"));
        if (current().isLetter())
        {
            while (current().isLetterOrNumber() || current() == QLatin1Char('_')) ++m_position;
            const SourceRange range = rangeFrom(start);
            const QString name = m_text.mid(start, range.length);
            if (name.compare(QStringLiteral("ans"), Qt::CaseInsensitive) != 0)
                fail(QStringLiteral("程序员模式仅支持 ans，不支持函数或常量：%1").arg(name),
                     CalculationError::UnknownName, range);
            ProgrammerValue converted;
            if (m_answer.converted(m_config, converted) != ProgrammerError::None)
                fail(QStringLiteral("ans 无法在当前位宽与符号设置下保持原数值"),
                     CalculationError::Overflow, range);
            return {converted, range};
        }
        if (current() == QLatin1Char('!')) failHere(QStringLiteral("程序员模式不支持布尔逻辑"));
        if (current() < QLatin1Char('0') || current() > QLatin1Char('9'))
            failHere(QStringLiteral("此处需要整数、ans 或左括号"));
        return number();
    }
    Operand number()
    {
        const int start = m_position;
        int base = 10;
        if (current() == QLatin1Char('0') && m_position + 1 < m_text.size())
        {
            switch (m_text.at(m_position + 1).toLower().unicode())
            {
            case 'x': base = 16; break;
            case 'b': base = 2; break;
            case 'o': base = 8; break;
            default: break;
            }
            if (base != 10) m_position += 2;
        }
        const int digitsStart = m_position;
        quint64 bits = 0;
        bool overflow = false, previousDigit = false;
        const quint64 limit = m_config.bitWidth == 64 ? std::numeric_limits<quint64>::max()
                                                      : (quint64(1) << m_config.bitWidth) - 1;
        while (current().isLetterOrNumber() || current() == QLatin1Char('_') || current() == QLatin1Char('.'))
        {
            const QChar ch = current();
            if (ch == QLatin1Char('_'))
            {
                const int next = m_position + 1 < m_text.size() ? digit(m_text.at(m_position + 1)) : -1;
                if (!previousDigit || next < 0 || next >= base)
                    failHere(QStringLiteral("_ 只能位于两个合法进制数字之间"));
                previousDigit = false;
            }
            else
            {
                const int value = digit(ch);
                if (value < 0 || value >= base)
                    failHere(base == 10 ? QStringLiteral("只支持十进制整数，不支持小数或科学计数法")
                                        : QStringLiteral("无效的 %1 进制数字").arg(base));
                if (!overflow)
                {
                    overflow = bits > (limit - quint64(value)) / quint64(base);
                    if (!overflow) bits = bits * quint64(base) + quint64(value);
                }
                previousDigit = true;
            }
            ++m_position;
        }
        if (m_position == digitsStart) failHere(QStringLiteral("进制前缀后需要数字"));
        const SourceRange range{start, m_position - start};
        if (overflow)
            fail(QStringLiteral("整数字面量超出当前 %1 位范围").arg(m_config.bitWidth), CalculationError::Overflow, range);
        const quint64 signBit = quint64(1) << (m_config.bitWidth - 1);
        if (base == 10 && m_config.isSigned && bits > signBit)
            fail(QStringLiteral("十进制字面量超出当前有符号数值范围"), CalculationError::Overflow, range);
        ProgrammerValue value;
        ProgrammerValue::fromBits(bits, m_config, value);
        return {value, range, base == 10 && m_config.isSigned && bits == signBit, range};
    }

    const QString &m_text;
    ProgrammerConfig m_config;
    const ProgrammerValue &m_answer;
    int m_position = 0;
    int m_depth = 0;
};
}

ProgrammerResult ProgrammerEngine::evaluate(const QString &expression, ProgrammerConfig config,
                                            const ProgrammerValue &answer)
{
    return Parser(expression, config, answer).parse();
}
