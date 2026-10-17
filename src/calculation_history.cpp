#include "calculation_history.h"
#include "custom_formula.h"

QString CalculationRecord::valueText() const
{
    if (!result.ok) return {};
    return result.value.text();
}

QString CalculationRecord::calculationText() const
{
    if (!result.ok) return expression + QStringLiteral("\n无法计算：") + result.text;
    const QString sources = result.value.sourceText();
    return expression + QStringLiteral("\n= ") + valueText() + (sources.isEmpty() ? QString() : QLatin1Char('\n') + sources);
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
    record.id = quint64(m_records.size()) + 1;
    record.expression = expression;
    record.answerBefore = m_answer;
    record.result = ExpressionEngine::evaluate(expression, record.answerBefore);
    m_records.append(record);
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
    const QString parameterInput = input;
    calculate(expression);
    auto &entry = m_records.last();
    entry.customDefinition = formula.definition();
    entry.parameterInput = parameterInput;
    entry.expression.prepend(formula.name() + QLatin1Char('='));
    if (!entry.result.ok) entry.result.errorPosition += formula.name().size() + 1;
    return entry;
}

void CalculationHistory::clear()
{
    m_records.clear();
    m_answer = NumericValue();
}

const CalculationRecord *CalculationHistory::record(quint64 id) const
{
    return id > 0 && id <= quint64(m_records.size()) ? &m_records.at(int(id - 1)) : nullptr;
}

bool CalculationHistory::restoreRecords(const QVector<CalculationRecord> &records)
{
    NumericValue answer;
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
        if (entry.id != nextId++ || entry.expression.isEmpty() || (!result.ok && result.text.isEmpty())
            || entry.answerBefore != answer)
            return false;
        if (result.ok)
        {
            if (result.error != CalculationError::None || result.errorPosition != -1 || result.errorLength != 0)
                return false;
            answer = result.value;
        }
        else if (result.value != NumericValue() || result.error <= CalculationError::None || result.error > CalculationError::Limit
                 || result.errorPosition < 0 || result.errorPosition > entry.expression.size()
                 || result.errorLength < 0 || result.errorLength > entry.expression.size() - result.errorPosition)
            return false;
    }
    m_records = records;
    for (auto &entry : m_records)
        if (entry.result.ok) entry.result.text = entry.result.value.text();
    m_answer = answer;
    return true;
}
