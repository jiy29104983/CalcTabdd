#include "formula_completion.h"
#include "calculator_style.h"
#include "calculation_catalog.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QInputMethodEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTextCursor>
#include <QTimer>

namespace {
constexpr int EntryRole = Qt::UserRole + 1;
bool isQueryCharacter(QChar ch) { return ch.isLetterOrNumber() || ch == QLatin1Char('_'); }

QColor mix(const QColor &a, const QColor &b, int percentB)
{
    return QColor((a.red() * (100 - percentB) + b.red() * percentB) / 100,
                  (a.green() * (100 - percentB) + b.green() * percentB) / 100,
                  (a.blue() * (100 - percentB) + b.blue() * percentB) / 100);
}

class CompletionItemDelegate : public QStyledItemDelegate
{
public:
    explicit CompletionItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        const QFontMetrics fm(option.font);
        return QSize(qMax(380, option.rect.width()), fm.height() * 2 + 18);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const auto &entries = CalculationCatalog::entries();
        const int entryIndex = index.data(EntryRole).toInt();
        if (entryIndex < 0 || entryIndex >= entries.size())
        {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        const auto &entry = entries.at(entryIndex);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const QPalette &pal = option.palette;
        const QColor base = pal.color(QPalette::Base);
        const QColor text = pal.color(QPalette::Text);
        const QColor accent = pal.color(QPalette::Highlight);
        const QColor accentText = pal.color(QPalette::HighlightedText);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;

        painter->fillRect(option.rect, base);
        const QRect card = option.rect.adjusted(4, 2, -4, -2);
        if (selected || hovered)
        {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? accent : mix(base, text, 9));
            painter->drawRoundedRect(card, 6, 6);
        }

        const QRect inner = card.adjusted(10, 5, -10, -5);
        QFont uiFont = option.font;
        QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (uiFont.pointSizeF() > 0) monoFont.setPointSizeF(uiFont.pointSizeF());
        else if (uiFont.pixelSize() > 0) monoFont.setPixelSize(uiFont.pixelSize());
        monoFont.setBold(true);

        QFont badgeFont = uiFont;
        if (badgeFont.pointSizeF() > 0) badgeFont.setPointSizeF(qMax(8.5, badgeFont.pointSizeF() - 1.0));
        const QFontMetrics uiFm(uiFont);
        const QFontMetrics monoFm(monoFont);
        const QFontMetrics badgeFm(badgeFont);

        const int line1Height = qMax(uiFm.height(), monoFm.height());
        const QRect row1(inner.left(), inner.top(), inner.width(), line1Height);
        const QRect row2(inner.left(), inner.bottom() - uiFm.height() + 1, inner.width(), uiFm.height());

        const int badgeWidth = badgeFm.horizontalAdvance(entry.category) + 14;
        const int badgeHeight = qMin(line1Height, badgeFm.height() + 4);
        const QRect badgeRect(row1.right() - badgeWidth + 1,
                              row1.top() + (row1.height() - badgeHeight) / 2,
                              badgeWidth, badgeHeight);
        const QColor badgeBg = selected ? mix(accent, accentText, 20) : mix(base, text, 10);
        const QColor mutedText = selected ? mix(accent, accentText, 84) : mix(base, text, 68);

        painter->setPen(Qt::NoPen);
        painter->setBrush(badgeBg);
        painter->drawRoundedRect(badgeRect, 4, 4);
        painter->setFont(badgeFont);
        painter->setPen(selected ? accentText : mutedText);
        painter->drawText(badgeRect, Qt::AlignCenter, entry.category);

        const int availableLeft = qMax(40, badgeRect.left() - row1.left() - 8);
        painter->setFont(monoFont);
        painter->setPen(selected ? accentText : (base.lightness() < 128 ? accent.lighter(130) : accent.darker(110)));
        const QString sigText = monoFm.elidedText(entry.signature, Qt::ElideRight, availableLeft);
        painter->drawText(row1.left(), row1.top(), availableLeft, row1.height(),
                          Qt::AlignLeft | Qt::AlignVCenter, sigText);
        const int titleLeft = row1.left() + monoFm.horizontalAdvance(sigText) + 10;
        const int titleWidth = badgeRect.left() - titleLeft - 8;
        if (titleWidth > 20)
        {
            painter->setFont(uiFont);
            painter->setPen(selected ? accentText : text);
            const QString titleText = uiFm.elidedText(entry.title, Qt::ElideRight, titleWidth);
            painter->drawText(titleLeft, row1.top(), titleWidth, row1.height(),
                              Qt::AlignLeft | Qt::AlignVCenter, titleText);
        }

