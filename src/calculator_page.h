#pragma once

#include "calculation_history.h"
#include <QWidget>
#include <QHash>
#include <QPointer>

class FormulaCompletion;
class QLabel;
class QDialog;
class QPushButton;
class QPlainTextEdit;
class QScrollArea;
class QVBoxLayout;

class CalculatorPage : public QWidget
{
    Q_OBJECT
public:
    explicit CalculatorPage(QWidget *parent = nullptr);
    QPlainTextEdit *input() const { return m_input; }
    int recordCount() const { return m_history.count(); }
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
    struct InputState
    {
        QString text;
        int position = 0;
        int anchor = 0;
    };
    InputState captureInput() const;
    void restoreInput(const InputState &state);
    void recallHistory(bool older);
    void showHistory(int position);
    void restoreDraft();
    void requestClearSession();
    void clearSession();
    void appendRecord(const CalculationRecord &entry);
    void reuseFormula(quint64 id);
    void copyRecord(quint64 id, bool valueOnly);
    void insertResult(quint64 id);
    void applyTheme();
    QPlainTextEdit *m_input = nullptr;
    FormulaCompletion *m_completion = nullptr;
    QPointer<QWidget> m_editTarget;
    QScrollArea *m_scroll = nullptr;
    QVBoxLayout *m_records = nullptr;
    QLabel *m_count = nullptr;
    QPushButton *m_clearButton = nullptr;
    QPointer<QDialog> m_clearConfirmation;
    QLabel *m_empty = nullptr;
    QLabel *m_status = nullptr;
    CalculationHistory m_history;
    // -1 表示原草稿；仅保存本轮浏览过的记录的临时编辑，不改历史模型。
    int m_historyPosition = -1;
    InputState m_draft;
    QHash<quint64, InputState> m_recalledInputs;
    bool m_composing = false;
    bool m_followLatest = true;
    bool m_applyingTheme = false;
};
