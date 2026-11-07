#pragma once

#include "calculation_history.h"
#include <QByteArray>
#include <QMap>
#include <memory>

class QLockFile;

struct CalculationInputState
{
    QString text;
    int position = 0;
    int anchor = 0;
    // 数学模式中，空定义表示普通模式；历史浏览也携带当时的模式和定义。
    QString customDefinition;
    // 未确认的定义仅在窗口内随模式／历史草稿保留；文件仍只保存已确认定义。
    QString definitionDraft = {};
    // 程序员模式及其计算设置独立于数学自定义定义。
    bool programmer = false;
    ProgrammerConfig programmerConfig = {};
};

struct CalculationSession
{
    CalculationHistory history;
    CalculationInputState input;
    CalculationInputState draft;
    CalculationInputState normalInput;
    CalculationInputState customInput;
    CalculationInputState programmerInput = {{}, 0, 0, {}, {}, true, {}};
    int historyPosition = -1;
    QMap<quint64, CalculationInputState> recalledInputs;
};

// 当前 version 4 使用 UTF-8 JSON；数值与来源独立保存，整数位模式及编号为字符串。
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
    // 仅成功读取或提交后保留原文件字节（最多 16 MiB），精确检测外部修改。
    QByteArray m_savedBytes;
    // 持有已校验的 COW 快照，确保存储身份在缓存有效期间不会被复用。
    // 草稿／光标变化只需重新校验输入状态，不重新构建和解析历史。
    bool m_hasCachedHistory = false;
    CalculationHistory m_cachedHistory;
    QByteArray m_cachedRecords;
};
