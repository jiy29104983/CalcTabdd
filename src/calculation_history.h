#pragma once

#include "expression_engine.h"
#include <QVector>

struct CalculationRecord
{
    // 编号仅在当前会话内有效，从 1 开始；清空开始新会话，追加不改已有编号。
    quint64 id = 0;
    QString expression;
    CalculationResult result;
    NumericValue answerBefore;
    QString customDefinition;
    QString parameterInput;

    // 数值复用读取内部值；失败记录没有可复制或插入的数值。
    QString valueText() const;
    QString calculationText() const;
    QString insertionText() const;
};

// 计算会话的数据源，不依赖页面或控件；每个页面独立持有一个实例。
class CalculationHistory
{
public:
    // 返回值副本，供调用方安全使用，不受后续追加引起的存储移动影响。
    CalculationRecord calculate(const QString &expression);
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

private:
    QVector<CalculationRecord> m_records;
    quint64 m_nextId = 1;
    NumericValue m_answer;
};
