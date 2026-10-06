#pragma once
#include <QString>
#include <QVector>

enum class CalculationError
{
    None,
    Syntax,
    UnknownName,
    ArgumentCount,
    DivisionByZero,
    Domain,
    Overflow,
    Underflow,
    Limit
};

struct CalculationResult
{
    bool ok = false;
    double value = 0;
    QString text;
    // 相对传入公式的 UTF-16 范围；长度 0 表示插入点（例如末尾缺少右括号）。
    int errorPosition = -1;
    int errorLength = 0;
    CalculationError error = CalculationError::None;
};

struct ExpressionParameter
{
    QString name;
    int position;
    int length;
};

class ExpressionEngine
{
public:
    // 只校验语法／函数元数并提取参数，不执行数学运算或读取 ans。
    static CalculationResult inspect(const QString &expression, QVector<ExpressionParameter> &parameters);
    static bool isReservedName(const QString &name);
    static CalculationResult evaluate(const QString &expression, double answer = 0);
};
