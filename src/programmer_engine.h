#pragma once

#include "expression_engine.h"
#include "programmer_value.h"

struct ProgrammerResult
{
    bool ok = false;
    ProgrammerValue value;
    QString text;
    int errorPosition = -1;
    int errorLength = 0;
    CalculationError error = CalculationError::None;
};

// 独立整数求值入口；不读写数学引擎、历史或会话。调用方仅在成功时保存新的整数 ans。
class ProgrammerEngine
{
public:
    static constexpr int maximumLength = 4096;
    static constexpr int maximumDepth = 128;
    static ProgrammerResult evaluate(const QString &expression, ProgrammerConfig config = {},
                                     const ProgrammerValue &answer = {});
};
