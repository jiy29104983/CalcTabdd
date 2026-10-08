#include "record_text.h"
#include "calculator_page.h"
#include "calculator_help.h"
#include "calculator_style.h"
#include "formula_completion.h"
#include "custom_formula.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QFrame>
#include <QInputMethodEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QDialog>
#include <QDialogButtonBox>
#include <QToolButton>
#include <QTextEdit>
#include <QTextBlock>
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
    if (parent) setFont(parent->font());
    setAutoFillBackground(true);
    setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *headerPanel = new QFrame(this);
    headerPanel->setObjectName(QStringLiteral("calculatorHeader"));
    auto *headerLayout = new QVBoxLayout(headerPanel);
    headerLayout->setContentsMargins(24, 18, 24, 16);
    headerLayout->setSpacing(12);
    auto *header = new QHBoxLayout;
    header->setSpacing(12);
    auto *title = new QLabel(QStringLiteral("计算器"), this);
    title->setObjectName(QStringLiteral("calculatorTitle"));
    QFont titleFont = font();
    titleFont.setPointSizeF(qMax(12.0, titleFont.pointSizeF() + 2));
    titleFont.setBold(true);
    title->setFont(titleFont);
    header->addWidget(title);
    header->addStretch();
    m_count = new QLabel(QStringLiteral("0 条记录"), this);
    m_count->setObjectName(QStringLiteral("recordCount"));
    header->addWidget(m_count);
    headerLayout->addLayout(header);
    auto *toolbar = new ButtonFlowLayout;
    headerLayout->addLayout(toolbar);
    m_exportButton = new QToolButton(this);
    m_exportButton->setObjectName(QStringLiteral("exportHistoryButton"));
    m_exportButton->setText(QStringLiteral("导出记录"));
    m_exportButton->setToolTip(QStringLiteral("将当前窗口的全部记录导出为 TXT 或 Markdown"));
    m_exportButton->setPopupMode(QToolButton::InstantPopup);
    m_exportButton->setProperty("calculatorMenu", true);
    m_exportButton->setEnabled(false);
    auto *exportMenu = new QMenu(m_exportButton);
    m_exportButton->setMenu(exportMenu);
    auto *exportText = exportMenu->addAction(QStringLiteral("导出为 TXT…"));
    exportText->setObjectName(QStringLiteral("exportText"));
    connect(exportText, &QAction::triggered, this, [this]() { requestExport(CalculationExport::Format::Text); });
    auto *exportMarkdown = exportMenu->addAction(QStringLiteral("导出为 Markdown…"));
    exportMarkdown->setObjectName(QStringLiteral("exportMarkdown"));
    connect(exportMarkdown, &QAction::triggered, this, [this]() { requestExport(CalculationExport::Format::Markdown); });
    toolbar->addWidget(m_exportButton);
    m_sessionButton = new QToolButton(this);
    m_sessionButton->setObjectName(QStringLiteral("sessionButton"));
    m_sessionButton->setText(QStringLiteral("本地会话"));
    m_sessionButton->setToolTip(QStringLiteral("可选保存与恢复；默认不保存，文件由当前窗口独占"));
    m_sessionButton->setPopupMode(QToolButton::InstantPopup);
    m_sessionButton->setProperty("calculatorMenu", true);
    auto *sessionMenu = new QMenu(m_sessionButton);
    m_sessionButton->setMenu(sessionMenu);
    auto *saveAs = sessionMenu->addAction(QStringLiteral("开启保存／另存新文件…"));
    saveAs->setObjectName(QStringLiteral("saveSessionAs"));
    connect(saveAs, &QAction::triggered, this, [this]() { requestSessionFile(false); });
    auto *restore = sessionMenu->addAction(QStringLiteral("恢复会话…"));
    restore->setObjectName(QStringLiteral("restoreSession"));
    connect(restore, &QAction::triggered, this, [this]() { requestSessionFile(true); });
    m_saveNowAction = sessionMenu->addAction(QStringLiteral("立即保存／重试"));
    m_saveNowAction->setObjectName(QStringLiteral("saveSessionNow"));
    m_saveNowAction->setEnabled(false);
    connect(m_saveNowAction, &QAction::triggered, this, [this]() {
        if (!m_composing && !m_definitionDialog && !m_sessionDialog && !m_exportDialog && !m_clearConfirmation) saveSession();
    });
    m_stopSavingAction = sessionMenu->addAction(QStringLiteral("停止保存（保留文件）"));
    m_stopSavingAction->setObjectName(QStringLiteral("stopSavingSession"));
    m_stopSavingAction->setEnabled(false);
    connect(m_stopSavingAction, &QAction::triggered, this, &CalculatorPage::stopSaving);
    toolbar->addWidget(m_sessionButton);
    m_clearButton = new QPushButton(QStringLiteral("清空会话"), this);
    m_clearButton->setObjectName(QStringLiteral("clearSessionButton"));
    m_clearButton->setToolTip(QStringLiteral("确认后清空记录并重置 ans，保留召回前的草稿"));
    m_clearButton->setEnabled(false);
    connect(m_clearButton, &QPushButton::clicked, this, &CalculatorPage::requestClearSession);
    toolbar->addWidget(m_clearButton);
    auto *help = new QPushButton(QStringLiteral("帮助"), this);
    help->setObjectName(QStringLiteral("helpButton"));
    help->setToolTip(QStringLiteral("查看全部运算、函数、常量与精度限制"));
    connect(help, &QPushButton::clicked, this, [this]() { showCalculatorHelp(window()); });
    toolbar->addWidget(help);
    layout->addWidget(headerPanel);
    m_sessionStatus = new QLabel(this);
    m_sessionStatus->setObjectName(QStringLiteral("sessionSaveStatus"));
    m_sessionStatus->setWordWrap(true);
    m_sessionStatus->setTextFormat(Qt::PlainText);
    m_sessionStatus->setContentsMargins(24, 0, 24, 8);
    m_sessionStatus->hide();
    layout->addWidget(m_sessionStatus);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("calculationHistory"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *history = new QWidget(m_scroll);
    history->setObjectName(QStringLiteral("historyCanvas"));
    m_records = new QVBoxLayout(history);
    m_records->setContentsMargins(24, 16, 24, 16);
    m_records->setSpacing(10);
    m_empty = new QLabel(QStringLiteral("开始一次计算\n在下方输入公式，按 Ctrl+Enter 查看结果"), history);
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
    composeLayout->setContentsMargins(24, 16, 24, 18);
    composeLayout->setSpacing(10);
    auto *inputHeading = new QHBoxLayout;
    m_inputLabel = new QLabel(QStringLiteral("输入公式"), composer);
    QFont labelFont = font();
    labelFont.setBold(true);
    m_inputLabel->setFont(labelFont);
    inputHeading->addWidget(m_inputLabel);
    m_modeButton = new QToolButton(composer);
    m_modeButton->setObjectName(QStringLiteral("calculationModeButton"));
    m_modeButton->setPopupMode(QToolButton::InstantPopup);
    m_modeButton->setProperty("calculatorMenu", true);
    m_modeButton->setToolTip(QStringLiteral("切换计算模式，或定义／修改自定义公式"));
    auto *modeMenu = new QMenu(m_modeButton);
    m_modeButton->setMenu(modeMenu);
    auto *define = modeMenu->addAction(QStringLiteral("定义／修改公式…"));
    define->setObjectName(QStringLiteral("defineCustomFormula"));
    connect(define, &QAction::triggered, this, &CalculatorPage::requestDefinition);
    modeMenu->addSeparator();
    m_normalModeAction = modeMenu->addAction(QStringLiteral("普通计算模式"));
    m_normalModeAction->setObjectName(QStringLiteral("normalCalculationMode"));
    m_normalModeAction->setCheckable(true);
    connect(m_normalModeAction, &QAction::triggered, this, [this]() { switchMode(false); });
    m_customModeAction = modeMenu->addAction(QStringLiteral("自定义公式模式"));
    m_customModeAction->setObjectName(QStringLiteral("customCalculationMode"));
    m_customModeAction->setCheckable(true);
    connect(m_customModeAction, &QAction::triggered, this, [this]() { switchMode(true); });
    inputHeading->addWidget(m_modeButton);
    inputHeading->addStretch();
    composeLayout->addLayout(inputHeading);
    m_definitionLabel = new QLabel(composer);
    m_definitionLabel->setObjectName(QStringLiteral("currentCustomDefinition"));
    m_definitionLabel->setTextFormat(Qt::PlainText);
    m_definitionLabel->setWordWrap(true);
    m_definitionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_definitionLabel->hide();
    composeLayout->addWidget(m_definitionLabel);
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
    m_inputLabel->setBuddy(m_input);
    m_editTarget = m_input;
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (!now || !isAncestorOf(now)) return;
        if (now == m_input || m_input->isAncestorOf(now)) m_editTarget = m_input;
        else if (auto *result = qobject_cast<RecordText *>(now)) m_editTarget = result;
        else if (auto *label = qobject_cast<QLabel *>(now))
        {
            if (label->textInteractionFlags() & Qt::TextSelectableByKeyboard) m_editTarget = label;
        }
    });
    composeLayout->addWidget(m_input);
    auto *footer = new QHBoxLayout;
    footer->setSpacing(16);
    auto *calculate = new QPushButton(QStringLiteral("计算"), composer);
    calculate->setObjectName(QStringLiteral("calculateButton"));
    calculate->setProperty("role", QStringLiteral("primary"));
    calculate->setMinimumWidth(96);
    calculate->setToolTip(QStringLiteral("计算并新增记录（Ctrl+Enter）"));
    connect(calculate, &QPushButton::clicked, this, &CalculatorPage::submit);
    auto *hint = new QLabel(QStringLiteral("@ 补全函数 · Alt+↑↓ 召回历史／返回草稿 · ans 上次结果"), composer);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("inputHint"));
    composeLayout->addWidget(hint);
    m_status = new QLabel(QStringLiteral("Ctrl+Enter 确认计算"), composer);
    m_status->setObjectName(QStringLiteral("calculationStatus"));
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    footer->addWidget(m_status, 1);
    footer->addWidget(calculate, 0, Qt::AlignBottom);
    composeLayout->addLayout(footer);
    layout->addWidget(composer);
    connect(m_input, &QPlainTextEdit::textChanged, this, &CalculatorPage::clearInputError);
    m_completion = new FormulaCompletion(m_input);
    connect(m_completion, &FormulaCompletion::calculationRequested, this, &CalculatorPage::submit);
    connect(m_completion, &FormulaCompletion::hintChanged, m_status, &QLabel::setText);
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(200);
    connect(m_saveTimer, &QTimer::timeout, this, [this]() { saveSession(); });
    connect(m_input, &QPlainTextEdit::textChanged, this, &CalculatorPage::scheduleSave);
    connect(m_input, &QPlainTextEdit::cursorPositionChanged, this, &CalculatorPage::scheduleSave);
    connect(m_input, &QPlainTextEdit::selectionChanged, this, &CalculatorPage::scheduleSave);
    setFocusProxy(m_input);
    updateModeUi();
    applyTheme();
}

