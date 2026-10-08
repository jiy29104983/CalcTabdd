#pragma once

#include <QTextBrowser>

// 可选择的纯文本结果；任意长数字可软换行，复制始终使用未插入换行的原始正文。
class RecordText : public QTextBrowser
{
    Q_OBJECT
public:
    explicit RecordText(const QString &text, QWidget *parent = nullptr);
    QString text() const { return toPlainText(); }
    void setText(const QString &text) { setPlainText(text); }
    QString selectedText() const { return textCursor().selectedText(); }
    bool hasSelectedText() const { return textCursor().hasSelection(); }
    void setSelection(int start, int length);
    Qt::TextFormat textFormat() const { return Qt::PlainText; }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateHeight();
};
