#pragma once

#include "calculation_history.h"
#include <QByteArray>

namespace CalculationExport {
enum class Format { Text, Markdown };

// 只读取已有快照，不重新求值。UTF-8（无 BOM），统一使用 LF 换行。
QByteArray serialize(const QVector<CalculationRecord> &records, Format format);
// 空字符串表示成功；失败时返回原因。只有完整写入并提交后才替换目标文件。
QString writeFile(const QString &path, const QByteArray &contents);
}