void CalculatorPage::focusInput()
{
    if (m_definitionDialog || m_clearConfirmation || m_exportDialog || m_sessionDialog) return;
    m_input->setFocus(Qt::OtherFocusReason);
}

void CalculatorPage::submit()
{
    if (m_composing || m_definitionDialog || m_clearConfirmation || m_exportDialog || m_sessionDialog) return;
    const QString inputText = m_input->toPlainText();
    const QString expression = inputText.trimmed();
    if (!customMode() && expression.isEmpty())
    {
        m_status->setText(QStringLiteral("请先输入公式"));
        focusInput();
        return;
    }
    if (!customMode() && expression.contains(QLatin1Char('@')))
    {
        m_status->setText(QStringLiteral("请先完成 @ 函数或常量补全，或删除 @ 查询后再计算"));
        focusInput();
        return;
    }
    QString validationError;
    const CalculationRecord entry = customMode()
        ? m_history.calculateCustom(m_definition, inputText, validationError) : m_history.calculate(expression);
    if (!validationError.isEmpty())
    {
        m_status->setText(validationError);
        focusInput();
        return;
    }
    appendRecord(entry);
    const CalculationResult &result = entry.result;
    if (result.ok)
    {
        if (m_historyPosition >= 0)
        {
            restoreDraft();
            m_status->setText(QStringLiteral("计算完成 · 已返回召回前的草稿"));
        }
        else
        {
            if (!customMode()) m_input->clear();
            m_status->setText(customMode() ? QStringLiteral("计算完成 · 参数已保留") : QStringLiteral("计算完成"));
        }
    }
    else if (customMode())
    {
        // 数学错误位置属于历史代入式，不能当成参数输入框的字符偏移。
        clearInputError();
        m_status->setText(QStringLiteral("无法计算：%1 · 请检查参数或修改定义").arg(result.text));
    }
    else
    {
        int offset = 0;
        while (offset < inputText.size() && inputText.at(offset).isSpace()) ++offset;
        showInputError(result, offset);
    }
    if (m_sessionFile) saveSession();
    focusInput();
    QTimer::singleShot(0, this, [this]() { m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum()); });
}

