#include "calculator_style.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QAbstractButton>
#include <QEvent>
#include <QFontMetrics>
#include <QVariant>

namespace {
class ButtonMetrics : public QObject
{
public:
    explicit ButtonMetrics(QAbstractButton *button) : QObject(button), m_button(button)
    {
        setObjectName(QStringLiteral("calculatorButtonMetrics"));
        button->installEventFilter(this);
        updateMetrics();
    }
    void updateMetrics()
    {
        const QFontMetrics metrics(m_button->font());
        const bool compact = m_button->property("compact").toBool();
        m_button->setFixedHeight(compact ? qMax(28, metrics.height() + 10)
                                         : qMax(34, metrics.height() + 16));
        if (m_button->objectName() == QStringLiteral("reuseFormula") ||
            m_button->objectName() == QStringLiteral("recordActions"))
        {
            if (QWidget *parent = m_button->parentWidget())
            {
                auto *reuse = parent->findChild<QAbstractButton *>(QStringLiteral("reuseFormula"), Qt::FindDirectChildrenOnly);
                auto *more = parent->findChild<QAbstractButton *>(QStringLiteral("recordActions"), Qt::FindDirectChildrenOnly);
                if (reuse && more)
                {
                    const int width = qMax(metrics.horizontalAdvance(reuse->text()) + 26,
                                           metrics.horizontalAdvance(more->text()) + 36);
                    reuse->setFixedWidth(width);
                    more->setFixedWidth(width);
                }
            }
        }
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) updateMetrics();
        return QObject::eventFilter(object, event);
    }
private:
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
    if (auto *metrics = button->findChild<ButtonMetrics *>(QStringLiteral("calculatorButtonMetrics"), Qt::FindDirectChildrenOnly))
        metrics->updateMetrics();
    else
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
    const QColor base = colors.color(QPalette::Base);
    const QColor text = colors.color(QPalette::Text);
    const QColor accent = colors.color(QPalette::Highlight);
    const bool dark = background.lightness() < 128;
    // 所有控件共用同一组色值；颜色来自所属窗口，不设置全局应用样式。
    QString css = QStringLiteral(R"(
        QDialog, QLabel, QPushButton, QToolButton, QMenu, QLineEdit, QComboBox, QTabBar, QTextBrowser { font-size: @uiSize; }
        QLabel#calculatorTitle { font-size: @titleSize; font-weight: bold; }
        RecordText#recordFormula { font-size: @formulaSize; }
        RecordText#recordResult[error="false"] { font-size: @resultSize; font-weight: bold; }
        QDialog, QWidget#calctabddPage { background: @window; color: @text; }
        QLabel { color: @text; background: transparent; }
        RecordText#recordFormula, RecordText#recordResult, RecordText#recordHex,
        RecordText#recordBin, RecordText#recordOct { color: @text; background: transparent; border: none; padding: 0; margin: 0; }
        RecordText#recordSources { color: @muted; background: transparent; border: none; padding: 0; margin: 0; }
        QLabel#recordCount, QLabel#recordNumber,
        QLabel#emptyHistory, QLabel#helpMatchCount, QLabel#clearSessionDetails,
        QLabel#sessionSaveStatus, QLabel#programmerHint { color: @muted; }
        QLabel#sessionSaveStatus { border-bottom: 1px solid @border; }
        QLabel#sessionSaveStatus[error="true"] { color: @error; border-bottom-color: @errorBorder; }
        QLabel#recordCount { background: @hover; border-radius: 10px; padding: 3px 10px; font-size: @captionSize; }
        QLabel#answerBadge {
            background: @accentSoft; color: @text; border: 1px solid @accentBorder;
            border-radius: 10px; padding: 3px 10px; font-size: @captionSize; font-weight: bold;
        }
        QLabel#recordNumber {
            background: @hover; color: @muted; border-radius: 4px;
            padding: 2px 7px; font-size: @captionSize; font-weight: bold;
        }
        QLabel#recordModeBadge {
            background: @accentSoft; color: @muted; border: 1px solid @accentBorder;
            border-radius: 4px; padding: 1px 7px; font-size: @captionSize;
        }
        QLabel#recordStatusBadge {
            background: @errorBackground; color: @error; border: 1px solid @errorBorder;
            border-radius: 4px; padding: 1px 7px; font-size: @captionSize; font-weight: bold;
        }
        RecordText#recordCustomMeta {
            background: @hover; color: @muted; border: none; border-radius: 4px;
            padding: 3px 8px; font-size: @captionSize;
        }
        QLabel#customDefinitionLabel, QLabel#parameterSectionLabel {
            color: @text; font-weight: bold; font-size: @captionSize;
        }
        QLabel#customDefinitionStateBadge {
            background: @accentSoft; color: @muted; border: 1px solid @accentBorder;
            border-radius: 4px; padding: 1px 7px; font-size: @captionSize;
        }
        QLabel#customDefinitionStateBadge[pending="true"] {
            background: @warningBackground; color: @warningText; border: 1px solid @warningBorder; font-weight: bold;
        }
        QLabel#historyRecallBadge {
            background: @hover; color: @muted; border: 1px solid @quietBorder;
            border-radius: 4px; padding: 1px 8px; font-size: @captionSize;
        }
        QLabel#emptyHistory {
            background: @card; color: @muted; border: 1px dashed @border;
            border-radius: 10px; padding: 22px 20px;
        }
        QFrame#calculatorHeader { background: @base; border-bottom: 1px solid @border; }
        QScrollArea#formulaComposerScroll { background: @base; border: none; }
        QFrame#formulaComposer { background: @base; border-top: 1px solid @border; }
        QFrame#modeSwitcher { background: @hover; border: 1px solid @border; border-radius: 7px; }
        QScrollArea#calculationHistory, QWidget#historyCanvas { background: @window; border: none; }
        QFrame#calculationRecord {
            background: @card; border: 1px solid @border;
            border-left: 3px solid @accent; border-radius: 8px;
        }
        QFrame#calculationRecord[error="true"] {
            background: @errorCard; border: 1px solid @errorBorder;
            border-left: 3px solid @error; border-radius: 8px;
        }
        RecordText#recordResult[error="true"], QLabel#customDefinitionError { color: @error; }
        QPushButton, QToolButton {
            background: @button; color: @text; border: 1px solid @border;
            border-radius: 6px; padding: 5px 12px;
        }
        QToolButton { padding: 4px; }
        QToolButton[calculatorMenu="true"] { padding: 5px 24px 5px 12px; }
        QPushButton[compact="true"] { padding: 3px 10px; border-radius: 5px; font-size: @captionSize; }
        QToolButton[compact="true"][calculatorMenu="true"] { padding: 3px 22px 3px 10px; border-radius: 5px; font-size: @captionSize; }
        QToolButton::menu-indicator { subcontrol-origin: padding; subcontrol-position: right center; right: 8px; }
        QPushButton:hover, QToolButton:hover { background: @hover; border-color: @muted; }
        QPushButton:pressed, QToolButton:pressed { background: @pressed; }
        QPushButton:focus, QToolButton:focus { border-color: @accent; }
        QPushButton[role="quiet"], QToolButton[role="quiet"] {
            background: @quietBg; border: 1px solid @quietBorder; color: @muted;
        }
        QPushButton[role="quiet"]:hover, QToolButton[role="quiet"]:hover {
            background: @hover; border-color: @border; color: @text;
        }
        QToolButton[modeSegment="true"] {
            background: transparent; color: @muted; border: 1px solid transparent;
            border-radius: 5px; padding: 3px 12px; font-size: @captionSize;
        }
        QToolButton[modeSegment="true"]:hover { color: @text; background: @quietBg; }
        QToolButton[modeSegment="true"]:checked {
            background: @base; color: @text; border: 1px solid @border; font-weight: bold;
        }
        QPushButton[role="primary"] {
            background: @accent; color: @accentText; border: 1px solid @accent; font-weight: bold;
        }
        QPushButton[role="primary"]:hover { background: @accentHover; border-color: @accentHover; }
        QPushButton[role="primary"]:pressed { background: @accentPressed; border-color: @accentPressed; }
        QPushButton[role="primary"]:focus { border: 2px solid @accentFocus; padding: 4px 11px; }
        QPushButton[role="danger"] { color: @error; border-color: @error; }
        QPushButton[role="danger"]:hover { background: @errorBackground; }
        QPushButton[role="danger"]:focus { border: 2px solid @error; padding: 4px 11px; }
        QPushButton:disabled, QToolButton:disabled { background: @window; color: @disabled; border-color: @border; }
        QPlainTextEdit, QLineEdit, QTextBrowser, QComboBox {
            background: @base; color: @text; border: 1px solid @border;
            border-radius: 6px; padding: 7px 8px; selection-background-color: @accent; selection-color: @accentText;
        }
        QPlainTextEdit:focus, QLineEdit:focus, QTextBrowser:focus, QComboBox:focus { border-color: @accent; }
        QAbstractItemView { background: @base; color: @text; selection-background-color: @accent; selection-color: @accentText; }
        QAbstractItemView#formulaCompletionPopup { border: 1px solid @border; border-radius: 6px; padding: 4px; outline: none; }
        QAbstractItemView#formulaCompletionPopup::item { padding: 4px 6px; border-radius: 5px; }
        QMenu { background: @base; color: @text; border: 1px solid @border; padding: 6px; }
        QMenu::item { padding: 7px 24px; border-radius: 4px; }
        QMenu::item:selected { background: @accent; color: @accentText; }
        QMenu::item:disabled { color: @disabled; }
        QMenu::separator { height: 1px; background: @border; margin: 5px 8px; }
        QTabWidget::pane { border: 1px solid @border; border-radius: 6px; }
        QTabBar::tab { background: @window; color: @text; padding: 9px 18px; border-bottom: 2px solid transparent; }
        QTabBar::tab:selected { background: @base; border-bottom-color: @accent; font-weight: bold; }
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
    css.replace(QStringLiteral("@captionSize"), QString::number(qMax(9.0, size - 1.0)) + unit);
    css.replace(QStringLiteral("@uiSize"), QString::number(size) + unit);
    css.replace(QStringLiteral("@titleSize"), QString::number(size + 2) + unit);
    css.replace(QStringLiteral("@formulaSize"), QString::number(qMax(11.0, size + 1)) + unit);
    css.replace(QStringLiteral("@resultSize"), QString::number(qMax(11.0, size + 1) + 4) + unit);
    // 替换长标记在先，避免 @accent 吞掉 @accentText 等标记。
    css.replace(QStringLiteral("@accentPressed"), accent.darker(120).name());
    css.replace(QStringLiteral("@accentHover"), accent.darker(110).name());
    css.replace(QStringLiteral("@accentFocus"), (dark ? accent.lighter(135) : accent.darker(125)).name());
    css.replace(QStringLiteral("@accentBorder"), blend(base, accent, dark ? 38 : 28).name());
    css.replace(QStringLiteral("@accentSoft"), blend(base, accent, dark ? 18 : 10).name());
    css.replace(QStringLiteral("@accentText"), colors.color(QPalette::HighlightedText).name());
    const QColor error(dark ? "#f2a49a" : "#b3443b");
    css.replace(QStringLiteral("@errorBackground"), blend(background, error, 12).name());
    css.replace(QStringLiteral("@errorBorder"), blend(background, error, 32).name());
    css.replace(QStringLiteral("@errorCard"), blend(base, error, dark ? 8 : 5).name());
    css.replace(QStringLiteral("@error"), error.name());
    const QColor warning(dark ? "#f0c36d" : "#9a6700");
    css.replace(QStringLiteral("@warningBackground"), blend(base, warning, dark ? 18 : 12).name());
    css.replace(QStringLiteral("@warningBorder"), blend(base, warning, 36).name());
    css.replace(QStringLiteral("@warningText"), warning.name());
    css.replace(QStringLiteral("@quietBorder"), blend(background, text, dark ? 14 : 11).name());
    css.replace(QStringLiteral("@quietBg"), blend(base, text, dark ? 4 : 3).name());
    css.replace(QStringLiteral("@window"), background.name());
    css.replace(QStringLiteral("@card"), (dark ? blend(base, text, 4) : base).name());
    css.replace(QStringLiteral("@base"), base.name());
    css.replace(QStringLiteral("@text"), text.name());
    css.replace(QStringLiteral("@border"), blend(background, text, dark ? 18 : 15).name());
    css.replace(QStringLiteral("@muted"), blend(background, text, 70).name());
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
QLayoutItem *itemAtHelper(const QList<QLayoutItem *> &items, int index)
{
    return items.value(index);
}
QLayoutItem *ButtonFlowLayout::itemAt(int index) const
{
    return itemAtHelper(m_items, index);
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
