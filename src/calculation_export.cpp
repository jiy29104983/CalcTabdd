#include "calculation_export.h"

#include <QSaveFile>

namespace CalculationExport {
QByteArray serialize(const QVector<CalculationRecord> &records, Format format)
{
    const bool markdown = format == Format::Markdown;
    QString text = markdown ? QStringLiteral("# CalcTabdd 计算记录\n\n")
                            : QStringLiteral("CalcTabdd 计算记录\n\n");
    for (const auto &record : records)
    {
        const QString number = QStringLiteral("%1").arg(record.id, 2, 10, QLatin1Char('0'));
        const QString calculation = record.calculationText();
        if (markdown)
        {
            // 围栏比正文中的任意连续反引号更长；公式、HTML 和错误文本均按原文显示。
            int longest = 0;
            int current = 0;
            for (const QChar character : calculation)
            {
                current = character == QLatin1Char('`') ? current + 1 : 0;
                longest = qMax(longest, current);
            }
            const QString fence(qMax(3, longest + 1), QLatin1Char('`'));
            text += QStringLiteral("## 记录 %1\n\n").arg(number) + fence + QStringLiteral("text\n")
                + calculation + QLatin1Char('\n') + fence + QStringLiteral("\n\n");
        }
        else text += QStringLiteral("[%1]\n").arg(number) + calculation + QStringLiteral("\n\n");
    }
    return text.toUtf8();
}

QString writeFile(const QString &path, const QByteArray &contents)
{
    QSaveFile file(path);
    // 不回退到直接截断旧文件：目录权限不足或提交失败时保留原文件。
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("无法创建导出文件：%1").arg(file.errorString());
    if (file.write(contents) != contents.size())
    {
        const QString error = QStringLiteral("写入未完成：%1").arg(file.errorString());
        file.cancelWriting();
        return error;
    }
    if (!file.commit()) return QStringLiteral("无法完成文件保存：%1").arg(file.errorString());
    return {};
}
}
