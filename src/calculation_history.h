#pragma once

#include "expression_engine.h"
#include "programmer_value.h"
#include <QVector>

struct CalculationRecord
{
    // 编号等于记录下标 + 1；只追加或整批清空，恢复时验证连续性。
    quint64 id = 0;
    QString expression;
    CalculationResult result;
    NumericValue answerBefore;
    QString customDefinition;
    QString parameterInput;
    // 整数结果独立存储，result 只共用成功／错误与诊断字段，不转换为数学值。
    bool programmer = false;
    ProgrammerConfig programmerConfig;
    ProgrammerValue programmerValue;
    ProgrammerValue programmerAnswerBefore;

    // 数值复用读取内部值；失败记录没有可复制或插入的数值。
    QString valueText() const;
    QString calculationText() const;
    // 程序员输入使用保存值的完整整数正文；非整数返回空字符串。
    QString insertionText(bool integerOnly = false) const;
    QString sourceText() const;
};

class CalculationHistory
{
public:
    // 返回值副本，供调用方安全使用，不受后续追加引起的存储移动影响。
    CalculationRecord calculate(const QString &expression);
    CalculationRecord calculateProgrammer(const QString &expression, ProgrammerConfig config);
    // 参数校验失败不追加记录，返回 id=0 并提供提示。
    CalculationRecord calculateCustom(const QString &definition, const QString &input, QString &error);
    // 同时清空成功和失败记录，重置 ans 与编号。
    void clear();
    // 校验整份快照后替换；失败保持原模型，不求值旧公式。
    bool restoreRecords(const QVector<CalculationRecord> &records);
    const QVector<CalculationRecord> &records() const { return m_records; }
    // 未找到时返回 nullptr；指针只在下次修改模型前有效。跨事件保存 id。
    const CalculationRecord *record(quint64 id) const;
    int count() const { return m_records.size(); }
    const NumericValue &answer() const { return m_answer; }
    const ProgrammerValue &programmerAnswer() const { return m_programmerAnswer; }
    bool hasProgrammerRecords() const;

private:
    QVector<CalculationRecord> m_records;
    NumericValue m_answer;
    ProgrammerValue m_programmerAnswer;
};
