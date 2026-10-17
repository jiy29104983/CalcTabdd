#pragma once

#include <QTextBrowser>

// 可选择的纯文本结果；任意长数字可软换行，复制始终使用未插入换行的原始正文。
class RecordText : public QTextBrowser
{
    Q_OBJECT
public:
    explicit RecordText(const QString &text, QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateHeight();
};
