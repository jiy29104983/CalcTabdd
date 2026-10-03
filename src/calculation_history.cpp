#include "calculation_history.h"

#include <algorithm>

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
