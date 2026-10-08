#pragma once

#include "expression_engine.h"
#include <QStringList>

// 已校验的定义。参数位置来自与普通计算共用的语法解析器，替换不会改动函数或常量。
class CustomFormula
{
public:
    static QString parse(const QString &definition, CustomFormula &formula);
    QString definition() const { return m_name + QLatin1Char('=') + m_expression; }
    QString name() const { return m_name; }
    QStringList parameters() const { return m_parameters; }
    QString parameterTemplate(const QString &previousInput = QString()) const;
    // 仅校验参数及生成代入式；数学运算留给明确提交后的 ExpressionEngine。
    QString substitute(const QString &input, QString &expression) const;
    // 与普通入口共用十进制校验和原始字面量代入。
    QString substituteNumeric(const QString &input, QString &expression) const;
    NumericCalculationResult evaluateNumeric(const QString &input, const NumericValue &answer, QString &parameterError) const;

private:
    QString substituteImpl(const QString &input, QString &expression) const;
    QString m_name;
    QString m_expression;
    QStringList m_parameters;
    QVector<ExpressionParameter> m_tokens;
};
