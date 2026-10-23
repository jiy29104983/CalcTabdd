#pragma once

#include "calculation_history.h"
#include "calculation_export.h"
#include "calculation_session.h"
#include <QWidget>
#include <QPointer>

class QAction;
class QTimer;
class FormulaCompletion;
class QLabel;
class ElidedLabel;
class QDialog;
class QPushButton;
class QToolButton;
class QPlainTextEdit;
class QScrollArea;
class QVBoxLayout;

class CalculatorPage : public QWidget
{
    Q_OBJECT
public:
    explicit CalculatorPage(QWidget *parent = nullptr);
    ~CalculatorPage() override;
    QPlainTextEdit *input() const { return m_input; }
    const CalculationHistory &history() const { return m_history; }
    void focusInput();
    void routeEdit(const QString &command);

public slots:
    void submit();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    using InputState = CalculationInputState;
    bool customMode() const { return !m_definition.isEmpty(); }
    // 窗口级模态框打开期间挂起会改写会话或历史的操作。
    bool dialogOpen() const;
    // 模态框或输入法组词期间挂起提交、模式切换与对话框入口。
    bool busy() const;
    void applyDefinition();
    void resetDefinitionEditor(const QString &text);
    bool hasPendingDefinition() const;
    void switchMode(bool custom);
    void updateModeUi();
    void updateDefinitionStateUi();
    void updateRecordUi();
    QString m_definition;
    InputState m_normalInput;
    InputState m_customInput;
    QWidget *m_definitionPanel = nullptr;
    QPlainTextEdit *m_definitionInput = nullptr;
    FormulaCompletion *m_definitionCompletion = nullptr;
    QLabel *m_definitionStateBadge = nullptr;
    QLabel *m_definitionHint = nullptr;
    QLabel *m_definitionError = nullptr;
    QPushButton *m_applyDefinitionButton = nullptr;
    QWidget *m_parameterHeader = nullptr;
    ElidedLabel *m_parameterChipsBadge = nullptr;
    bool m_defining = false;
    QLabel *m_inputLabel = nullptr;
    QLabel *m_historyBadge = nullptr;
    QToolButton *m_normalModeButton = nullptr;
    QToolButton *m_customModeButton = nullptr;
    QAction *m_normalModeAction = nullptr;
    QAction *m_customModeAction = nullptr;
    CalculationSession captureSession() const;
    void restoreSession(const CalculationSession &session);
    void requestSessionFile(bool restore);
    void scheduleSave();
    bool saveSession();
    void stopSaving();
    void updateSessionStatus(const QString &error = QString());
    InputState captureInput() const;
    void restoreInput(const InputState &state);
    void recallHistory(bool older);
    void showHistory(int position);
    void restoreDraft();
    void requestExport(CalculationExport::Format format);
    void confirmExport(const QString &path, const QByteArray &contents);
    void writeExport(const QString &path, const QByteArray &contents);
    void requestClearSession();
    void clearSession();
    void appendRecord(const CalculationRecord &entry);
    void reuseFormula(quint64 id);
    void copyRecord(quint64 id, bool valueOnly);
    void insertResult(quint64 id);
    void showInputError(const CalculationResult &result, int offset, bool moveCursor = true);
    void clearInputError();
    void applyErrorHighlight();
    void applyTheme();
    QToolButton *m_sessionButton = nullptr;
    QAction *m_saveNowAction = nullptr;
    QAction *m_stopSavingAction = nullptr;
    QLabel *m_sessionStatus = nullptr;
    QPointer<QDialog> m_sessionDialog;
    QTimer *m_saveTimer = nullptr;
    std::unique_ptr<CalculationSessionFile> m_sessionFile;
    bool m_sessionDirty = false;
    QPlainTextEdit *m_input = nullptr;
    FormulaCompletion *m_completion = nullptr;
    QPointer<QWidget> m_editTarget;
    QScrollArea *m_scroll = nullptr;
    QVBoxLayout *m_records = nullptr;
    ElidedLabel *m_answerBadge = nullptr;
    QLabel *m_count = nullptr;
    QPushButton *m_clearButton = nullptr;
    QPointer<QDialog> m_clearConfirmation;
    QToolButton *m_exportButton = nullptr;
    QPointer<QDialog> m_exportDialog;
    QString m_textExportPath;
    QString m_markdownExportPath;
    QLabel *m_empty = nullptr;
    QLabel *m_status = nullptr;
    CalculationHistory m_history;
    // -1 表示原草稿；仅保存本轮浏览过的记录的临时编辑，不改历史模型。
    int m_historyPosition = -1;
    InputState m_draft;
    QMap<quint64, InputState> m_recalledInputs;
    int m_errorPosition = -1;
    int m_errorLength = 0;
    bool m_composing = false;
    bool m_followLatest = true;
    bool m_applyingTheme = false;
};
