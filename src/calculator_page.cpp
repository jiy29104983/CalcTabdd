#include "calculator_page.h"
#include "expression_engine.h"
#include "calculator_help.h"
#include "formula_completion.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFrame>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QTimer>
#include <QVBoxLayout>

CalculatorPage::CalculatorPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("calctabddPage"));
    setAutoFillBackground(true);
    setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(26, 18, 26, 18);
    auto *title = new QLabel(QStringLiteral("计算器"), this);
    QFont titleFont = font();
    titleFont.setPointSizeF(qMax(12.0, titleFont.pointSizeF() + 2));
    titleFont.setBold(true);
    title->setFont(titleFont);
    header->addWidget(title);
    header->addStretch();
    m_count = new QLabel(QStringLiteral("0 条记录"), this);
    m_count->setObjectName(QStringLiteral("recordCount"));
    header->addWidget(m_count);
    auto *help = new QPushButton(QStringLiteral("帮助"), this);
    help->setObjectName(QStringLiteral("helpButton"));
    help->setToolTip(QStringLiteral("查看全部运算、函数、常量与精度限制"));
    connect(help, &QPushButton::clicked, this, [this]() { showCalculatorHelp(window()); });
    header->addWidget(help);
    layout->addLayout(header);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("calculationHistory"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *history = new QWidget(m_scroll);
    m_records = new QVBoxLayout(history);
    m_records->setContentsMargins(24, 0, 24, 18);
    m_records->setSpacing(0);
    m_empty = new QLabel(QStringLiteral("从下面输入第一条公式\n公式与结果会按顺序显示在这里"), history);
    m_empty->setObjectName(QStringLiteral("emptyHistory"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setMinimumHeight(140);
    m_records->addWidget(m_empty);
    m_records->addStretch();
    m_scroll->setWidget(history);
    connect(m_scroll->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int maximum) {
        if (m_followLatest) m_scroll->verticalScrollBar()->setValue(maximum);
    });
    connect(m_scroll->verticalScrollBar(), &QScrollBar::actionTriggered, this, [this]() { m_followLatest = false; });
    layout->addWidget(m_scroll, 1);

    auto *composer = new QFrame(this);
    composer->setObjectName(QStringLiteral("formulaComposer"));
    auto *composeLayout = new QVBoxLayout(composer);
    composeLayout->setContentsMargins(24, 14, 24, 16);
    auto *inputHeading = new QHBoxLayout;
    auto *inputLabel = new QLabel(QStringLiteral("输入公式"), composer);
    inputHeading->addWidget(inputLabel);
    inputHeading->addStretch();
    inputHeading->addWidget(new QLabel(QStringLiteral("Ctrl+Enter 计算"), composer));
    composeLayout->addLayout(inputHeading);
    auto *inputRow = new QHBoxLayout;
    m_input = new QPlainTextEdit(composer);
    m_input->setObjectName(QStringLiteral("formulaInput"));
    m_input->setAccessibleName(QStringLiteral("输入公式"));
    m_input->setPlaceholderText(QStringLiteral("例如 (128 + 256) / 3"));
    m_input->setTabChangesFocus(true);
    m_input->setFixedHeight(72);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(qMax(11.0, font().pointSizeF() + 1));
    m_input->setFont(mono);
    m_input->installEventFilter(this);
    inputLabel->setBuddy(m_input);
    m_editTarget = m_input;
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (!now || !isAncestorOf(now)) return;
        if (now == m_input || m_input->isAncestorOf(now)) m_editTarget = m_input;
        else if (auto *label = qobject_cast<QLabel *>(now))
        {
            if (label->textInteractionFlags() & Qt::TextSelectableByKeyboard) m_editTarget = label;
        }
    });
    inputRow->addWidget(m_input, 1);
    auto *calculate = new QPushButton(QStringLiteral("计算"), composer);
    calculate->setObjectName(QStringLiteral("calculateButton"));
    calculate->setMinimumSize(76, 36);
    inputRow->addWidget(calculate, 0, Qt::AlignBottom);
    connect(calculate, &QPushButton::clicked, this, &CalculatorPage::submit);
    composeLayout->addLayout(inputRow);
    auto *hint = new QLabel(QStringLiteral("输入 @ 查找函数和常量 · Ctrl+Enter 计算 · 使用 ans 引用上次结果"), composer);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("inputHint"));
    composeLayout->addWidget(hint);
    m_status = new QLabel(QStringLiteral("就绪 · 关闭标签后清空记录"), composer);
    m_status->setObjectName(QStringLiteral("calculationStatus"));
    m_status->setWordWrap(true);
    composeLayout->addWidget(m_status);
    layout->addWidget(composer);
    auto *completion = new FormulaCompletion(m_input);
    connect(completion, &FormulaCompletion::calculationRequested, this, &CalculatorPage::submit);
    connect(completion, &FormulaCompletion::hintChanged, m_status, &QLabel::setText);
    setFocusProxy(m_input);
    applyTheme();
}

