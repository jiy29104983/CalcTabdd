#include "calculation_session.h"
#include "custom_formula.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <cmath>

namespace {
QJsonObject number(const NumericValue &value)
{
    QJsonArray sources;
    if (value.sources() & NumericValue::Rounded) sources.append(QStringLiteral("R"));
    if (value.sources() & NumericValue::Approximate) sources.append(QStringLiteral("A"));
    if (value.sources() & NumericValue::ConversionLoss) sources.append(QStringLiteral("C"));
    return {{QStringLiteral("kind"), value.isBinary() ? QStringLiteral("binary64") : QStringLiteral("decimal")},
            {value.isBinary() ? QStringLiteral("bits") : QStringLiteral("text"), value.isBinary() ? value.binaryHex() : value.text()},
            {QStringLiteral("sources"), sources}};
}

bool readNumber(const QJsonValue &value, NumericValue &result)
{
    if (!value.isObject()) return false;
    const auto object = value.toObject();
    if (object.size() != 3 || !object.value(QStringLiteral("kind")).isString()
        || !object.value(QStringLiteral("sources")).isArray()) return false;
    const QString kind = object.value(QStringLiteral("kind")).toString();
    const bool binary = kind == QStringLiteral("binary64");
    if (!binary && kind != QStringLiteral("decimal")) return false;
    const auto payload = object.value(binary ? QStringLiteral("bits") : QStringLiteral("text"));
    if (!payload.isString()) return false;
    unsigned sources = 0, previous = 0;
    for (const auto &item : object.value(QStringLiteral("sources")).toArray())
    {
        if (!item.isString()) return false;
        const QString name = item.toString();
        const unsigned flag = name == QStringLiteral("R") ? unsigned(NumericValue::Rounded)
            : name == QStringLiteral("A") ? unsigned(NumericValue::Approximate) : name == QStringLiteral("C") ? unsigned(NumericValue::ConversionLoss) : 0u;
        if (!flag || flag <= previous) return false;
        sources |= flag;
        previous = flag;
    }
    return binary ? NumericValue::restoreBinary(payload.toString(), sources, result)
                  : NumericValue::restoreDecimal(payload.toString(), sources, result);
}

bool readInteger(const QJsonValue &value, int &result, int minimum, int maximum)
{
    if (!value.isDouble()) return false;
    const double numeric = value.toDouble();
    if (!std::isfinite(numeric) || numeric < minimum || numeric > maximum || std::floor(numeric) != numeric) return false;
    result = static_cast<int>(numeric);
    return true;
}

QJsonObject configJson(ProgrammerConfig config)
{
    return {{QStringLiteral("bitWidth"), config.bitWidth}, {QStringLiteral("isSigned"), config.isSigned}};
}

bool readConfig(const QJsonValue &value, ProgrammerConfig &config)
{
    if (!value.isObject()) return false;
    const auto object = value.toObject();
    if (object.size() != 2 || !object.value(QStringLiteral("isSigned")).isBool()
        || !readInteger(object.value(QStringLiteral("bitWidth")), config.bitWidth, 8, 64)) return false;
    config.isSigned = object.value(QStringLiteral("isSigned")).toBool();
    return config.isValid();
}

QJsonObject integerJson(const ProgrammerValue &value)
{
    QJsonArray sources;
    unsigned flag = ProgrammerValue::Wrap8;
    for (int width : {8, 16, 32, 64})
    {
        if (value.sources() & flag) sources.append(width);
        flag <<= 1;
    }
    return {{QStringLiteral("kind"), QStringLiteral("integer")},
            {QStringLiteral("bits"), value.hexText().mid(2).toLower()},
            {QStringLiteral("config"), configJson(value.config())}, {QStringLiteral("sources"), sources}};
}

bool readProgrammerValue(const QJsonValue &value, ProgrammerValue &result)
{
    if (!value.isObject()) return false;
    const auto object = value.toObject();
    ProgrammerConfig config;
    if (object.size() != 4 || object.value(QStringLiteral("kind")) != QStringLiteral("integer")
        || !object.value(QStringLiteral("bits")).isString()
        || !readConfig(object.value(QStringLiteral("config")), config)
        || !object.value(QStringLiteral("sources")).isArray()) return false;
    unsigned sources = 0;
    int previous = 0;
    for (const auto &item : object.value(QStringLiteral("sources")).toArray())
    {
        int width = 0;
        if (!readInteger(item, width, 8, 64) || width <= previous) return false;
        const unsigned flag = width == 8 ? ProgrammerValue::Wrap8 : width == 16 ? ProgrammerValue::Wrap16
            : width == 32 ? ProgrammerValue::Wrap32 : width == 64 ? ProgrammerValue::Wrap64 : 0u;
        if (!flag) return false;
        sources |= flag;
        previous = width;
    }
    return ProgrammerValue::restore(object.value(QStringLiteral("bits")).toString(), config, sources, result);
}

QString modeName(bool programmer, const QString &definition)
{
    return programmer ? QStringLiteral("programmer")
        : definition.isEmpty() ? QStringLiteral("normal") : QStringLiteral("custom");
}

QJsonObject inputJson(const CalculationInputState &input)
{
    return {{QStringLiteral("text"), input.text}, {QStringLiteral("position"), input.position},
            {QStringLiteral("anchor"), input.anchor}, {QStringLiteral("customDefinition"), input.customDefinition},
            {QStringLiteral("mode"), modeName(input.programmer, input.customDefinition)},
            {QStringLiteral("programmerConfig"), configJson(input.programmerConfig)}};
}

bool readInput(const QJsonValue &value, CalculationInputState &input)
{
    if (!value.isObject()) return false;
    const auto object = value.toObject();
    if (object.size() != 6 || !object.value(QStringLiteral("text")).isString()
        || !object.value(QStringLiteral("mode")).isString()
        || !readConfig(object.value(QStringLiteral("programmerConfig")), input.programmerConfig)) return false;
    const QString mode = object.value(QStringLiteral("mode")).toString();
    input.programmer = mode == QStringLiteral("programmer");
    input.text = object.value(QStringLiteral("text")).toString();
    if (!object.value(QStringLiteral("customDefinition")).isString()) return false;
    input.customDefinition = object.value(QStringLiteral("customDefinition")).toString();
    if (mode != modeName(input.programmer, input.customDefinition)
        || (input.programmer && !input.customDefinition.isEmpty())
        || (!input.programmer && input.programmerConfig != ProgrammerConfig())) return false;
    CustomFormula formula;
    if (!input.customDefinition.isEmpty() && !CustomFormula::parse(input.customDefinition, formula).isEmpty()) return false;
    return readInteger(object.value(QStringLiteral("position")), input.position, 0, input.text.size())
        && readInteger(object.value(QStringLiteral("anchor")), input.anchor, 0, input.text.size());
}

QByteArray digest(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

QString readFile(const QString &path, QByteArray &bytes)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink()) return QStringLiteral("会话文件不存在或不是普通文件。");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("无法读取会话：%1").arg(file.errorString());
    bytes = file.read(SessionFormat::maximumBytes + 1);
    if (file.error() != QFile::NoError) return QStringLiteral("读取会话失败：%1").arg(file.errorString());
    if (bytes.size() > SessionFormat::maximumBytes) return QStringLiteral("会话文件超过 16 MiB，未载入或覆盖。");
    return {};
}
}

