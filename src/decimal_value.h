#pragma once

#include <QByteArray>
#include <QString>

// 有界十进制基础层。科学函数、近似传播和用户可见文本由调用层的独立规则定义。
enum class DecimalError
{
    None,
    Syntax,
    PrecisionLimit,
    Overflow,
    Underflow,
    ResourceLimit,
    DivisionByZero
};

class DecimalValue
{
public:
    static constexpr int maximumDigits = 50;
    static constexpr int minimumAdjustedExponent = -999;
    static constexpr int maximumAdjustedExponent = 999;
    static constexpr int maximumLiteralLength = 4096;

    // 只接受 ASCII 数字 token；不经 double、不接受空白／NaN／Infinity。
    // 所有有输出参数的操作只在成功时替换输出，也允许输出与输入为同一对象。
    static DecimalError parse(const QString &text, DecimalValue &output);
    static DecimalError add(const DecimalValue &left, const DecimalValue &right, DecimalValue &output);
    static DecimalError subtract(const DecimalValue &left, const DecimalValue &right, DecimalValue &output);
    static DecimalError multiply(const DecimalValue &left, const DecimalValue &right, DecimalValue &output);

    // 每次除法最多 50 位有效数字，最近值／半值取偶；精确商和舍入后均检查指数范围。
    // inexact 只报告本次是否改变精确商，不推断输入或整条表达式的近似来源。
    // 成功同时更新 output 和 inexact；失败两者均保持。支持 output 与任一输入别名。
    static DecimalError divide(const DecimalValue &left, const DecimalValue &right,
                               DecimalValue &output, bool &inexact);

    // value = sign * coefficient * 10^exponent。
    // 非零系数无前导／尾随零；零的系数为 "0"，指数为 0。
    const QByteArray &coefficient() const { return m_coefficient; }
    int exponent() const { return m_exponent; }
    bool isNegative() const { return m_negative; }
    bool isZero() const { return m_coefficient == "0"; }
    DecimalValue negated() const;

private:
    static DecimalError finish(QByteArray coefficient, int exponent, bool negative, DecimalValue &output);
    QByteArray m_coefficient = "0";
    int m_exponent = 0;
    bool m_negative = false;
};