void CalculatorPage::showInputError(const CalculationResult &result, int offset, bool moveCursor)
{
    const QString text = m_input->toPlainText();
    m_errorPosition = qBound(0, offset + result.errorPosition, text.size());
    m_errorLength = qBound(0, result.errorLength, text.size() - m_errorPosition);
    QTextCursor cursor(m_input->document());
    cursor.setPosition(m_errorPosition);
    // 行列给用户阅读；内部范围仍用 Qt 的 UTF-16 偏移，代理对按一个字符计列。
    const int column = cursor.block().text().left(cursor.positionInBlock()).toUcs4().size() + 1;
    const QString location = QStringLiteral("第 %1 行，第 %2 列").arg(cursor.blockNumber() + 1).arg(column);
    const QString message = result.text + QStringLiteral(" · ") + location;
    m_status->setText(message);
    m_input->setToolTip(message);
    m_input->setAccessibleDescription(message);
    applyErrorHighlight();
    if (moveCursor)
    {
        m_input->setTextCursor(cursor);
        m_input->ensureCursorVisible();
    }
}

void CalculatorPage::clearInputError()
{
    if (m_errorPosition < 0) return;
    m_errorPosition = -1;
    m_errorLength = 0;
    m_input->setExtraSelections({});
    m_input->setToolTip(QString());
    m_input->setAccessibleDescription(QString());
    m_status->setText(QStringLiteral("继续编辑 · Ctrl+Enter 重新计算"));
}

void CalculatorPage::applyErrorHighlight()
{
    if (m_errorPosition < 0) return;
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    QTextEdit::ExtraSelection selection;
    selection.cursor = QTextCursor(m_input->document());
    selection.cursor.setPosition(m_errorPosition);
    selection.cursor.setPosition(m_errorPosition + m_errorLength, QTextCursor::KeepAnchor);
    selection.format.setBackground(QColor(dark ? "#583a39" : "#fbe4e1"));
    selection.format.setForeground(QColor(dark ? "#ffe2dd" : "#8f2922"));
    if (m_errorLength > 0)
    {
        selection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        selection.format.setUnderlineColor(QColor(dark ? "#f2a49a" : "#b3443b"));
    }
    else selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    // 额外格式不改变实际选区，继续输入不会替换被标记的参数。
    m_input->setExtraSelections({selection});
}

void CalculatorPage::requestExport(CalculationExport::Format format)
{
    if (m_composing || m_history.count() == 0 || m_definitionDialog || m_clearConfirmation || m_exportDialog || m_sessionDialog) return;
    const bool markdown = format == CalculationExport::Format::Markdown;
    const QByteArray contents = CalculationExport::serialize(m_history.records(), format);
    auto *dialog = new QFileDialog(this);
    m_exportDialog = dialog;
    // 使用可随页面关闭的异步 Qt 对话框。覆盖确认也使用 Qt 控件，保持相同生命周期。
    dialog->setOption(QFileDialog::DontUseNativeDialog);
    dialog->setAttribute(Qt::WA_WindowPropagation, true);
    dialog->setOption(QFileDialog::DontConfirmOverwrite);
    dialog->setObjectName(QStringLiteral("exportHistoryDialog"));
    dialog->setWindowTitle(markdown ? QStringLiteral("导出 Markdown 计算记录") : QStringLiteral("导出 TXT 计算记录"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->setFileMode(QFileDialog::AnyFile);
    dialog->setNameFilter(markdown ? QStringLiteral("Markdown (*.md)") : QStringLiteral("文本文件 (*.txt)"));
    dialog->setDefaultSuffix(markdown ? QStringLiteral("md") : QStringLiteral("txt"));
    dialog->setLabelText(QFileDialog::LookIn, QStringLiteral("位置："));
    dialog->setLabelText(QFileDialog::FileName, QStringLiteral("文件名："));
    dialog->setLabelText(QFileDialog::FileType, QStringLiteral("文件类型："));
    dialog->setLabelText(QFileDialog::Accept, QStringLiteral("导出"));
    dialog->setLabelText(QFileDialog::Reject, QStringLiteral("取消"));
    const QString previous = markdown ? m_markdownExportPath : m_textExportPath;
    if (previous.isEmpty())
    {
        dialog->setDirectory(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        dialog->selectFile(markdown ? QStringLiteral("CalcTabdd.md") : QStringLiteral("CalcTabdd.txt"));
    }
    else
    {
        dialog->setDirectory(QFileInfo(previous).absolutePath());
        dialog->selectFile(QFileInfo(previous).fileName());
    }
    connect(dialog, &QDialog::finished, this, [this, dialog, markdown, contents](int result) {
        m_exportDialog.clear();
        if (result == QDialog::Accepted && !dialog->selectedFiles().isEmpty())
        {
            const QString path = dialog->selectedFiles().first();
            (markdown ? m_markdownExportPath : m_textExportPath) = path;
            confirmExport(path, contents);
        }
        if (isVisible() && !m_exportDialog)
        {
            window()->activateWindow();
            focusInput();
        }
    });
    dialog->open();
    // 文件对话框显示时会初始化调色板，之后同步页面配色。
    CalculatorStyle::applyDialog(dialog, palette());
}

void CalculatorPage::confirmExport(const QString &path, const QByteArray &contents)
{
    const QFileInfo target(path);
    if (!target.exists() && !target.isSymLink())
    {
        writeExport(path, contents);
        return;
    }
    auto *dialog = new QDialog(this);
    m_exportDialog = dialog;
    dialog->setAttribute(Qt::WA_WindowPropagation, true);
    dialog->setObjectName(QStringLiteral("exportOverwriteConfirmation"));
    dialog->setWindowTitle(QStringLiteral("确认覆盖导出文件"));
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);
    auto *question = new QLabel(QStringLiteral("文件已存在，是否用当前计算记录覆盖？\n%1").arg(path), dialog);
    question->setTextFormat(Qt::PlainText);
    question->setWordWrap(true);
    layout->addWidget(question);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("覆盖"));
    buttons->button(QDialogButtonBox::Ok)->setProperty("role", QStringLiteral("danger"));
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setText(QStringLiteral("取消"));
    cancel->setDefault(true);
    cancel->setFocus();
    layout->addWidget(buttons);
    dialog->resize(480, dialog->sizeHint().height());
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::finished, this, [this, path, contents](int result) {
        m_exportDialog.clear();
        if (result == QDialog::Accepted) writeExport(path, contents);
        if (isVisible())
        {
            window()->activateWindow();
            focusInput();
        }
    });
    dialog->open();
    CalculatorStyle::applyDialog(dialog, palette());
}

