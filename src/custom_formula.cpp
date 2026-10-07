#include "custom_formula.h"

#include <QMap>
#include <QRegularExpression>

namespace {
struct Assignment
{
    QString name;
    QString value;
};
QVector<Assignment> assignments(const QString &input, QString &error)
{
    static const QRegularExpression assignment(QStringLiteral("\\A([A-Za-z_][A-Za-z0-9_]*)[ \\t]*=[ \\t]*([^=]*)\\z"));
    QVector<Assignment> values;
    const QStringList lines = input.split(QLatin1Char('\n'));
    for (int index = 0; index < lines.size(); ++index)
    {
        const QString line = lines.at(index).trimmed();
        if (line.isEmpty()) continue;
        const auto match = assignment.match(line);
        if (!match.hasMatch())
        {
            if (error.isEmpty()) error = QStringLiteral("第 %1 行：每行只能填写一个参数，格式为 名称=数字").arg(index + 1);
            continue;
        }
        values.append({match.captured(1), match.captured(2).trimmed()});
    }
    return values;
}
}

QString CustomFormula::parse(const QString &definition, CustomFormula &formula)
{
    const int equals = definition.indexOf(QLatin1Char('='));
    if (equals < 0) return QStringLiteral("请使用 名称=表达式，例如 A=x+y");
    CustomFormula parsed;
    parsed.m_name = definition.left(equals).trimmed();
    parsed.m_expression = definition.mid(equals + 1).trimmed();
    static const QRegularExpression identifier(QStringLiteral("\\A[A-Za-z_][A-Za-z0-9_]*\\z"));
    if (!identifier.match(parsed.m_name).hasMatch() || ExpressionEngine::isReservedName(parsed.m_name))
        return QStringLiteral("公式名称须以英文字母或下划线开头，仅含英文字母、数字、下划线，且不能使用内置名称");
    if (definition.size() > 4096) return QStringLiteral("公式定义过长（最多 4096 个 UTF-16 代码单元）");
    const auto checked = ExpressionEngine::inspect(parsed.m_expression, parsed.m_tokens);
    if (!checked.ok) return QStringLiteral("定义无效：%1（表达式第 %2 个字符）").arg(checked.text).arg(checked.errorPosition + 1);
    for (const auto &token : parsed.m_tokens)
    {
        if (token.name == parsed.m_name) return QStringLiteral("公式不能引用自身名称：%1").arg(token.name);
        if (!parsed.m_parameters.contains(token.name)) parsed.m_parameters.append(token.name);
    }
    formula = parsed;
    return {};
}

QString CustomFormula::parameterTemplate(const QString &previousInput) const
{
    QString ignored;
    QMap<QString, QString> previous;
    for (const auto &item : assignments(previousInput, ignored))
        if (!previous.contains(item.name)) previous.insert(item.name, item.value);
    QStringList lines;
    for (const auto &name : m_parameters) lines.append(name + QLatin1Char('=') + previous.value(name));
    return lines.join(QLatin1Char('\n'));
}

QString CustomFormula::substitute(const QString &input, QString &expression) const
{
    return substituteImpl(input, expression, false);
}

QString CustomFormula::substituteNumeric(const QString &input, QString &expression) const
{
    return substituteImpl(input, expression, true);
}

NumericCalculationResult CustomFormula::evaluateNumeric(const QString &input, const NumericValue &answer, QString &parameterError) const
{
    QString expression;
    parameterError = substituteNumeric(input, expression);
    if (!parameterError.isEmpty()) return {};
    return ExpressionEngine::evaluateNumeric(expression, answer);
}

QString CustomFormula::substituteImpl(const QString &input, QString &expression, bool numericValues) const
{
    QString error;
    const auto items = assignments(input, error);
    if (!error.isEmpty()) return error;
    static const QRegularExpression numeric(QStringLiteral("\\A[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?\\z"));
    QMap<QString, QString> values;
    for (const auto &item : items)
    {
        if (!m_parameters.contains(item.name)) return QStringLiteral("未知参数：%1（名称区分大小写）").arg(item.name);
        if (values.contains(item.name)) return QStringLiteral("参数重复：%1").arg(item.name);
        if (!numeric.match(item.value).hasMatch())
            return QStringLiteral("请填写 %1 的数字值（支持正负号、小数和科学计数法）").arg(item.name);
        if (numericValues)
        {
            const auto checked = ExpressionEngine::evaluateNumeric(item.value);
            if (!checked.ok) return QStringLiteral("参数 %1：%2").arg(item.name, checked.text);
        }
        else
        {
            const auto checked = ExpressionEngine::evaluate(item.value);
            if (!checked.ok) return QStringLiteral("参数 %1：%2").arg(item.name, checked.text);
        }
        values.insert(item.name, item.value);
    }
    for (const auto &name : m_parameters)
        if (!values.contains(name)) return QStringLiteral("缺少参数：%1").arg(name);
    // 先计算最终长度，再分配代入式；重复参数不能通过展开消耗无界内存。
    qint64 expandedLength = m_expression.size();
    for (const auto &token : m_tokens)
    {
        const QString &value = values[token.name];
        const int parentheses = value.startsWith(QLatin1Char('-')) || value.startsWith(QLatin1Char('+')) ? 2 : 0;
        expandedLength += value.size() + parentheses - token.length;
    }
    if (expandedLength > 4096)
        return QStringLiteral("代入后的公式过长（最多 4096 个 UTF-16 代码单元），请缩短公式或参数值");
    QString substituted = m_expression;
    for (int i = m_tokens.size() - 1; i >= 0; --i)
    {
        const auto &token = m_tokens.at(i);
        QString value = values.value(token.name);
        if (value.startsWith(QLatin1Char('-')) || value.startsWith(QLatin1Char('+')))
            value = QLatin1Char('(') + value + QLatin1Char(')');
        substituted.replace(token.position, token.length, value);
    }
    expression = substituted;
    return {};
}