void CalculatorPage::focusInput()
{
    m_input->setFocus(Qt::OtherFocusReason);
}

void CalculatorPage::submit()
{
    if (m_composing) return;
    const QString expression = m_input->toPlainText().trimmed();
    if (expression.isEmpty())
    {
        m_status->setText(QStringLiteral("请先输入公式"));
        focusInput();
        return;
    }
    if (expression.contains(QLatin1Char('@')))
    {
        m_status->setText(QStringLiteral("请先完成 @ 函数或常量补全，或删除 @ 查询后再计算"));
        focusInput();
        return;
    }
    const CalculationResult result = ExpressionEngine::evaluate(expression, m_answer);
    m_followLatest = true;
    m_empty->hide();
    auto *record = new QFrame;
    record->setObjectName(QStringLiteral("calculationRecord"));
    record->setProperty("error", !result.ok);
    auto *row = new QHBoxLayout(record);
    row->setContentsMargins(10, 13, 10, 13);
    row->setSpacing(14);
    auto *number = new QLabel(QStringLiteral("%1").arg(++m_recordCount, 2, 10, QLatin1Char('0')), record);
    number->setObjectName(QStringLiteral("recordNumber"));
    number->setMinimumWidth(24);
    row->addWidget(number, 0, Qt::AlignTop);
    auto *values = new QVBoxLayout;
    values->setSpacing(5);
    auto *formula = new QLabel(expression, record);
    formula->setObjectName(QStringLiteral("recordFormula"));
    formula->setTextFormat(Qt::PlainText);
    formula->setWordWrap(true);
    formula->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    formula->setFont(m_input->font());
    values->addWidget(formula);
    auto *value = new QLabel(result.ok ? QStringLiteral("= %1").arg(result.text) : QStringLiteral("无法计算：%1").arg(result.text), record);
    value->setObjectName(QStringLiteral("recordResult"));
    value->setTextFormat(Qt::PlainText);
    value->setWordWrap(true);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    value->setProperty("error", !result.ok);
    QFont resultFont = result.ok ? m_input->font() : font();
    if (result.ok) resultFont.setPointSizeF(m_input->font().pointSizeF() + 4);
    value->setFont(resultFont);
    values->addWidget(value);
    row->addLayout(values, 1);
    auto *reuse = new QPushButton(result.ok ? QStringLiteral("再次使用") : QStringLiteral("修改公式"), record);
    reuse->setObjectName(QStringLiteral("reuseFormula"));
    reuse->setFlat(true);
    connect(reuse, &QPushButton::clicked, this, [this, expression]() {
        m_input->setPlainText(expression);
        m_input->moveCursor(QTextCursor::End);
        focusInput();
        m_status->setText(QStringLiteral("公式已放入输入框，可修改后重新计算"));
    });
    row->addWidget(reuse, 0, Qt::AlignVCenter);
    m_records->insertWidget(m_records->count() - 1, record);
    m_count->setText(QStringLiteral("%1 条记录").arg(m_recordCount));
    if (result.ok)
    {
        m_answer = result.value;
        m_input->clear();
        m_status->setText(QStringLiteral("计算完成 · 关闭标签后清空记录"));
    }
    else
    {
        m_status->setText(result.text);
        QTextCursor cursor = m_input->textCursor();
        cursor.setPosition(qBound(0, result.errorPosition, m_input->toPlainText().size()));
        m_input->setTextCursor(cursor);
    }
    focusInput();
    QTimer::singleShot(0, this, [this]() { m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum()); });
}

