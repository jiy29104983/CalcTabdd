#pragma once

#include "decimal_value.h"
#include <QVector>

enum class NumericError
{
    None, Syntax, PrecisionLimit, ResourceLimit, Domain, DivisionByZero,
    Overflow, Underflow, ConversionOverflow, ConversionUnderflow, UnknownName, ArgumentCount
};

struct NumericStatus
{
    NumericError error = NumericError::None;
    // -1 指运算节点；0/1 指出错参数。转换错误保留参数定位。
    int argument = -1;
    bool ok() const { return error == NumericError::None; }
};

// 独立混算值：数值类型和计算来源共同传递，不以显示文本重建。
// 显示、复用与持久化均读取同一数值；纯文本重新输入不携带来源。
class NumericValue
{
public:
    enum Source : unsigned { Rounded = 1, Approximate = 2, ConversionLoss = 4 };
    NumericValue() = default;
    explicit NumericValue(const DecimalValue &value) : m_decimal(value) {}
    static NumericStatus parse(const QString &literal, NumericValue &output);
    static NumericStatus fromBinary(double value, NumericValue &output);
    static NumericStatus constant(const QString &name, NumericValue &output);
    static NumericStatus operate(char operation, const NumericValue &left, const NumericValue &right,
                                 NumericValue &output);
    static NumericStatus function(const QString &name, const QVector<NumericValue> &arguments, NumericValue &output);
    static int argumentCount(const QString &name);
    // 有限非零次正规数可接受；成功同时更新两输出，失败都保持。
    NumericStatus toBinary(double &output, bool &changed) const;
    bool isBinary() const { return m_isBinary; }
    const DecimalValue &decimal() const { return m_decimal; }
    double binary() const { return m_binary; }
    unsigned sources() const { return m_sources; }
    // 统一、区域无关的完整正文与独立来源说明。
    QString text() const;
    QString sourceText() const;
    QString binaryHex() const;
    // 只接受规范存储形式及合法来源组合，失败不修改输出。
    static bool restoreDecimal(const QString &text, unsigned sources, NumericValue &output);
    static bool restoreBinary(const QString &hex, unsigned sources, NumericValue &output);
    // 快照相等包含类型、数值、零符号和来源；不是数学上的大小比较。
    bool operator==(const NumericValue &other) const;
    bool operator!=(const NumericValue &other) const { return !(*this == other); }
    bool isZero() const;
    bool isNegative() const;
    bool isInteger() const;
    NumericValue negated() const;

private:
    static NumericStatus finishBinary(double value, unsigned sources, bool zeroUnderflow, NumericValue &output);
    DecimalValue m_decimal;
    double m_binary = 0;
    unsigned m_sources = 0;
    bool m_isBinary = false;
};
