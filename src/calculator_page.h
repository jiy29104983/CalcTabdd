#pragma once

#include "calculation_history.h"
#include <QWidget>
#include <QPointer>

class QLabel;
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

private:
    void appendRecord(const CalculationRecord &entry);
    void reuseFormula(quint64 id);
    void copyRecord(quint64 id, bool valueOnly);
    void insertResult(quint64 id);
    void applyTheme();
    QPlainTextEdit *m_input = nullptr;
    QPointer<QWidget> m_editTarget;
    QScrollArea *m_scroll = nullptr;
    QVBoxLayout *m_records = nullptr;
    QLabel *m_count = nullptr;
    QLabel *m_empty = nullptr;
    QLabel *m_status = nullptr;
    CalculationHistory m_history;
    bool m_composing = false;
    bool m_followLatest = true;
    bool m_applyingTheme = false;
};