QByteArray SessionFormat::encode(const CalculationSession &session)
{
    QJsonArray records;
    for (const auto &entry : session.history.records())
    {
        const auto &result = entry.result;
        QJsonObject record{
            {QStringLiteral("id"), QString::number(entry.id)},
            {QStringLiteral("expression"), entry.expression},
            {QStringLiteral("customDefinition"), entry.customDefinition}, {QStringLiteral("parameterInput"), entry.parameterInput},
            {QStringLiteral("mode"), modeName(entry.programmer, entry.customDefinition)},
            {QStringLiteral("answerBefore"), number(entry.answerBefore)},
            {QStringLiteral("ok"), result.ok},
            {QStringLiteral("value"), result.ok ? QJsonValue(entry.programmer ? integerJson(entry.programmerValue) : number(result.value))
                                               : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("error"), static_cast<int>(result.error)},
            {QStringLiteral("errorPosition"), result.errorPosition}, {QStringLiteral("errorLength"), result.errorLength}};
        if (entry.programmer)
        {
            record.insert(QStringLiteral("programmerConfig"), configJson(entry.programmerConfig));
            record.insert(QStringLiteral("programmerAnswerBefore"), integerJson(entry.programmerAnswerBefore));
        }
        if (!result.ok) record.insert(QStringLiteral("text"), result.text);
        records.append(record);
    }
    QJsonObject recalled;
    for (auto it = session.recalledInputs.cbegin(); it != session.recalledInputs.cend(); ++it)
        recalled.insert(QString::number(it.key()), inputJson(it.value()));
    return QJsonDocument(QJsonObject{
        {QStringLiteral("format"), QStringLiteral("CalcTabdd.Session")}, {QStringLiteral("version"), 4},
        {QStringLiteral("records"), records},
        {QStringLiteral("answer"), number(session.history.answer())},
        {QStringLiteral("programmerAnswer"), integerJson(session.history.programmerAnswer())},
        {QStringLiteral("normalInput"), inputJson(session.normalInput)}, {QStringLiteral("customInput"), inputJson(session.customInput)},
        {QStringLiteral("programmerInput"), inputJson(session.programmerInput)},
        {QStringLiteral("input"), inputJson(session.input)}, {QStringLiteral("draft"), inputJson(session.draft)},
        {QStringLiteral("historyPosition"), session.historyPosition}, {QStringLiteral("recalledInputs"), recalled}
    }).toJson(QJsonDocument::Indented);
}