void CalculatorPage::routeEdit(const QString &command)
{
    // Host menu proxies remain active while a modeless help window is focused.
    QWidget *focused = QApplication::focusWidget();
    if (focused && (focused->window()->objectName() == QStringLiteral("calctabddHelpDialog") ||
                    focused->window()->objectName() == QStringLiteral("calctabddAboutDialog")))
    {
        if (auto *line = qobject_cast<QLineEdit *>(focused))
        {
            if (command == QStringLiteral("actioncopy")) line->copy();
            else if (command == QStringLiteral("actioncut")) line->cut();
            else if (command == QStringLiteral("actionpaste")) line->paste();
            else if (command == QStringLiteral("actionundo")) line->undo();
            else if (command == QStringLiteral("actionredo")) line->redo();
            else if (command == QStringLiteral("actionselect_All")) line->selectAll();
        }
        else if (auto *text = qobject_cast<QTextEdit *>(focused))
        {
            if (command == QStringLiteral("actioncopy")) text->copy();
            else if (command == QStringLiteral("actionselect_All")) text->selectAll();
        }
        return;
    }
    auto *selectedLabel = qobject_cast<QLabel *>(m_editTarget.data());
    if (selectedLabel)
    {
        if (command == QStringLiteral("actioncopy") && selectedLabel->hasSelectedText())
            QApplication::clipboard()->setText(selectedLabel->selectedText());
        else if (command == QStringLiteral("actionselect_All"))
            selectedLabel->setSelection(0, selectedLabel->text().size());
        else if (command == QStringLiteral("actionpaste"))
        {
            focusInput();
            m_input->paste();
        }
        return;
    }
    if (command == QStringLiteral("actioncopy")) m_input->copy();
    else if (command == QStringLiteral("actioncut")) m_input->cut();
    else if (command == QStringLiteral("actionpaste")) m_input->paste();
    else if (command == QStringLiteral("actionundo")) m_input->undo();
    else if (command == QStringLiteral("actionredo")) m_input->redo();
    else if (command == QStringLiteral("actionselect_All")) m_input->selectAll();
}

bool CalculatorPage::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_input)
    {
        if (event->type() == QEvent::InputMethod)
            m_composing = !static_cast<QInputMethodEvent *>(event)->preeditString().isEmpty();
        if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
        {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            {
                if (m_composing) return QWidget::eventFilter(object, event);
                key->accept();
                if (event->type() == QEvent::KeyPress && key->modifiers() & Qt::ControlModifier) submit();
                return true;
            }
        }
    }
    return QWidget::eventFilter(object, event);
}

void CalculatorPage::keyPressEvent(QKeyEvent *event) { event->accept(); }
void CalculatorPage::keyReleaseEvent(QKeyEvent *event) { event->accept(); }

void CalculatorPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) applyTheme();
}

void CalculatorPage::applyTheme()
{
    if (m_applyingTheme) return;
    QScopedValueRollback<bool> applying(m_applyingTheme, true);
    const QPalette colors = palette();
    const bool dark = colors.color(QPalette::Window).lightness() < 128;
    const QString border = colors.color(QPalette::Mid).name();
    const QString base = colors.color(QPalette::Base).name();
    const QString text = colors.color(QPalette::Text).name();
    const QString error = dark ? QStringLiteral("#f2a49a") : QStringLiteral("#b3443b");
    setStyleSheet(QStringLiteral(
        "QWidget#calctabddPage { background: %1; color: %2; }"
        "QScrollArea#calculationHistory, QScrollArea#calculationHistory > QWidget > QWidget { background: %1; }"
        "QFrame#calculationRecord { border-bottom: 1px solid %3; }"
        "QLabel#recordResult[error=\"true\"] { color: %4; }"
        "QFrame#formulaComposer { border-top: 1px solid %3; }"
        "QPlainTextEdit#formulaInput { border: 1px solid %3; border-radius: 4px; padding: 6px; }"
        "QPushButton#calculateButton { background: %5; color: %6; border: none; border-radius: 4px; padding: 6px 16px; }"
        "QPushButton#calculateButton:focus { border: 2px solid %2; }")
        .arg(base, text, border, error, colors.color(QPalette::Highlight).name(), colors.color(QPalette::HighlightedText).name()));
}
