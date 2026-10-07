#pragma once

#include <QByteArray>
#include <QString>

// 精确十进制基础层。除法、科学函数和用户可见文本由调用层的独立规则定义。
enum class DecimalError
{
    None,
    Syntax,
    PrecisionLimit,
    Overflow,
    Underflow,
    ResourceLimit
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