QString SessionFormat::decode(const QByteArray &bytes, CalculationSession &session)
{
    if (bytes.size() > maximumBytes) return QStringLiteral("会话文件超过 16 MiB，未载入。");
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    const QString invalid = QStringLiteral("会话文件损坏或数据不一致，当前会话与原文件保持不变。");
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return invalid;
    const auto root = document.object();
    if (root.value(QStringLiteral("format")) != QStringLiteral("CalcTabdd.Session")) return invalid;
    if (root.value(QStringLiteral("version")) != QJsonValue(4))
        return QStringLiteral("不支持此会话格式版本，原文件未修改。");
    if (root.size() != 12) return invalid;
    CalculationSession restored;
    if (!root.value(QStringLiteral("records")).isArray()) return invalid;
    const auto array = root.value(QStringLiteral("records")).toArray();
    if (array.size() > maximumRecords) return QStringLiteral("会话超过 10000 条记录，未载入。");
    QVector<CalculationRecord> records;
    records.reserve(array.size());
    for (const auto &value : array)
    {
        if (!value.isObject()) return invalid;
        const auto object = value.toObject();
        CalculationRecord entry;
        entry.id = static_cast<quint64>(records.size()) + 1;
        if (object.value(QStringLiteral("id")) != QString::number(entry.id)
            || !object.value(QStringLiteral("expression")).isString()
            || !object.value(QStringLiteral("ok")).isBool()) return invalid;
        if (!object.value(QStringLiteral("customDefinition")).isString()
            || !object.value(QStringLiteral("parameterInput")).isString()) return invalid;
        entry.customDefinition = object.value(QStringLiteral("customDefinition")).toString();
        entry.parameterInput = object.value(QStringLiteral("parameterInput")).toString();
        entry.programmer = object.value(QStringLiteral("mode")) == QStringLiteral("programmer");
        if (object.value(QStringLiteral("mode")) != modeName(entry.programmer, entry.customDefinition)
            || object.size() != 11 + (entry.programmer ? 2 : 0) + (object.value(QStringLiteral("ok")).toBool() ? 0 : 1))
            return invalid;
        if (entry.programmer && (!readConfig(object.value(QStringLiteral("programmerConfig")), entry.programmerConfig)
            || !readProgrammerValue(object.value(QStringLiteral("programmerAnswerBefore")), entry.programmerAnswerBefore)))
            return invalid;
        if (!entry.programmer && (object.contains(QStringLiteral("programmerConfig"))
            || object.contains(QStringLiteral("programmerAnswerBefore")))) return invalid;
        entry.expression = object.value(QStringLiteral("expression")).toString();
        entry.result.ok = object.value(QStringLiteral("ok")).toBool();
        int kind = 0;
        if (!readNumber(object.value(QStringLiteral("answerBefore")), entry.answerBefore)
            || !readInteger(object.value(QStringLiteral("error")), kind, 0, static_cast<int>(CalculationError::Limit))
            || !readInteger(object.value(QStringLiteral("errorPosition")), entry.result.errorPosition, -1, entry.expression.size())
            || !readInteger(object.value(QStringLiteral("errorLength")), entry.result.errorLength, 0, entry.expression.size()))
            return invalid;
        if (entry.result.ok)
        {
            if (object.contains(QStringLiteral("text"))) return invalid;
            if (entry.programmer)
            {
                if (!readProgrammerValue(object.value(QStringLiteral("value")), entry.programmerValue)) return invalid;
            }
            else if (!readNumber(object.value(QStringLiteral("value")), entry.result.value)) return invalid;
            entry.result.text = entry.valueText();
        }
        else
        {
            if (!object.value(QStringLiteral("value")).isNull() || !object.value(QStringLiteral("text")).isString()) return invalid;
            entry.result.text = object.value(QStringLiteral("text")).toString();
        }
        entry.result.error = static_cast<CalculationError>(kind);
        records.append(entry);
    }
    if (!restored.history.restoreRecords(records)) return invalid;
    NumericValue answer;
    ProgrammerValue programmerAnswer;
    if (!readNumber(root.value(QStringLiteral("answer")), answer)
        || answer != restored.history.answer()
        || !readProgrammerValue(root.value(QStringLiteral("programmerAnswer")), programmerAnswer)
        || programmerAnswer != restored.history.programmerAnswer()
        || !readInput(root.value(QStringLiteral("input")), restored.input)
        || !readInput(root.value(QStringLiteral("draft")), restored.draft)
        || !readInteger(root.value(QStringLiteral("historyPosition")), restored.historyPosition, -1, records.size() - 1)
        || !root.value(QStringLiteral("recalledInputs")).isObject()) return invalid;
    if (!readInput(root.value(QStringLiteral("normalInput")), restored.normalInput)
        || restored.normalInput.programmer || !restored.normalInput.customDefinition.isEmpty()
        || !readInput(root.value(QStringLiteral("customInput")), restored.customInput)
        || restored.customInput.programmer
        || (restored.customInput.customDefinition.isEmpty() && !restored.customInput.text.isEmpty())
        || !readInput(root.value(QStringLiteral("programmerInput")), restored.programmerInput)
        || !restored.programmerInput.programmer) return invalid;
    const auto recalled = root.value(QStringLiteral("recalledInputs")).toObject();
    for (auto it = recalled.begin(); it != recalled.end(); ++it)
    {
        bool ok = false;
        const quint64 id = it.key().toULongLong(&ok);
        CalculationInputState input;
        const auto *record = restored.history.record(id);
        if (!ok || QString::number(id) != it.key() || !record || !readInput(it.value(), input)
            || input.programmer != record->programmer || input.customDefinition != record->customDefinition)
            return invalid;
        restored.recalledInputs.insert(id, input);
    }
    if (restored.historyPosition < 0)
    {
        if (!recalled.isEmpty() || inputJson(restored.draft) != inputJson(CalculationInputState())) return invalid;
    }
    else
    {
        const quint64 id = records.at(restored.historyPosition).id;
        if (!restored.recalledInputs.contains(id) || inputJson(restored.input) != inputJson(restored.recalledInputs.value(id)))
            return invalid;
    }
    session = restored;
    return {};
}