void CalculatorPage::writeExport(const QString &path, const QByteArray &contents)
{
    const QString error = CalculationExport::writeFile(path, contents);
    m_status->setText(error.isEmpty()
        ? QStringLiteral("已导出 %1 条记录：%2").arg(m_history.count()).arg(path)
        : QStringLiteral("导出失败：%1\n%2\n记录仍保留，可再次点击“导出记录”重试或选择其他位置。")
              .arg(path, error));
}

void CalculatorPage::requestClearSession()
{
    if (m_composing || m_history.count() == 0 || m_definitionDialog || m_clearConfirmation || m_exportDialog || m_sessionDialog) return;
    // 使用与帮助窗口一致的 Qt 控件对话框，避免 Qt 5.15.2 QMessageBox
    // 在 Windows offscreen 平台的原生系统菜单访问。
    auto *dialog = new QDialog(this);
    m_clearConfirmation = dialog;
    dialog->setObjectName(QStringLiteral("clearSessionConfirmation"));
    dialog->setWindowTitle(QStringLiteral("清空会话"));
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setAttribute(Qt::WA_WindowPropagation, true);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);
    auto *question = new QLabel(
        QStringLiteral("清空当前窗口的 %1 条计算记录，并将 ans 重置为 0？").arg(m_history.count()), dialog);
    question->setTextFormat(Qt::PlainText);
    question->setWordWrap(true);
    layout->addWidget(question);
    auto *details = new QLabel(m_historyPosition >= 0
        ? QStringLiteral("将返回召回前的草稿，并丢弃本轮历史公式的临时编辑。清空不可撤销；其他窗口不受影响。")
        : QStringLiteral("输入区的草稿、光标和选区会保留。清空不可撤销；其他窗口不受影响。"), dialog);
    if (m_sessionFile) details->setText(details->text() + QStringLiteral("\n本地保存已开启：清空也会更新会话文件。"));
    details->setObjectName(QStringLiteral("clearSessionDetails"));
    details->setTextFormat(Qt::PlainText);
    details->setWordWrap(true);
    layout->addWidget(details);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("清空会话"));
    buttons->button(QDialogButtonBox::Ok)->setProperty("role", QStringLiteral("danger"));
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setText(QStringLiteral("取消"));
    cancel->setDefault(true);
    cancel->setFocus();
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog->setMinimumWidth(380);
    applyTheme();
    connect(dialog, &QDialog::finished, this, [this](int result) {
        m_clearConfirmation.clear();
        if (result == QDialog::Accepted) clearSession();
        if (isVisible())
        {
            // 从补全弹窗进入确认时，先恢复所属窗口，避免焦点留在已隐藏的候选窗。
            window()->activateWindow();
            focusInput();
        }
    });
    // 异步窗口模态确认，不阻塞其他宿主窗口；关闭页面会一起销毁对话框。
    dialog->open();
}

void CalculatorPage::clearSession()
{
    clearInputError();
    if (m_historyPosition >= 0) restoreDraft();
    m_historyPosition = -1;
    m_draft = {};
    m_recalledInputs.clear();
    m_editTarget = m_input;
    // 先销毁旧记录及其菜单回调，再允许新会话复用编号。
    while (m_records->count() > 2)
    {
        QLayoutItem *item = m_records->takeAt(1); // 保留空状态提示和底部 stretch。
        delete item->widget();
        delete item;
    }
    m_history.clear();
    m_count->setText(QStringLiteral("0 条记录"));
    m_clearButton->setEnabled(false);
    m_exportButton->setEnabled(false);
    m_empty->show();
    m_followLatest = true;
    m_scroll->verticalScrollBar()->setValue(0);
    m_status->setText(QStringLiteral("会话已清空 · ans = 0 · 草稿已保留"));
    if (m_sessionFile && !m_restoringSession) saveSession();
}

