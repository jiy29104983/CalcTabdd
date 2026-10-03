#include "calculation_history.h"

#include <algorithm>
#include <cmath>
#include <limits>

QString CalculationRecord::valueText() const
{
    if (!result.ok) return {};
    // QString 的 C 区域格式不含分组符；保留负零，避免往返时丢失符号。
    if (result.value == 0 && std::signbit(result.value)) return QStringLiteral("-0");
    return QString::number(result.value, 'g', std::numeric_limits<double>::max_digits10);
}

QString CalculationRecord::calculationText() const
{
    return expression + (result.ok ? QStringLiteral("\n= ") + valueText()
                                   : QStringLiteral("\n无法计算：") + result.text);
}

QString CalculationRecord::insertionText() const
{
    const QString value = valueText();
    // 一元负号的优先级低于乘方；作为一个操作数插入时必须括起来。
    return value.startsWith(QLatin1Char('-')) ? QStringLiteral("(%1)").arg(value) : value;
}

CalculationRecord CalculationHistory::calculate(const QString &expression)
{
    CalculationRecord record;
    record.id = m_nextId;
    record.expression = expression;
    record.answerBefore = m_answer;
    record.result = ExpressionEngine::evaluate(expression, record.answerBefore);
    m_records.append(record);
    ++m_nextId;
    if (record.result.ok) m_answer = record.result.value;
    return record;
}

const CalculationRecord *CalculationHistory::record(quint64 id) const
{
    const auto found = std::lower_bound(m_records.cbegin(), m_records.cend(), id,
        [](const CalculationRecord &record, quint64 target) { return record.id < target; });
    return found != m_records.cend() && found->id == id ? &*found : nullptr;
}
