#include "calculator_style.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QAbstractButton>
#include <QEvent>
#include <QFontMetrics>

namespace {
class ButtonMetrics : public QObject
{
public:
    explicit ButtonMetrics(QAbstractButton *button) : QObject(button), m_button(button)
    {
        setObjectName(QStringLiteral("calculatorButtonMetrics"));
        button->installEventFilter(this);
        updateHeight();
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) updateHeight();
        return QObject::eventFilter(object, event);
    }
private:
    void updateHeight()
    {
        m_button->setFixedHeight(qMax(36, QFontMetrics(m_button->font()).height() + 18));
    }
    QAbstractButton *m_button;
};

QColor blend(const QColor &base, const QColor &ink, int percent)
{
    return QColor((base.red() * (100 - percent) + ink.red() * percent) / 100,
                  (base.green() * (100 - percent) + ink.green() * percent) / 100,
                  (base.blue() * (100 - percent) + ink.blue() * percent) / 100);
}
}

void CalculatorStyle::prepareButton(QAbstractButton *button)
{
    button->ensurePolished();
    if (!button->findChild<QObject *>(QStringLiteral("calculatorButtonMetrics"), Qt::FindDirectChildrenOnly))
        new ButtonMetrics(button);
}

QPalette CalculatorStyle::palette(const QPalette &source)
{
    QPalette colors = source;
    const QColor background = source.color(QPalette::Window);
    const QColor text = source.color(QPalette::Text);
    colors.setColor(QPalette::WindowText, text);
    colors.setColor(QPalette::Button, blend(background, text, 5));
    colors.setColor(QPalette::ButtonText, text);
    colors.setColor(QPalette::AlternateBase, blend(source.color(QPalette::Base), text, 5));
    colors.setColor(QPalette::Disabled, QPalette::ButtonText, blend(background, text, 45));
    colors.setColor(QPalette::Disabled, QPalette::Text, blend(background, text, 45));
    return colors;
}