void CalculatorPage::appendRecord(const CalculationRecord &entry)
{
    const CalculationResult &result = entry.result;
    m_followLatest = true;
    m_empty->hide();
    auto *record = new QFrame;
    record->setObjectName(QStringLiteral("calculationRecord"));
    record->setProperty("error", !result.ok);
    auto *row = new QHBoxLayout(record);
    row->setContentsMargins(16, 14, 16, 14);
    row->setSpacing(14);
    auto *number = new QLabel(QStringLiteral("%1").arg(entry.id, 2, 10, QLatin1Char('0')), record);
    number->setObjectName(QStringLiteral("recordNumber"));
    number->setMinimumWidth(24);
    row->addWidget(number, 0, Qt::AlignTop);
    auto *values = new QVBoxLayout;
    values->setSpacing(5);
    auto *formula = new QLabel(entry.expression, record);
    formula->setObjectName(QStringLiteral("recordFormula"));
    formula->setTextFormat(Qt::PlainText);
    formula->setWordWrap(true);
    if (!entry.customDefinition.isEmpty()) formula->setToolTip(entry.customDefinition + QLatin1Char('\n') + entry.parameterInput);
    formula->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    formula->setFont(m_input->font());
    values->addWidget(formula);
    auto *value = new RecordText(result.ok ? QStringLiteral("= %1").arg(result.text) : QStringLiteral("无法计算：%1").arg(result.text), record);
    value->setObjectName(QStringLiteral("recordResult"));
    value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    value->setProperty("error", !result.ok);
    QFont resultFont = result.ok ? m_input->font() : font();
    if (result.ok) resultFont.setPointSizeF(m_input->font().pointSizeF() + 4);
    value->setFont(resultFont);
    value->setMinimumWidth(0);
    values->addWidget(value);
    if (result.ok && result.value.sources())
    {
        auto *sources = new QLabel(result.value.sourceText(), record);
        sources->setObjectName(QStringLiteral("recordSources"));
        sources->setTextFormat(Qt::PlainText);
        sources->setWordWrap(true);
        sources->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        sources->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        values->addWidget(sources);
    }
    row->addLayout(values, 1);
    auto *reuse = new QPushButton(result.ok ? QStringLiteral("再次使用") : QStringLiteral("修改公式"), record);
    reuse->setObjectName(QStringLiteral("reuseFormula"));
    reuse->setProperty("role", QStringLiteral("quiet"));
    reuse->setToolTip(QStringLiteral("回填这条记录，编辑后确认计算；Alt+↓ 向后浏览至草稿"));
    connect(reuse, &QPushButton::clicked, this, [this, id = entry.id]() { reuseFormula(id); });
    auto *actions = new QVBoxLayout;
    actions->setSpacing(8);
    actions->setAlignment(Qt::AlignTop);
    actions->addWidget(reuse);
    auto *more = new QToolButton(record);
    more->setObjectName(QStringLiteral("recordActions"));
    more->setText(QStringLiteral("记录操作"));
    more->setProperty("role", QStringLiteral("quiet"));
    more->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    more->setAccessibleName(QStringLiteral("第 %1 条计算记录操作").arg(entry.id));
    more->setPopupMode(QToolButton::InstantPopup);
    more->setProperty("calculatorMenu", true);
    auto *menu = new QMenu(more);
    more->setMenu(menu);
    if (result.ok)
    {
        auto *copyValue = menu->addAction(QStringLiteral("复制纯数值"));
        copyValue->setObjectName(QStringLiteral("copyValue"));
        copyValue->setToolTip(QStringLiteral("复制完整数值文本；重新输入作为新十进制数字，不保留原类型和计算来源"));
        connect(copyValue, &QAction::triggered, this, [this, id = entry.id]() { copyRecord(id, true); });
    }
    auto *copyCalculation = menu->addAction(QStringLiteral("复制整条计算"));
    copyCalculation->setObjectName(QStringLiteral("copyCalculation"));
    connect(copyCalculation, &QAction::triggered, this, [this, id = entry.id]() { copyRecord(id, false); });
    if (result.ok)
    {
        auto *insert = menu->addAction(QStringLiteral("插入结果数值"));
        insert->setObjectName(QStringLiteral("insertResult"));
        insert->setToolTip(QStringLiteral("插入当时的数值文本；再次计算作为新十进制数字，不保留原类型和计算来源"));
        connect(insert, &QAction::triggered, this, [this, id = entry.id]() { insertResult(id); });
    }
    actions->addWidget(more);
    row->addLayout(actions);
    m_records->insertWidget(m_records->count() - 1, record);
    CalculatorStyle::prepareButton(reuse);
    CalculatorStyle::prepareButton(more);
    m_count->setText(QStringLiteral("%1 条记录").arg(m_history.count()));
    m_clearButton->setEnabled(!m_composing);
    m_exportButton->setEnabled(!m_composing);
}

CalculatorPage::InputState CalculatorPage::captureInput() const
{
    const QTextCursor cursor = m_input->textCursor();
    return {m_input->toPlainText(), cursor.position(), cursor.anchor(), m_definition};
}

void CalculatorPage::restoreInput(const InputState &state)
{
    m_definition = state.customDefinition;
    updateModeUi();
    m_input->setPlainText(state.text);
    QTextCursor cursor = m_input->textCursor();
    cursor.setPosition(qBound(0, state.anchor, state.text.size()));
    cursor.setPosition(qBound(0, state.position, state.text.size()), QTextCursor::KeepAnchor);
    m_input->setTextCursor(cursor);
    m_editTarget = m_input;
    focusInput();
}

void CalculatorPage::showHistory(int position)
{
    const auto &records = m_history.records();
    if (position < 0 || position >= records.size()) return;
    if (m_historyPosition < 0)
    {
        storeModeDraft();
        m_draft = captureInput();
    }
    else m_recalledInputs.insert(records.at(m_historyPosition).id, captureInput());
    m_historyPosition = position;
    const auto &entry = records.at(position);
    const QString text = entry.customDefinition.isEmpty() ? entry.expression : entry.parameterInput;
    const InputState initial{text, text.size(), text.size(), entry.customDefinition};
    restoreInput(m_recalledInputs.value(entry.id, initial));
    m_status->setText(QStringLiteral("已召回第 %1 条公式 · Alt+↑↓ 浏览，向后越过最新记录返回草稿").arg(entry.id));
    // 仅对未修改的错误公式使用历史诊断，已有临时编辑须重新提交才能定位。
    if (!customMode() && !entry.result.ok && m_input->toPlainText() == entry.expression)
        showInputError(entry.result, 0, !m_recalledInputs.contains(entry.id));
    scheduleSave();
}

