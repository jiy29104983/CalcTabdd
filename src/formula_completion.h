#pragma once

#include <QObject>
#include <QString>

class QCompleter;
class QPlainTextEdit;
class QStandardItemModel;

class FormulaCompletion : public QObject
{
    Q_OBJECT
public:
    explicit FormulaCompletion(QPlainTextEdit *input);

signals:
    void calculationRequested();
    void hintChanged(const QString &text);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    struct Token
    {
        int start = -1;
        int end = -1;
        QString query;
    };
    Token tokenAtCursor() const;
    void refresh();
    void insertCurrent(int entryIndex);
    void hide();
    QPlainTextEdit *m_input;
    QCompleter *m_completer;
    QStandardItemModel *m_model;
    bool m_composing = false;
    bool m_inserting = false;
    QString m_dismissedText;
    int m_dismissedPosition = -1;
};