CalculationSessionFile::CalculationSessionFile() = default;
CalculationSessionFile::~CalculationSessionFile() = default;

void CalculationSessionFile::release()
{
    m_lock.reset();
    m_path.clear();
    m_digest.clear();
}

QString CalculationSessionFile::acquire(const QString &path)
{
    if (m_lock) return QStringLiteral("当前文件已打开，请使用新的会话文件对象。");
    const QFileInfo info(path);
    const QString directory = info.dir().canonicalPath();
    if (path.isEmpty() || directory.isEmpty() || info.isSymLink())
        return QStringLiteral("请选择存在的本地目录中的普通会话文件，不能使用符号链接。");
    m_path = QDir(directory).filePath(info.fileName());
    m_lock.reset(new QLockFile(m_path + QStringLiteral(".lock")));
    // 会话可能打开数小时；不因锁文件的年龄抢占仍在使用的会话。
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0))
    {
        const QString error = m_lock->error() == QLockFile::LockFailedError
            ? QStringLiteral("会话正在其他窗口或进程使用，请先关闭该会话或选择其他文件。")
            : QStringLiteral("无法锁定会话文件，请检查目录权限和可用空间。");
        release();
        return error;
    }
    return {};
}

QString CalculationSessionFile::create(const QString &path, const CalculationSession &session)
{
    QString error = acquire(path);
    if (!error.isEmpty()) return error;
    error = save(session);
    if (!error.isEmpty()) release();
    return error;
}

QString CalculationSessionFile::load(const QString &path, CalculationSession &session)
{
    QString error = acquire(path);
    if (!error.isEmpty()) return error;
    QByteArray bytes;
    error = readFile(m_path, bytes);
    if (error.isEmpty()) error = SessionFormat::decode(bytes, session);
    if (!error.isEmpty()) release();
    else m_digest = digest(bytes);
    return error;
}

QString CalculationSessionFile::save(const CalculationSession &session)
{
    if (!m_lock || !m_lock->isLocked()) return QStringLiteral("本地保存尚未开启。");
    const QByteArray bytes = SessionFormat::encode(session);
    CalculationSession checked;
    QString error = SessionFormat::decode(bytes, checked);
    if (!error.isEmpty()) return error;
    if (!m_digest.isEmpty())
    {
        QByteArray current;
        error = readFile(m_path, current);
        if (!error.isEmpty()) return error;
        if (digest(current) != m_digest)
            return QStringLiteral("文件已被外部修改，未覆盖；请另存到新文件。");
    }
    else if (QFileInfo::exists(m_path) || QFileInfo(m_path).isSymLink())
        return QStringLiteral("文件已存在，未覆盖；请恢复该文件，或选择新的文件名。");
    QSaveFile file(m_path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return QStringLiteral("无法保存会话：%1").arg(file.errorString());
    if (file.write(bytes) != bytes.size())
    {
        error = QStringLiteral("会话写入未完成：%1").arg(file.errorString());
        file.cancelWriting();
        return error;
    }
    if (!file.commit()) return QStringLiteral("无法提交会话文件：%1").arg(file.errorString());
    m_digest = digest(bytes);
    return {};
}