        painter->setFont(uiFont);
        painter->setPen(mutedText);
        const QString exampleText = uiFm.elidedText(QStringLiteral("示例：") + entry.example, Qt::ElideRight, row2.width());
        painter->drawText(row2, Qt::AlignLeft | Qt::AlignVCenter, exampleText);
        painter->restore();
    }
};
}

FormulaCompletion::FormulaCompletion(QPlainTextEdit *input, bool definition)
    : QObject(input), m_input(input), m_completer(new QCompleter(this)), m_model(new QStandardItemModel(this)), m_definition(definition)
{
    m_completer->setModel(m_model);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setMaxVisibleItems(6);
    m_completer->setWrapAround(true);
    m_completer->setWidget(input);
    m_completer->popup()->setObjectName(QStringLiteral("formulaCompletionPopup"));
    m_completer->popup()->setAccessibleName(QStringLiteral("函数和常量候选"));
    m_completer->popup()->setMouseTracking(true);
    m_completer->popup()->setItemDelegate(new CompletionItemDelegate(m_completer->popup()));
    // Install after QCompleter so Ctrl+Enter and IME keys keep their own meaning.
    input->installEventFilter(this);
    m_completer->popup()->installEventFilter(this);
    connect(input, &QPlainTextEdit::textChanged, this, &FormulaCompletion::refresh);
    connect(input, &QPlainTextEdit::cursorPositionChanged, this, &FormulaCompletion::refresh);
    connect(input, &QPlainTextEdit::selectionChanged, this, &FormulaCompletion::refresh);
    connect(m_completer, QOverload<const QModelIndex &>::of(&QCompleter::activated), this,
            [this](const QModelIndex &index) { insertCurrent(index.data(EntryRole).toInt()); });
    connect(m_completer, QOverload<const QModelIndex &>::of(&QCompleter::highlighted), this,
            [this](const QModelIndex &index) {
        if (!index.isValid()) return;
        const auto &entry = CalculationCatalog::entries().at(index.data(EntryRole).toInt());
        emit hintChanged(entry.signature + QStringLiteral(" · ") + entry.description + QStringLiteral(" 示例：") + entry.example);
    });
}

FormulaCompletion::Token FormulaCompletion::tokenAtCursor() const
{
    const QTextCursor cursor = m_input->textCursor();
    if (cursor.isNull() || cursor.hasSelection()) return {};
    const QString text = m_input->toPlainText();
    int start = cursor.position();
    if (start < 0 || start > text.size()) return {};
    while (start > 0 && isQueryCharacter(text.at(start - 1))) --start;
    if (start == 0 || text.at(start - 1) != QLatin1Char('@')) return {};
    --start;
    int previous = start - 1;
    while (previous >= 0 && text.at(previous).isSpace()) --previous;
    if (previous >= 0 && !QStringLiteral("(,+-*/%^×÷−").contains(text.at(previous))
        && !(m_definition && text.at(previous) == QLatin1Char('='))) return {};
    int end = cursor.position();
    while (end < text.size() && isQueryCharacter(text.at(end))) ++end;
    return {start, end, text.mid(start + 1, cursor.position() - start - 1)};
}

bool FormulaCompletion::hasVisiblePopup() const
{
    return m_completer->popup()->isVisible();
}

void FormulaCompletion::hide()
{
    m_completer->popup()->hide();
}

void FormulaCompletion::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled) hide();
}

void FormulaCompletion::refresh()
{
    if (m_inserting) return;
    if (!m_enabled || m_composing || !m_input->hasFocus() || !m_input->isVisible())
    {
        hide();
        return;
    }
    const Token token = tokenAtCursor();
    if (token.start < 0 ||
        (m_dismissedPosition == m_input->textCursor().position() && m_dismissedText == m_input->toPlainText()))
    {
        hide();
        return;
    }
    m_model->clear();
    const auto &entries = CalculationCatalog::entries();
    for (int index = 0; index < entries.size(); ++index)
    {
        const auto &entry = entries.at(index);
        if (!entry.isCompletion() || !entry.matches(token.query, true)) continue;
        auto *item = new QStandardItem(QStringLiteral("[%1] %2 — %3\n示例：%4")
            .arg(entry.category, entry.signature, entry.title, entry.example));
        item->setData(index, EntryRole);
        item->setToolTip(entry.description);
        m_model->appendRow(item);
    }
    if (!m_model->rowCount())
    {
        hide();
        emit hintChanged(QStringLiteral("没有匹配的函数或常量；可修改 @ 后的名称或中文关键词"));
        return;
    }
    m_completer->popup()->setAttribute(Qt::WA_WindowPropagation, true);
    m_completer->popup()->setPalette(CalculatorStyle::palette(m_input->palette()));
    m_completer->popup()->setStyleSheet(CalculatorStyle::sheet(m_input->palette(), m_input->font()));
    m_completer->popup()->setFont(m_input->font());
    m_completer->setCompletionPrefix(QString());
    m_completer->setCurrentRow(0);
    QRect position = m_input->cursorRect();
    position.setWidth(qMin(540, m_input->viewport()->width()));
    m_completer->complete(position);
    m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
}

