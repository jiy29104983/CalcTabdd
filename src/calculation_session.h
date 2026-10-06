#pragma once

#include "calculation_history.h"
#include <QByteArray>
#include <QMap>
#include <QUuid>
#include <memory>

class QLockFile;

struct CalculationInputState
{
    QString text;
    int position = 0;
    int anchor = 0;
    // 空定义表示普通模式；历史浏览也携带当时的模式和定义。
    QString customDefinition;
};

struct CalculationSession
{
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    CalculationHistory history;
    CalculationInputState input;
    CalculationInputState draft;
    CalculationInputState normalInput;
    CalculationInputState customInput;
    int historyPosition = -1;
    QMap<quint64, CalculationInputState> recalledInputs;
};

// 当前会话使用 UTF-8 JSON；数值／编号用十进制字符串，避免 JSON 数值精度或负零丢失。
namespace SessionFormat {
constexpr int maximumBytes = 16 * 1024 * 1024;
constexpr int maximumRecords = 10000;
QByteArray encode(const CalculationSession &session);
// 成功才替换输出；未知版本／损坏文件不做部分恢复，不重新计算历史结果。
QString decode(const QByteArray &bytes, CalculationSession &session);
}

// 一个实例独占一个本地文件；离开会话即释放锁。新建不覆盖已有文件。
class CalculationSessionFile
{
public:
    CalculationSessionFile();
    ~CalculationSessionFile();
    QString create(const QString &path, const CalculationSession &session);
    QString load(const QString &path, CalculationSession &session);
    QString save(const CalculationSession &session);
    QString path() const { return m_path; }

private:
    QString acquire(const QString &path);
    void release();
    QString m_path;
    std::unique_ptr<QLockFile> m_lock;
    QByteArray m_digest;
    bool m_exists = false;
};
