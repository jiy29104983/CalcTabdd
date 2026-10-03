#include "formula_completion.h"
#include "calculation_catalog.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QInputMethodEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTextCursor>
#include <QTimer>

namespace {
constexpr int EntryRole = Qt::UserRole + 1;
bool isQueryCharacter(QChar ch) { return ch.isLetterOrNumber() || ch == QLatin1Char('_'); }
}

FormulaCompletion::FormulaCompletion(QPlainTextEdit *input)
    : QObject(input), m_input(input), m_completer(new QCompleter(this)), m_model(new QStandardItemModel(this))
{
    m_completer->setModel(m_model);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setMaxVisibleItems(6);
    m_completer->setWrapAround(true);
    m_completer->setWidget(input);
    m_completer->popup()->setObjectName(QStringLiteral("formulaCompletionPopup"));
    m_completer->popup()->setAccessibleName(QStringLiteral("函数和常量候选"));
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
    if (cursor.hasSelection()) return {};
    const QString text = m_input->toPlainText();
    int start = cursor.position();
    while (start > 0 && isQueryCharacter(text.at(start - 1))) --start;
    if (start == 0 || text.at(start - 1) != QLatin1Char('@')) return {};
    --start;
    int previous = start - 1;
    while (previous >= 0 && text.at(previous).isSpace()) --previous;
    if (previous >= 0 && !QStringLiteral("(,+-*/%^×÷−").contains(text.at(previous))) return {};
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

void FormulaCompletion::refresh()
{
    if (m_inserting) return;
    const Token token = tokenAtCursor();
    if (m_composing || !m_input->hasFocus() || !m_input->isVisible() || token.start < 0 ||
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
    m_completer->popup()->setPalette(m_input->window()->palette());
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
    if (m_composing || token.start < 0 || entryIndex < 0 || entryIndex >= entries.size()) return;
    const auto &entry = entries.at(entryIndex);
    if (!entry.isCompletion() || !entry.matches(token.query, true)) return;
    const QString text = m_input->toPlainText();
    // Replacing a name next to an existing call must preserve its arguments.
    int following = token.end;
    while (following < text.size() && text.at(following).isSpace()) ++following;
    const bool existingCall = entry.kind == CalculationCatalog::Kind::Function &&
        following < text.size() && text.at(following) == QLatin1Char('(');
    const QString insertion = existingCall ? entry.name : entry.insertion;
    const int offset = existingCall ? entry.name.size() + following - token.end + 1 : entry.cursorOffset;
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
