#include "calculation_history.h"
#include "custom_formula.h"

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

CalculationRecord CalculationHistory::calculateCustom(const QString &definition, const QString &input, QString &error)
{
    CustomFormula formula;
    error = CustomFormula::parse(definition, formula);
    QString expression;
    if (error.isEmpty()) error = formula.substitute(input, expression);
    if (!error.isEmpty()) return {};
    CalculationRecord entry;
    entry.id = m_nextId++;
    entry.customDefinition = formula.definition();
    entry.parameterInput = input;
    entry.expression = formula.name() + QLatin1Char('=') + expression;
    entry.answerBefore = m_answer;
    entry.result = ExpressionEngine::evaluate(expression, m_answer);
    if (entry.result.ok) m_answer = entry.result.value;
    else entry.result.errorPosition += formula.name().size() + 1;
    m_records.append(entry);
    return entry;
}

void CalculationHistory::clear()
{
    m_records.clear();
    m_answer = 0;
    m_nextId = 1;
}

const CalculationRecord *CalculationHistory::record(quint64 id) const
{
    const auto found = std::lower_bound(m_records.cbegin(), m_records.cend(), id,
        [](const CalculationRecord &record, quint64 target) { return record.id < target; });
    return found != m_records.cend() && found->id == id ? &*found : nullptr;
}

bool CalculationHistory::restoreRecords(const QVector<CalculationRecord> &records)
{
    double answer = 0;
    quint64 nextId = 1;
    for (const auto &entry : records)
    {
        if (!entry.customDefinition.isEmpty())
        {
            CustomFormula formula;
            QString substituted;
            if (!CustomFormula::parse(entry.customDefinition, formula).isEmpty()
                || !formula.substitute(entry.parameterInput, substituted).isEmpty()
                || entry.expression != formula.name() + QLatin1Char('=') + substituted) return false;
        }
        else if (!entry.parameterInput.isEmpty()) return false;
        const auto &result = entry.result;
        if (entry.id != nextId++ || entry.expression.isEmpty() || result.text.isEmpty()
            || !std::isfinite(entry.answerBefore) || !std::isfinite(result.value)
            || entry.answerBefore != answer || std::signbit(entry.answerBefore) != std::signbit(answer))
            return false;
        if (result.ok)
        {
            if (result.error != CalculationError::None || result.errorPosition != -1 || result.errorLength != 0)
                return false;
            answer = result.value;
        }
        else if (result.error <= CalculationError::None || result.error > CalculationError::Limit
                 || result.errorPosition < 0 || result.errorPosition > entry.expression.size()
                 || result.errorLength < 0 || result.errorLength > entry.expression.size() - result.errorPosition)
            return false;
    }
    m_records = records;
    m_nextId = nextId;
    m_answer = answer;
    return true;
}
