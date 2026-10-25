#include "calculation_history.h"
#include "custom_formula.h"
#include "programmer_engine.h"
#include <algorithm>

QString CalculationRecord::valueText() const
{
    if (!result.ok) return {};
    return programmer ? programmerValue.text() : result.value.text();
}

QString CalculationRecord::sourceText() const
{
    return result.ok ? (programmer ? programmerValue.sourceText() : result.value.sourceText()) : QString();
}

QString CalculationRecord::calculationText() const
{
    // 程序员完整文本由后续复用／导出任务接入，不能生成丢失配置的数学记录。
    if (programmer) return {};
    if (!result.ok) return expression + QStringLiteral("\n无法计算：") + result.text;
    const QString sources = result.value.sourceText();
    return expression + QStringLiteral("\n= ") + valueText() + (sources.isEmpty() ? QString() : QLatin1Char('\n') + sources);
}

CalculationRecord CalculationHistory::calculateProgrammer(const QString &expression, ProgrammerConfig config)
{
    CalculationRecord record;
    record.id = quint64(m_records.size()) + 1;
    record.expression = expression;
    record.programmer = true;
    record.programmerConfig = config;
    record.answerBefore = m_answer;
    record.programmerAnswerBefore = m_programmerAnswer;
    const auto result = ProgrammerEngine::evaluate(expression, config, m_programmerAnswer);
    record.result.ok = result.ok;
    record.result.text = result.text;
    record.result.error = result.error;
    record.result.errorPosition = result.errorPosition;
    record.result.errorLength = result.errorLength;
    record.programmerValue = result.value;
    if (result.ok) m_programmerAnswer = result.value;
    m_records.append(record);
    return record;
}

bool CalculationHistory::hasProgrammerRecords() const
{
    return std::any_of(m_records.cbegin(), m_records.cend(), [](const CalculationRecord &record) { return record.programmer; });
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
    m_programmerAnswer = ProgrammerValue();
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
        // 当前文件格式仅支持数学记录；整数快照恢复在 version 4 中接入。
        if (entry.programmer) return false;
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
    m_programmerAnswer = ProgrammerValue();
    return true;
}
