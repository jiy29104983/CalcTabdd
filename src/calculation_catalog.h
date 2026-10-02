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
    QString insertion;
    int cursorOffset = 0;

    bool isCompletion() const;
    bool matches(const QString &query, bool completion = false) const;
};

const QVector<Entry> &entries();
}