void CalculatorPage::restoreDraft()
{
    m_historyPosition = -1;
    m_recalledInputs.clear();
    restoreInput(m_draft);
    m_draft = {};
    scheduleSave();
}

void CalculatorPage::recallHistory(bool older)
{
    if (m_definitionDialog || m_clearConfirmation || m_exportDialog || m_sessionDialog) return;
    if (m_history.count() == 0 || (!older && m_historyPosition < 0)) return;
    const int position = m_historyPosition < 0 ? m_history.count() - 1
                                               : m_historyPosition + (older ? -1 : 1);
    if (position < 0) return; // 最旧记录不循环跳到最新，也不覆盖当前编辑。
    if (position == m_history.count())
    {
        restoreDraft();
        m_status->setText(QStringLiteral("已返回召回前的草稿"));
    }
    else showHistory(position);
}

void CalculatorPage::reuseFormula(quint64 id)
{
    if (m_composing || m_definitionDialog || m_clearConfirmation || m_exportDialog || m_sessionDialog) return;
    const auto &records = m_history.records();
    for (int index = 0; index < records.size(); ++index)
    {
        if (records.at(index).id == id)
        {
            showHistory(index);
            return;
        }
    }
}

void CalculatorPage::copyRecord(quint64 id, bool valueOnly)
{
    if (m_definitionDialog || m_exportDialog || m_sessionDialog) return;
    const CalculationRecord *entry = m_history.record(id);
    if (!entry || (valueOnly && !entry->result.ok)) return;
    QApplication::clipboard()->setText(valueOnly ? entry->valueText() : entry->calculationText());
    m_status->setText(!valueOnly ? QStringLiteral("已复制公式、结果与来源说明")
        : entry->result.value.sources() ? QStringLiteral("已复制数值文本；重新输入按新数字处理，不保留原计算来源")
                                       : QStringLiteral("已复制完整十进制数值"));
}

void CalculatorPage::insertResult(quint64 id)
{
    if (m_definitionDialog || m_clearConfirmation || m_exportDialog || m_sessionDialog) return;
    const CalculationRecord *entry = m_history.record(id);
    if (!entry || !entry->result.ok) return;
    if (m_composing)
    {
        m_status->setText(QStringLiteral("请先完成输入法组词，再插入结果数值"));
        return;
    }
    QTextCursor cursor = m_input->textCursor();
    cursor.beginEditBlock();
    cursor.insertText(customMode() ? entry->valueText() : entry->insertionText());
    cursor.endEditBlock();
    m_input->setTextCursor(cursor);
    m_editTarget = m_input;
    focusInput();
    m_status->setText(entry->result.value.sources()
        ? QStringLiteral("已插入数值文本；再次计算按新数字处理，不保留原计算来源")
        : QStringLiteral("已插入完整十进制数值，可继续编辑后计算"));
}

void CalculatorPage::routeEdit(const QString &command)
{
    if (m_clearConfirmation || m_exportDialog || m_sessionDialog) return;
    // Host menu proxies remain active while a modeless help window is focused.
    QWidget *focused = QApplication::focusWidget();
    if (focused && (focused->window()->objectName() == QStringLiteral("calctabddHelpDialog") ||
                    focused->window()->objectName() == QStringLiteral("calctabddAboutDialog") ||
                    focused->window() == m_definitionDialog))
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
    if (m_definitionDialog) return;
    if (auto *result = qobject_cast<RecordText *>(m_editTarget.data()))
    {
        if (command == QStringLiteral("actioncopy")) result->copy();
        else if (command == QStringLiteral("actionselect_All")) result->selectAll();
        else if (command == QStringLiteral("actionpaste")) { focusInput(); m_input->paste(); }
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
        {
            m_composing = !static_cast<QInputMethodEvent *>(event)->preeditString().isEmpty();
            if (m_composing) clearInputError();
            m_sessionButton->setEnabled(!m_composing);
            m_modeButton->setEnabled(!m_composing);
            findChild<QPushButton *>(QStringLiteral("calculateButton"))->setEnabled(!m_composing);
            for (auto *reuse : findChildren<QPushButton *>(QStringLiteral("reuseFormula")))
                reuse->setEnabled(!m_composing);
            m_clearButton->setEnabled(m_history.count() > 0 && !m_composing);
            m_exportButton->setEnabled(m_history.count() > 0 && !m_composing);
        }
        if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
        {
            auto *key = static_cast<QKeyEvent *>(event);
            const Qt::KeyboardModifiers modifiers = key->modifiers() & ~Qt::KeypadModifier;
            const bool historyKey = modifiers == Qt::AltModifier &&
                (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down);
            if (historyKey)
            {
                // 在输入区阻止同键宿主快捷键；预编辑中的按键仍交给输入控件。
                if (event->type() == QEvent::KeyPress && m_composing)
                    return QWidget::eventFilter(object, event);
                key->accept();
                if (event->type() == QEvent::KeyPress && !m_completion->hasVisiblePopup())
                    recallHistory(key->key() == Qt::Key_Up);
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            {
                if (m_composing) return QWidget::eventFilter(object, event);
                // 自定义参数按行编辑；仍拦截宿主同键快捷键，并只在 Ctrl+Enter 提交。
                if (customMode() && modifiers == Qt::NoModifier && event->type() == QEvent::KeyPress)
                    return QWidget::eventFilter(object, event);
                key->accept();
                if (event->type() == QEvent::KeyPress && modifiers == Qt::ControlModifier) submit();
                return true;
            }
        }
    }
    return QWidget::eventFilter(object, event);
}

void CalculatorPage::keyPressEvent(QKeyEvent *event) { event->accept(); }
void CalculatorPage::keyReleaseEvent(QKeyEvent *event) { event->accept(); }

void CalculatorPage::hideEvent(QHideEvent *event)
{
    if (m_clearConfirmation) m_clearConfirmation->reject();
    if (m_exportDialog) m_exportDialog->reject();
    if (m_sessionDialog) m_sessionDialog->reject();
    if (m_definitionDialog) m_definitionDialog->reject();
    if (m_sessionDirty) saveSession();
    QWidget::hideEvent(event);
}

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
    setStyleSheet(CalculatorStyle::sheet(colors, font()));
    for (auto *button : findChildren<QAbstractButton *>())
        if (button->window() == window()) CalculatorStyle::prepareButton(button);
    applyErrorHighlight();
    CalculatorStyle::applyDialog(m_exportDialog, colors);
    CalculatorStyle::applyDialog(m_sessionDialog, colors);
    CalculatorStyle::applyDialog(m_definitionDialog, colors);
    CalculatorStyle::applyDialog(m_clearConfirmation, colors);
}