void FormulaCompletion::insertCurrent(int entryIndex)
{
    const Token token = tokenAtCursor();
    const auto &entries = CalculationCatalog::entries();
    if (!m_enabled || m_composing || token.start < 0 || entryIndex < 0 || entryIndex >= entries.size()) return;
    const auto &entry = entries.at(entryIndex);
    if (!entry.isCompletion() || !entry.matches(token.query, true)) return;
    const QString text = m_input->toPlainText();
    // Replacing a name next to an existing call must preserve its arguments.
    int following = token.end;
    while (following < text.size() && text.at(following).isSpace()) ++following;
    const bool function = entry.kind == CalculationCatalog::Kind::Function;
    const bool existingCall = function && following < text.size() && text.at(following) == QLatin1Char('(');
    const QString insertion = existingCall ? entry.name : entry.insertion();
    const int offset = entry.name.size() + (function ? 1 : 0) + (existingCall ? following - token.end : 0);
    m_inserting = true;
    QTextCursor cursor = m_input->textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(token.start);
    cursor.setPosition(token.end, QTextCursor::KeepAnchor);
    cursor.insertText(insertion);
    cursor.setPosition(token.start + offset);
    cursor.endEditBlock();
    m_input->setTextCursor(cursor);
    hide();
    m_inserting = false;
    emit hintChanged(entry.signature + QStringLiteral(" · ") + entry.description);
}

bool FormulaCompletion::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_input)
    {
        if (event->type() == QEvent::InputMethod)
        {
            m_composing = !static_cast<QInputMethodEvent *>(event)->preeditString().isEmpty();
            hide();
            // A committed string is installed by QPlainTextEdit after this filter returns.
            if (!m_composing) QTimer::singleShot(0, this, [this]() { refresh(); });
        }
        if (event->type() == QEvent::Hide) hide();
        if (event->type() == QEvent::FocusOut &&
            static_cast<QFocusEvent *>(event)->reason() != Qt::PopupFocusReason) hide();
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
        return QObject::eventFilter(object, event);
    if (m_composing || !m_completer->popup()->isVisible()) return false;
    auto *key = static_cast<QKeyEvent *>(event);
    const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
    const Qt::KeyboardModifiers modifiers = key->modifiers() & ~Qt::KeypadModifier;
    // 候选可见时不让历史召回替换正在补全的输入；普通方向键仍选择候选。
    if (modifiers == Qt::AltModifier && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down))
    {
        key->accept();
        return true;
    }
    const bool submit = enter && modifiers == Qt::ControlModifier;
    const bool confirm = (enter || key->key() == Qt::Key_Tab) && modifiers == Qt::NoModifier;
    const bool cancel = key->key() == Qt::Key_Escape && modifiers == Qt::NoModifier;
    const bool navigate = (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) && modifiers == Qt::NoModifier;
    if (!submit && !confirm && !cancel && !navigate) return false;
    key->accept();
    if (event->type() == QEvent::ShortcutOverride) return true;
    if (submit) emit calculationRequested();
    else if (confirm)
    {
        const QModelIndex index = m_completer->popup()->currentIndex();
        if (index.isValid()) insertCurrent(index.data(EntryRole).toInt());
    }
    else if (cancel)
    {
        m_dismissedText = m_input->toPlainText();
        m_dismissedPosition = m_input->textCursor().position();
        hide();
        emit hintChanged(QStringLiteral("已关闭补全候选；可继续编辑，或删除未完成的 @ 查询"));
    }
    else
    {
        const int count = m_completer->completionModel()->rowCount();
        if (count)
        {
            int row = m_completer->popup()->currentIndex().row();
            row = (row + (key->key() == Qt::Key_Down ? 1 : -1) + count) % count;
            m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(row, 0));
        }
    }
    return true;
}
