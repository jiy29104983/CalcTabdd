#pragma once

#include <QLayout>
#include <QFont>
#include <QPalette>
#include <QString>

class QDialog;
class QAbstractButton;

namespace CalculatorStyle {
void prepareButton(QAbstractButton *button);
QPalette palette(const QPalette &source);
QString sheet(const QPalette &source, const QFont &font);
void applyDialog(QDialog *dialog, const QPalette &source);
}

// 根据实际字体和按钮 sizeHint 换行，不依赖固定窗口宽度或 DPI。
class ButtonFlowLayout : public QLayout
{
public:
    explicit ButtonFlowLayout();
    ~ButtonFlowLayout() override;
    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    Qt::Orientations expandingDirections() const override { return {}; }
    void setGeometry(const QRect &rect) override;
private:
    int arrange(const QRect &rect, bool apply) const;
    QList<QLayoutItem *> m_items;
};
