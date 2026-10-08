#pragma once
#include "numeric_value.h"
#include <QtTest>

// 旧交互用例只比较小数值的大小；新数值契约用例另外验证类型、来源与规范文本。
inline double asDouble(const NumericValue &value)
{
    double result = 0;
    bool changed = false;
    if (!value.toBinary(result, changed).ok()) qFatal("Test requested an unrepresentable binary64 conversion");
    return result;
}
inline NumericValue decimalNumber(const QString &text)
{
    NumericValue result;
    if (!NumericValue::parse(text, result).ok()) qFatal("Invalid decimal test fixture");
    return result;
}
inline NumericValue binaryNumber(double value)
{
    NumericValue result;
    if (!NumericValue::fromBinary(value, result).ok()) qFatal("Invalid binary64 test fixture");
    return result;
}
namespace QTest {
template<> inline char *toString(const NumericValue &value)
{
    const QByteArray description = (value.isBinary() ? QStringLiteral("binary64:") + value.binaryHex()
        : QStringLiteral("decimal:") + value.text()).toUtf8() + ":sources=" + QByteArray::number(value.sources());
    return qstrdup(description.constData());
}
}