CalculatorPage::~CalculatorPage()
{
    // QWidget 的子控件此时仍在；即使没有先收到 hideEvent 也提交最后的已上屏草稿。
    if (m_sessionDirty) saveSession();
}

CalculationSession CalculatorPage::captureSession() const
{
    CalculationSession session;
    session.id = m_sessionId;
    session.history = m_history;
    session.input = captureInput();
    session.normalInput = m_normalInput;
    session.customInput = m_customInput;
    if (m_historyPosition < 0)
        (customMode() ? session.customInput : session.normalInput) = session.input;
    session.historyPosition = m_historyPosition;
    if (m_historyPosition >= 0)
    {
        session.draft = m_draft;
        session.recalledInputs = m_recalledInputs;
        session.recalledInputs.insert(m_history.records().at(m_historyPosition).id, session.input);
    }
    return session;
}

void CalculatorPage::restoreSession(const CalculationSession &session)
{
    QScopedValueRollback<bool> restoring(m_restoringSession, true);
    clearSession();
    m_normalInput = session.normalInput;
    m_customInput = session.customInput;
    m_history = session.history;
    m_sessionId = session.id;
    for (const auto &entry : m_history.records()) appendRecord(entry);
    m_historyPosition = session.historyPosition;
    m_draft = session.draft;
    m_recalledInputs = session.recalledInputs;
    restoreInput(session.input);
    const CalculationRecord *entry = m_historyPosition >= 0
        ? &m_history.records().at(m_historyPosition)
        : (m_history.count() > 0 ? &m_history.records().last() : nullptr);
    if (!customMode() && entry && !entry->result.ok && session.input.text.trimmed() == entry->expression)
    {
        int offset = 0;
        while (offset < session.input.text.size() && session.input.text.at(offset).isSpace()) ++offset;
        showInputError(entry->result, offset, false);
    }
    else m_status->setText(QStringLiteral("已恢复历史结果与草稿，未重新计算旧公式"));
}

void CalculatorPage::scheduleSave()
{
    if (!m_sessionFile || m_restoringSession) return;
    m_sessionDirty = true;
    m_saveTimer->start();
}

void CalculatorPage::updateSessionStatus(const QString &error)
{
    m_saveNowAction->setEnabled(bool(m_sessionFile));
    m_stopSavingAction->setEnabled(bool(m_sessionFile));
    if (!error.isEmpty())
    {
        m_sessionStatus->setText(QStringLiteral("%1\n当前记录与草稿仍保留。请通过“本地会话”重试或另存新文件，成功保存前不要关闭标签。")
            .arg(error));
    }
    else if (m_sessionFile)
        m_sessionStatus->setText(QStringLiteral("本地保存已开启 · %1\n关闭后可在空白计算器中选择“恢复会话”。").arg(m_sessionFile->path()));
    else m_sessionStatus->setText(QStringLiteral("本地保存已停止，已保存文件保留；后续更改仅在当前标签有效。"));
    m_sessionStatus->show();
}

bool CalculatorPage::saveSession()
{
    m_saveTimer->stop();
    if (!m_sessionFile) return true;
    const QString error = m_sessionFile->save(captureSession());
    m_sessionDirty = !error.isEmpty();
    updateSessionStatus(error.isEmpty() ? QString() : QStringLiteral("保存失败：%1").arg(error));
    return error.isEmpty();
}

void CalculatorPage::stopSaving()
{
    if (m_composing || m_sessionDialog || m_exportDialog || m_definitionDialog || m_clearConfirmation || !m_sessionFile) return;
    // 失败时继续持有文件与可见会话，用户可以重试或另存。
    if (!saveSession()) return;
    m_sessionFile.reset();
    updateSessionStatus();
}

void CalculatorPage::requestSessionFile(bool restore)
{
    if (m_composing || m_sessionDialog || m_definitionDialog || m_clearConfirmation || m_exportDialog) return;
    if (restore && (m_history.count() > 0 || !m_input->toPlainText().isEmpty() || m_sessionFile
        || !m_customInput.customDefinition.isEmpty() || customMode() || !m_normalInput.text.isEmpty()))
    {
        updateSessionStatus(QStringLiteral("请在没有记录、没有草稿且未开启保存的空白计算器中恢复会话。"));
        return;
    }
    auto *dialog = new QFileDialog(this);
    m_sessionDialog = dialog;
    dialog->setObjectName(QStringLiteral("sessionFileDialog"));
    dialog->setOption(QFileDialog::DontUseNativeDialog);
    dialog->setOption(QFileDialog::DontConfirmOverwrite);
    dialog->setAttribute(Qt::WA_WindowPropagation, true);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog->setWindowTitle(restore ? QStringLiteral("恢复本地会话") : QStringLiteral("选择新文件并开启自动保存"));
    dialog->setAcceptMode(restore ? QFileDialog::AcceptOpen : QFileDialog::AcceptSave);
    dialog->setFileMode(restore ? QFileDialog::ExistingFile : QFileDialog::AnyFile);
    dialog->setNameFilter(QStringLiteral("CalcTabdd 会话 (*.calctabdd)"));
    dialog->setDefaultSuffix(QStringLiteral("calctabdd"));
    dialog->setLabelText(QFileDialog::LookIn, QStringLiteral("位置："));
    dialog->setLabelText(QFileDialog::FileName, QStringLiteral("文件名："));
    dialog->setLabelText(QFileDialog::FileType, QStringLiteral("文件类型："));
    dialog->setLabelText(QFileDialog::Accept, restore ? QStringLiteral("恢复") : QStringLiteral("开启保存"));
    dialog->setLabelText(QFileDialog::Reject, QStringLiteral("取消"));
    dialog->setDirectory(m_sessionFile ? QFileInfo(m_sessionFile->path()).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (!restore) dialog->selectFile(QStringLiteral("CalcTabdd-%1.calctabdd").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)));
    connect(dialog, &QDialog::finished, this, [this, dialog, restore](int result) {
        m_sessionDialog.clear();
        if (result == QDialog::Accepted && !dialog->selectedFiles().isEmpty())
        {
            auto file = std::unique_ptr<CalculationSessionFile>(new CalculationSessionFile);
            CalculationSession session;
            const QString path = dialog->selectedFiles().first();
            const QString error = restore ? file->load(path, session) : file->create(path, captureSession());
            if (error.isEmpty())
            {
                m_saveTimer->stop();
                if (restore) restoreSession(session);
                m_sessionFile = std::move(file);
                m_sessionDirty = false;
                updateSessionStatus();
            }
            else updateSessionStatus(error);
        }
        if (isVisible())
        {
            window()->activateWindow();
            focusInput();
        }
    });
    dialog->open();
    CalculatorStyle::applyDialog(dialog, palette());
}

