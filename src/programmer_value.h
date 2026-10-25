#pragma once

#include <QString>
#include <QtGlobal>

struct ProgrammerConfig
{
    int bitWidth = 32;
    bool isSigned = false;
    bool isValid() const;
    bool operator==(const ProgrammerConfig &other) const
    {
        return bitWidth == other.bitWidth && isSigned == other.isSigned;
    }
    bool operator!=(const ProgrammerConfig &other) const { return !(*this == other); }
};

enum class ProgrammerError { None, InvalidConfig, Overflow, DivisionByZero, ShiftRange, InvalidOperation };
enum class IntegerUnary { Plus, Negate, Complement };
enum class IntegerBinary { Add, Subtract, Multiply, Divide, Remainder, And, Xor, Or, LeftShift, RightShift, LogicalRightShift };

// 固定位宽整数及回绕来源。所有运算只用无符号位模式，不依赖有符号溢出或负数移位。
class ProgrammerValue
{
public:
    enum WrapSource : unsigned { Wrap8 = 1, Wrap16 = 2, Wrap32 = 4, Wrap64 = 8 };

    // 默认值为 U32 的 0。失败保持 output；所有输出参数均支持与输入别名。
    static ProgrammerError fromBits(quint64 bits, ProgrammerConfig config, ProgrammerValue &output);
    // 会话位模式为小写、无前缀、补满位宽的十六进制；校验后恢复来源，不重新计算。
    static bool restore(const QString &bits, ProgrammerConfig config, unsigned sources, ProgrammerValue &output);
    static ProgrammerError operate(IntegerUnary operation, const ProgrammerValue &value, ProgrammerValue &output);
    static ProgrammerError operate(IntegerBinary operation, const ProgrammerValue &left,
                                   const ProgrammerValue &right, ProgrammerValue &output);
    // 保持数学值，不截断或重新解释原位模式，保留原回绕位宽。
    ProgrammerError converted(ProgrammerConfig config, ProgrammerValue &output) const;

    ProgrammerConfig config() const { return m_config; }
    quint64 bits() const { return m_bits; }
    quint64 magnitude() const;
    bool isNegative() const;
    unsigned sources() const { return m_sources; }
    QString text() const;
    QString hexText() const;
    QString binaryText() const;
    QString octalText() const;
    QString sourceText() const;
    bool operator==(const ProgrammerValue &other) const;
    bool operator!=(const ProgrammerValue &other) const { return !(*this == other); }

private:
    static quint64 mask(ProgrammerConfig config);
    static quint64 magnitudeLimit(ProgrammerConfig config, bool negative);
    static unsigned wrapSource(ProgrammerConfig config);
    quint64 m_bits = 0;
    ProgrammerConfig m_config;
    unsigned m_sources = 0;
};
