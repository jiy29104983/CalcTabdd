#pragma once
#include <QString>

struct CalculationResult
{
    bool ok = false;
    double value = 0;
    QString text;
    int errorPosition = -1;
};

class ExpressionEngine
{
public:
    static CalculationResult evaluate(const QString &expression, double answer = 0);
};