QString CalculatorStyle::sheet(const QPalette &source, const QFont &font)
{
    const QPalette colors = palette(source);
    const QColor background = colors.color(QPalette::Window);
    const QColor text = colors.color(QPalette::Text);
    const QColor accent = colors.color(QPalette::Highlight);
    // 所有控件共用同一组色值；颜色来自所属窗口，不设置全局应用样式。
    QString css = QStringLiteral(R"(
        QDialog, QLabel, QPushButton, QToolButton, QMenu, QLineEdit, QComboBox, QTabBar, QTextBrowser { font-size: @uiSize; }
        QLabel#calculatorTitle { font-size: @titleSize; font-weight: bold; }
        QLabel#recordFormula, QLabel#currentCustomDefinition { font-size: @formulaSize; }
        RecordText#recordResult[error="false"] { font-size: @resultSize; }
        QDialog, QWidget#calctabddPage { background: @window; color: @text; }
        QLabel { color: @text; background: transparent; }
        RecordText#recordResult, RecordText#recordSources { color: @text; background: transparent; border: none; padding: 0; margin: 0; }
        QLabel#recordCount, QLabel#recordNumber, QLabel#inputHint, QLabel#emptyHistory,
        QLabel#helpMatchCount, QLabel#clearSessionDetails { color: @muted; }
        QLabel#recordCount { background: @hover; border-radius: 10px; padding: 4px 10px; }
        QFrame#calculatorHeader, QFrame#formulaComposer { background: @base; }
        QFrame#formulaComposer { border-top: 1px solid @border; }
        QScrollArea#calculationHistory, QWidget#historyCanvas { background: @window; border: none; }
        QFrame#calculationRecord { background: @card; border: 1px solid @border; border-radius: 8px; }
        QLabel#currentCustomDefinition { background: @hover; border-radius: 6px; padding: 8px 12px; }
        RecordText#recordResult[error="true"], QLabel#customDefinitionError { color: @error; }
        QPushButton, QToolButton {
            background: @button; color: @text; border: 1px solid @border;
            border-radius: 6px; padding: 6px 12px;
        }
        QToolButton { padding: 4px; }
        QToolButton[calculatorMenu="true"] { padding: 6px 24px 6px 12px; }
        QToolButton::menu-indicator { subcontrol-origin: padding; subcontrol-position: right center; right: 8px; }
        QPushButton:hover, QToolButton:hover { background: @hover; border-color: @muted; }
        QPushButton:pressed, QToolButton:pressed { background: @pressed; }
        QPushButton:focus, QToolButton:focus { border-color: @accent; }
        QPushButton[role="quiet"], QToolButton[role="quiet"] { background: transparent; }
        QPushButton[role="quiet"]:hover, QToolButton[role="quiet"]:hover { background: @hover; }
        QPushButton[role="primary"] { background: @accent; color: @accentText; border-color: @accent; font-weight: bold; }
        QPushButton[role="primary"]:hover { background: @accentHover; }
        QPushButton[role="primary"]:pressed { background: @accentPressed; }
        QPushButton[role="primary"]:focus { border: 2px solid @text; padding: 5px 11px; }
        QPushButton[role="danger"] { color: @error; border-color: @error; }
        QPushButton[role="danger"]:hover { background: @errorBackground; }
        QPushButton[role="danger"]:focus { border: 2px solid @error; padding: 5px 11px; }
        QPushButton:disabled, QToolButton:disabled { background: @window; color: @disabled; border-color: @border; }
        QPlainTextEdit, QLineEdit, QTextBrowser, QComboBox {
            background: @base; color: @text; border: 1px solid @border;
            border-radius: 6px; padding: 8px; selection-background-color: @accent; selection-color: @accentText;
        }
        QPlainTextEdit:focus, QLineEdit:focus, QTextBrowser:focus, QComboBox:focus { border-color: @accent; }
        QAbstractItemView { background: @base; color: @text; selection-background-color: @accent; selection-color: @accentText; }
        QAbstractItemView#formulaCompletionPopup { border: 1px solid @border; padding: 4px; }
        QAbstractItemView#formulaCompletionPopup::item { padding: 6px 8px; }
        QMenu { background: @base; color: @text; border: 1px solid @border; padding: 6px; }
        QMenu::item { padding: 7px 24px; border-radius: 4px; }
        QMenu::item:selected { background: @accent; color: @accentText; }
        QMenu::item:disabled { color: @disabled; }
        QMenu::separator { height: 1px; background: @border; margin: 5px 8px; }
        QTabWidget::pane { border: 1px solid @border; border-radius: 6px; }
        QTabBar::tab { background: @window; color: @text; padding: 9px 18px; border-bottom: 2px solid transparent; }
        QTabBar::tab:selected { background: @base; border-bottom-color: @accent; }
        QTabBar::tab:hover { background: @hover; }
        QScrollBar:vertical { background: @window; width: 12px; margin: 0; }
        QScrollBar:horizontal { background: @window; height: 12px; margin: 0; }
        QScrollBar::handle:vertical { background: @border; min-height: 28px; border-radius: 5px; margin: 2px; }
        QScrollBar::handle:horizontal { background: @border; min-width: 28px; border-radius: 5px; margin: 2px; }
        QScrollBar::handle:hover { background: @muted; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QToolTip { background: @base; color: @text; border: 1px solid @border; padding: 5px; }
    )");
    const bool pixels = font.pointSizeF() <= 0;
    const qreal size = pixels ? font.pixelSize() : font.pointSizeF();
    const QString unit = pixels ? QStringLiteral("px") : QStringLiteral("pt");
    css.replace(QStringLiteral("@uiSize"), QString::number(size) + unit);
    css.replace(QStringLiteral("@titleSize"), QString::number(size + 2) + unit);
    css.replace(QStringLiteral("@formulaSize"), QString::number(qMax(11.0, size + 1)) + unit);
    css.replace(QStringLiteral("@resultSize"), QString::number(qMax(11.0, size + 1) + 4) + unit);
    // 替换长标记在先，避免 @accent 吞掉 @accentText 等标记。
    css.replace(QStringLiteral("@accentPressed"), accent.darker(120).name());
    css.replace(QStringLiteral("@accentHover"), accent.darker(110).name());
    css.replace(QStringLiteral("@accentText"), colors.color(QPalette::HighlightedText).name());
    const QColor error(background.lightness() < 128 ? "#f2a49a" : "#b3443b");
    css.replace(QStringLiteral("@errorBackground"), blend(background, error, 12).name());
    css.replace(QStringLiteral("@error"), error.name());
    css.replace(QStringLiteral("@window"), background.name());
    css.replace(QStringLiteral("@card"), (background.lightness() < 128
        ? blend(colors.color(QPalette::Base), text, 3) : colors.color(QPalette::Base)).name());
    css.replace(QStringLiteral("@base"), colors.color(QPalette::Base).name());
    css.replace(QStringLiteral("@text"), text.name());
    css.replace(QStringLiteral("@border"), blend(background, text, 20).name());
    css.replace(QStringLiteral("@muted"), blend(background, text, 72).name());
    css.replace(QStringLiteral("@disabled"), blend(background, text, 45).name());
    css.replace(QStringLiteral("@button"), colors.color(QPalette::Button).name());
    css.replace(QStringLiteral("@hover"), blend(background, text, 8).name());
    css.replace(QStringLiteral("@pressed"), blend(background, text, 14).name());
    css.replace(QStringLiteral("@accent"), accent.name());
    return css;
}

void CalculatorStyle::applyDialog(QDialog *dialog, const QPalette &source)
{
    if (!dialog) return;
    dialog->setPalette(palette(source));
    if (dialog->parentWidget()) dialog->setFont(dialog->parentWidget()->font());
    dialog->setStyleSheet(sheet(source, dialog->font()));
    for (auto *box : dialog->findChildren<QDialogButtonBox *>())
        for (auto *button : box->buttons()) prepareButton(button);
}

ButtonFlowLayout::ButtonFlowLayout()
{
    setContentsMargins(0, 0, 0, 0);
    setSpacing(8);
}

ButtonFlowLayout::~ButtonFlowLayout()
{
    while (auto *item = takeAt(0)) delete item;
}

void ButtonFlowLayout::addItem(QLayoutItem *item)
{
    m_items.append(item);
}
int ButtonFlowLayout::count() const
{
    return m_items.size();
}
QLayoutItem *ButtonFlowLayout::itemAt(int index) const
{
    return m_items.value(index);
}
QLayoutItem *ButtonFlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

QSize ButtonFlowLayout::minimumSize() const
{
    QSize size;
    for (auto *item : m_items) size = size.expandedTo(item->minimumSize());
    return size;
}

QSize ButtonFlowLayout::sizeHint() const
{
    int width = 0;
    for (auto *item : m_items) width += item->sizeHint().width() + spacing();
    return QSize(qMax(0, width - spacing()), minimumSize().height());
}

int ButtonFlowLayout::heightForWidth(int width) const
{
    return arrange(QRect(0, 0, width, 0), false);
}
void ButtonFlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    arrange(rect, true);
}

int ButtonFlowLayout::arrange(const QRect &rect, bool apply) const
{
    int x = rect.x();
    int y = rect.y();
    int height = 0;
    for (auto *item : m_items)
    {
        if (item->isEmpty()) continue;
        const QSize size = item->sizeHint();
        if (x > rect.x() && x + size.width() > rect.x() + rect.width())
        {
            x = rect.x();
            y += height + spacing();
            height = 0;
        }
        if (apply) item->setGeometry(QRect(QPoint(x, y), size));
        x += size.width() + spacing();
        height = qMax(height, size.height());
    }
    return y + height - rect.y();
}
