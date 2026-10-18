#pragma once

#include <QString>
#include <QVector>

namespace CalculationCatalog {
enum class Kind { Function, Constant, Operator, Syntax, Precision };

struct Entry
{
    Kind kind;
    QString name;
    QString signature;
    QString category;
    QString title;
    QString description;
    QString example;
    QString keywords;

    bool isCompletion() const;
    bool matches(const QString &query, bool completion = false) const;
    // 补全插入文本由名称和函数元数派生，避免与引擎参数定义漂移。
    QString insertion() const;
};

const QVector<Entry> &entries();
}