void CalculatorPage::storeModeDraft()
{
    (customMode() ? m_customInput : m_normalInput) = captureInput();
}

void CalculatorPage::updateModeUi()
{
    const bool custom = customMode();
    m_modeButton->setText(custom ? QStringLiteral("自定义公式") : QStringLiteral("普通计算"));
    m_normalModeAction->setChecked(!custom);
    m_customModeAction->setChecked(custom);
    m_inputLabel->setText(custom ? QStringLiteral("填写参数") : QStringLiteral("输入公式"));
    m_input->setAccessibleName(m_inputLabel->text());
    m_input->setPlaceholderText(custom ? QStringLiteral("每行填写 名称=数字；无参数时可直接确认计算") : QStringLiteral("例如 (128 + 256) / 3"));
    m_definitionLabel->setText(m_definition);
    m_definitionLabel->setVisible(custom);
    m_completion->setEnabled(!custom);
    CustomFormula formula;
    const int rows = custom && CustomFormula::parse(m_definition, formula).isEmpty()
        ? qBound(3, formula.parameters().size(), 6) : 0;
    m_input->setFixedHeight(custom ? rows * QFontMetrics(m_input->font()).lineSpacing() + 28
        : qMax(80, 2 * QFontMetrics(m_input->font()).lineSpacing() + 28));
    findChild<QLabel *>(QStringLiteral("inputHint"))->setText(custom
        ? QStringLiteral("每行一个参数 · Enter 换行 · Alt+↑↓ 召回历史／返回草稿")
        : QStringLiteral("@ 补全函数 · Alt+↑↓ 召回历史／返回草稿 · ans 上次结果"));
}

void CalculatorPage::switchMode(bool custom)
{
    if (m_composing || m_definitionDialog || m_clearConfirmation || m_exportDialog || m_sessionDialog) return;
    if (custom == customMode() && m_historyPosition < 0)
    {
        updateModeUi();
        focusInput();
        return;
    }
    if (custom && m_customInput.customDefinition.isEmpty() && !customMode())
    {
        updateModeUi();
        requestDefinition();
        return;
    }
    // 手动切换结束历史浏览，先恢复其原草稿，再切换到另一份模式草稿。
    if (m_historyPosition >= 0) restoreDraft();
    storeModeDraft();
    restoreInput(custom ? m_customInput : m_normalInput);
    m_status->setText(custom ? QStringLiteral("自定义公式模式 · 填写参数后按 Ctrl+Enter 计算")
                             : QStringLiteral("普通计算模式 · Ctrl+Enter 计算"));
    scheduleSave();
}

void CalculatorPage::requestDefinition()
{
    if (m_composing || m_definitionDialog || m_clearConfirmation || m_exportDialog || m_sessionDialog) return;
    auto *dialog = new QDialog(this);
    m_definitionDialog = dialog;
    dialog->setObjectName(QStringLiteral("customDefinitionDialog"));
    dialog->setWindowTitle(QStringLiteral("定义／修改公式"));
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setAttribute(Qt::WA_WindowPropagation, true);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);
    auto *description = new QLabel(QStringLiteral("例如 A=x+y 或 S=pi*r^2\n支持现有全部函数；名称区分大小写。确认后自动生成参数输入行。"), dialog);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *edit = new QLineEdit(dialog);
    edit->setObjectName(QStringLiteral("customDefinitionInput"));
    edit->setAccessibleName(QStringLiteral("自定义公式定义"));
    edit->setFont(m_input->font());
    edit->setPlaceholderText(QStringLiteral("A=x+y"));
    const InputState previous = customMode() ? captureInput() : m_customInput;
    edit->setText(previous.customDefinition);
    edit->selectAll();
    layout->addWidget(edit);
    auto *error = new QLabel(dialog);
    error->setObjectName(QStringLiteral("customDefinitionError"));
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("使用公式"));
    buttons->button(QDialogButtonBox::Ok)->setProperty("role", QStringLiteral("primary"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, edit, error, previous]() {
        CustomFormula formula;
        const QString problem = CustomFormula::parse(edit->text(), formula);
        if (!problem.isEmpty())
        {
            error->setText(problem);
            edit->setFocus();
            return;
        }
        if (m_historyPosition >= 0) restoreDraft();
        storeModeDraft();
        const QString input = formula.parameterTemplate(previous.text);
        const int position = input.isEmpty() ? 0 : input.indexOf(QLatin1Char('=')) + 1;
        m_customInput = {input, position, position, formula.definition()};
        restoreInput(m_customInput);
        m_status->setText(QStringLiteral("定义已保存 · 填写参数后按 Ctrl+Enter 计算"));
        scheduleSave();
        dialog->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::finished, this, [this](int) {
        m_definitionDialog.clear();
        if (isVisible())
        {
            window()->activateWindow();
            focusInput();
        }
    });
    dialog->resize(520, dialog->sizeHint().height());
    dialog->open();
    CalculatorStyle::applyDialog(dialog, palette());
    edit->setFocus();
}
