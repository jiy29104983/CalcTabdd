#include "record_text.h"
#include "calculator_page.h"
#include "calculator_help.h"
#include "calculator_style.h"
#include "formula_completion.h"
#include "custom_formula.h"
#include "programmer_engine.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputMethodEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QMenu>
#include <QDialog>
#include <QDialogButtonBox>
#include <QStyle>
#include <QToolButton>
#include <QTextEdit>
#include <QTextBlock>
#include <QTextDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QTimer>
#include <QtMath>
#include <QUuid>
#include <QVBoxLayout>

// 摘要按实际可用宽度省略，完整内容仍提供给辅助技术和工具提示。
class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(QWidget *parent) : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        setTextFormat(Qt::PlainText);
    }
    QSize minimumSizeHint() const override { return QSize(0, QLabel::minimumSizeHint().height()); }
    QSize sizeHint() const override
    {
        QSize hint = QLabel::sizeHint();
        hint.rwidth() += fontMetrics().horizontalAdvance(m_text) - fontMetrics().horizontalAdvance(text());
        return hint;
    }
    void setFullText(const QString &text)
    {
        m_text = text;
        setAccessibleName(text);
        setToolTip(text);
        updateText();
    }
protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateText();
    }
    void changeEvent(QEvent *event) override
    {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) updateText();
    }
private:
    void updateText()
    {
        setText(fontMetrics().elidedText(m_text, Qt::ElideRight, qMax(0, contentsRect().width() - 2 * margin())));
    }
    QString m_text;
};

namespace {
// 高度不足时滚动输入区，不能让固定高度编辑器与换行提示互相覆盖。
class ComposerScrollArea : public QScrollArea
{
public:
    explicit ComposerScrollArea(QWidget *content, QWidget *parent) : QScrollArea(parent)
    {
        setObjectName(QStringLiteral("formulaComposerScroll"));
        setFrameShape(QFrame::NoFrame);
        setWidgetResizable(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setFocusPolicy(Qt::NoFocus);
        QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
        setWidget(content);
        content->installEventFilter(this);
    }
    QSize minimumSizeHint() const override { return QSize(0, 80); }
    QSize sizeHint() const override { return QSize(widget()->sizeHint().width(), heightForWidth(width())); }
    int heightForWidth(int width) const override
    {
        return widget()->layout()->totalHeightForWidth(qMax(1, width));
    }
protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QScrollArea::resizeEvent(event);
        updateContentHeight();
    }
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == widget() && event->type() == QEvent::LayoutRequest)
        {
            updateContentHeight();
            updateGeometry();
        }
        return QScrollArea::eventFilter(object, event);
    }
private:
    void updateContentHeight()
    {
        const int height = heightForWidth(viewport()->width());
        if (widget()->minimumHeight() != height) widget()->setMinimumHeight(height);
    }
};

// 求值使用去空白后的公式，诊断位置需换算回输入框偏移。
int skippedWhitespace(const QString &text)
{
    int offset = 0;
    while (offset < text.size() && text.at(offset).isSpace()) ++offset;
    return offset;
}

void repolishWidget(QWidget *widget)
{
    if (!widget || !widget->style()) return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

// 导出与会话对话框共用同一套 Qt 控件设置，保证随页面关闭并沿用页面配色。
QFileDialog *createFileDialog(QWidget *owner, const QString &objectName, const QString &title,
                              QFileDialog::AcceptMode mode, const QString &acceptText,
                              const QString &nameFilter, const QString &suffix)
{
    auto *dialog = new QFileDialog(owner);
    dialog->setObjectName(objectName);
    dialog->setWindowTitle(title);
    dialog->setOption(QFileDialog::DontUseNativeDialog);
    dialog->setOption(QFileDialog::DontConfirmOverwrite);
    dialog->setAttribute(Qt::WA_WindowPropagation, true);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    // Qt 设置模式时会重建标准按钮，中文文案必须在此之后设置。
    dialog->setAcceptMode(mode);
    dialog->setFileMode(mode == QFileDialog::AcceptOpen ? QFileDialog::ExistingFile : QFileDialog::AnyFile);
    dialog->setNameFilter(nameFilter);
    dialog->setDefaultSuffix(suffix);
    dialog->setLabelText(QFileDialog::LookIn, QStringLiteral("位置："));
    dialog->setLabelText(QFileDialog::FileName, QStringLiteral("文件名："));
    dialog->setLabelText(QFileDialog::FileType, QStringLiteral("文件类型："));
    dialog->setLabelText(QFileDialog::Accept, acceptText);
    dialog->setLabelText(QFileDialog::Reject, QStringLiteral("取消"));
    return dialog;
}

QDialogButtonBox *confirmationButtons(QDialog *dialog, const QString &acceptText)
{
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(acceptText);
    buttons->button(QDialogButtonBox::Ok)->setProperty("role", QStringLiteral("danger"));
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setText(QStringLiteral("取消"));
    cancel->setDefault(true);
    cancel->setFocus();
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return buttons;
}

// 覆盖与清空确认共用同一套 Qt 控件设置，随页面关闭并沿用页面配色。
QDialog *createConfirmationDialog(QWidget *owner, const QString &objectName, const QString &title,
                                  QVBoxLayout *&layout)
{
    auto *dialog = new QDialog(owner);
    dialog->setObjectName(objectName);
    dialog->setWindowTitle(title);
    dialog->setAttribute(Qt::WA_WindowPropagation, true);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);
    return dialog;
}

// 模态框关闭后把焦点交回页面；从补全弹窗进入时先恢复所属窗口。
void refocusPage(CalculatorPage *page)
{
    if (!page->isVisible()) return;
    page->window()->activateWindow();
    page->focusInput();
}
}

CalculatorPage::CalculatorPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("calctabddPage"));
    if (parent) setFont(parent->font());
    // 页面覆盖宿主原生编辑器；样式表会关闭 autoFillBackground。
    // 显式绘制页面背景，避免行号栏和当前行高亮透过透明子控件。
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *headerPanel = new QFrame(this);
    headerPanel->setObjectName(QStringLiteral("calculatorHeader"));
    auto *headerLayout = new QVBoxLayout(headerPanel);
    headerLayout->setContentsMargins(20, 12, 20, 12);
    headerLayout->setSpacing(8);
    auto *header = new QHBoxLayout;
    header->setSpacing(8);
    auto *title = new QLabel(QStringLiteral("计算器"), this);
    title->setObjectName(QStringLiteral("calculatorTitle"));
    QFont titleFont = font();
    titleFont.setPointSizeF(qMax(12.0, titleFont.pointSizeF() + 2));
    titleFont.setBold(true);
    title->setFont(titleFont);
    header->addWidget(title);
    header->addStretch(1);
    m_answerBadge = new ElidedLabel(this);
    m_answerBadge->setObjectName(QStringLiteral("answerBadge"));
    header->addWidget(m_answerBadge);
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
    m_sessionButton->setToolTip(QStringLiteral("保存三种模式的记录、ans、草稿与设置；默认不保存，文件由当前窗口独占"));
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
    connect(m_saveNowAction, &QAction::triggered, this, [this]() { if (!busy()) saveSession(); });
    m_stopSavingAction = sessionMenu->addAction(QStringLiteral("停止保存（保留文件）"));
    m_stopSavingAction->setObjectName(QStringLiteral("stopSavingSession"));
    m_stopSavingAction->setEnabled(false);
    connect(m_stopSavingAction, &QAction::triggered, this, &CalculatorPage::stopSaving);
    toolbar->addWidget(m_sessionButton);
    m_clearButton = new QPushButton(QStringLiteral("清空会话"), this);
    m_clearButton->setObjectName(QStringLiteral("clearSessionButton"));
    m_clearButton->setToolTip(QStringLiteral("确认后清空记录并重置 ans，保留召回前的草稿"));
    connect(m_clearButton, &QPushButton::clicked, this, &CalculatorPage::requestClearSession);
    toolbar->addWidget(m_clearButton);
    auto *help = new QPushButton(QStringLiteral("帮助"), this);
    help->setObjectName(QStringLiteral("helpButton"));
    help->setToolTip(QStringLiteral("查看全部运算、函数、常量与精度限制"));
    connect(help, &QPushButton::clicked, this, [this]() { showCalculatorHelp(window(), programmerMode()); });
    toolbar->addWidget(help);
    layout->addWidget(headerPanel);
    m_sessionStatus = new QLabel(this);
    m_sessionStatus->setObjectName(QStringLiteral("sessionSaveStatus"));
    m_sessionStatus->setWordWrap(true);
    m_sessionStatus->setTextFormat(Qt::PlainText);
    m_sessionStatus->setContentsMargins(20, 8, 20, 8);
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
    m_records->setContentsMargins(20, 14, 20, 14);
    m_records->setSpacing(10);
    m_empty = new QLabel(QStringLiteral(
        "开始一次计算\n"
        "· 普通算式：在下方输入如 (128 + 256) / 3，按 Ctrl+Enter 查看结果\n"
        "· 函数与历史：输入 @ 查找函数与常量，按 Alt+↑ / Alt+↓ 召回历史\n"
        "· 自定义公式：切换模式后输入如 S=w*h 按 Enter 应用，逐行填参数计算\n"
        "· 程序员：选择位宽和符号，输入如 (0x12 & 0x0F) << 4"), history);
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

    // 只创建当前页的卡片，完整历史仍由模型负责保存、导出和召回。
    m_historyPager = new QWidget(this);
    m_historyPager->setObjectName(QStringLiteral("historyPager"));
    auto *pagerLayout = new QVBoxLayout(m_historyPager);
    pagerLayout->setContentsMargins(20, 4, 20, 4);
    auto *pageLayout = new ButtonFlowLayout;
    pagerLayout->addLayout(pageLayout);
    pageLayout->setSpacing(8);
    m_historyOlderPage = new QPushButton(QStringLiteral("较早记录"), m_historyPager);
    m_historyOlderPage->setObjectName(QStringLiteral("historyOlderPage"));
    m_historyNewerPage = new QPushButton(QStringLiteral("较新记录"), m_historyPager);
    m_historyNewerPage->setObjectName(QStringLiteral("historyNewerPage"));
    for (auto *button : {m_historyOlderPage, m_historyNewerPage})
    {
        button->setProperty("role", QStringLiteral("quiet"));
        button->setProperty("compact", true);
    }
    m_historyPageRange = new QLabel(m_historyPager);
    m_historyPageRange->setObjectName(QStringLiteral("historyPageRange"));
    pageLayout->addWidget(m_historyOlderPage);
    pageLayout->addWidget(m_historyNewerPage);
    pageLayout->addWidget(m_historyPageRange);
    connect(m_historyOlderPage, &QPushButton::clicked, this, [this]() {
        if (!busy() && m_recordStart > 0) showRecordPage(m_recordStart);
    });
    connect(m_historyNewerPage, &QPushButton::clicked, this, [this]() {
        if (!busy() && m_recordEnd < m_history.count())
            showRecordPage(qMin(m_history.count(), m_recordEnd + RecordPageSize));
    });
    layout->addWidget(m_historyPager);

    auto *composer = new QFrame(this);
    composer->setObjectName(QStringLiteral("formulaComposer"));
    auto *composeLayout = new QVBoxLayout(composer);
    composeLayout->setContentsMargins(20, 12, 20, 14);
    composeLayout->setSpacing(8);
    m_historyBadge = new QLabel(composer);
    m_historyBadge->setObjectName(QStringLiteral("historyRecallBadge"));
    m_historyBadge->setWordWrap(true);
    m_historyBadge->hide();

    m_normalModeAction = new QAction(QStringLiteral("普通计算模式"), this);
    m_normalModeAction->setObjectName(QStringLiteral("normalCalculationMode"));
    m_normalModeAction->setCheckable(true);
    connect(m_normalModeAction, &QAction::triggered, this, [this]() { switchMode(Mode::Normal); });
    m_customModeAction = new QAction(QStringLiteral("自定义公式模式"), this);
    m_customModeAction->setObjectName(QStringLiteral("customCalculationMode"));
    m_customModeAction->setCheckable(true);
    connect(m_customModeAction, &QAction::triggered, this, [this]() { switchMode(Mode::Custom); });
    m_programmerModeAction = new QAction(QStringLiteral("程序员模式"), this);
    m_programmerModeAction->setObjectName(QStringLiteral("programmerCalculationMode"));
    m_programmerModeAction->setCheckable(true);
    connect(m_programmerModeAction, &QAction::triggered, this, [this]() { switchMode(Mode::Programmer); });

    auto *modeSwitcher = new QFrame(composer);
    modeSwitcher->setObjectName(QStringLiteral("modeSwitcher"));
    auto *modeLayout = new ButtonFlowLayout;
    modeSwitcher->setLayout(modeLayout);
    modeLayout->setContentsMargins(2, 2, 2, 2);
    modeLayout->setSpacing(2);
    m_normalModeButton = new QToolButton(modeSwitcher);
    m_normalModeButton->setObjectName(QStringLiteral("normalModeButton"));
    m_normalModeButton->setText(QStringLiteral("普通计算"));
    m_normalModeButton->setCheckable(true);
    m_normalModeButton->setFocusPolicy(Qt::TabFocus);
    m_normalModeButton->setProperty("modeSegment", true);
    m_normalModeButton->setProperty("compact", true);
    m_normalModeButton->setToolTip(QStringLiteral("切换为普通计算模式"));
    connect(m_normalModeButton, &QToolButton::clicked, m_normalModeAction, &QAction::trigger);
    m_customModeButton = new QToolButton(modeSwitcher);
    m_customModeButton->setObjectName(QStringLiteral("customModeButton"));
    m_customModeButton->setText(QStringLiteral("自定义公式"));
    m_customModeButton->setCheckable(true);
    m_customModeButton->setFocusPolicy(Qt::TabFocus);
    m_customModeButton->setProperty("modeSegment", true);
    m_customModeButton->setProperty("compact", true);
    m_customModeButton->setToolTip(QStringLiteral("切换为自定义公式模式"));
    connect(m_customModeButton, &QToolButton::clicked, m_customModeAction, &QAction::trigger);
    modeLayout->addWidget(m_normalModeButton);
    modeLayout->addWidget(m_customModeButton);
    m_programmerModeButton = new QToolButton(modeSwitcher);
    m_programmerModeButton->setObjectName(QStringLiteral("programmerModeButton"));
    m_programmerModeButton->setText(QStringLiteral("程序员"));
    m_programmerModeButton->setCheckable(true);
    m_programmerModeButton->setFocusPolicy(Qt::TabFocus);
    m_programmerModeButton->setProperty("modeSegment", true);
    m_programmerModeButton->setProperty("compact", true);
    m_programmerModeButton->setToolTip(QStringLiteral("固定宽度整数与位运算"));
    connect(m_programmerModeButton, &QToolButton::clicked, m_programmerModeAction, &QAction::trigger);
    modeLayout->addWidget(m_programmerModeButton);
    composeLayout->addWidget(modeSwitcher);
    composeLayout->addWidget(m_historyBadge);

    m_programmerPanel = new QWidget(composer);
    m_programmerPanel->setObjectName(QStringLiteral("programmerPanel"));
    auto *programmerLayout = new QVBoxLayout(m_programmerPanel);
    programmerLayout->setContentsMargins(0, 0, 0, 0);
    programmerLayout->setSpacing(6);
    auto *settings = new ButtonFlowLayout;
    const auto setting = [this, settings](const QString &labelText, const QString &name) {
        auto *group = new QWidget(m_programmerPanel);
        auto *row = new QHBoxLayout(group);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        auto *label = new QLabel(labelText, group);
        auto *combo = new QComboBox(group);
        combo->setObjectName(name);
        combo->setAccessibleName(labelText);
        label->setBuddy(combo);
        row->addWidget(label);
        row->addWidget(combo);
        settings->addWidget(group);
        return combo;
    };
    m_bitWidth = setting(QStringLiteral("位宽"), QStringLiteral("programmerBitWidth"));
    for (int bits : {8, 16, 32, 64}) m_bitWidth->addItem(QStringLiteral("%1 位").arg(bits), bits);
    m_bitWidth->setCurrentIndex(2);
    m_integerType = setting(QStringLiteral("类型"), QStringLiteral("programmerIntegerType"));
    m_integerType->addItem(QStringLiteral("无符号"), false);
    m_integerType->addItem(QStringLiteral("有符号"), true);
    auto *integerHint = new QLabel(QStringLiteral("^ 异或"), m_programmerPanel);
    integerHint->setObjectName(QStringLiteral("programmerHint"));
    integerHint->setWordWrap(true);
    integerHint->setToolTip(QStringLiteral("程序员模式 ^ 表示按位异或；普通计算与自定义公式中表示乘方"));
    settings->addWidget(integerHint);
    programmerLayout->addLayout(settings);
    composeLayout->addWidget(m_programmerPanel);
    const auto configChanged = [this]() {
        clearInputError();
        showStatus(QStringLiteral("设置已更新"), true);
        scheduleSave();
    };
    connect(m_bitWidth, QOverload<int>::of(&QComboBox::currentIndexChanged), this, configChanged);
    connect(m_integerType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, configChanged);

    m_definitionPanel = new QWidget(composer);
    m_definitionPanel->setObjectName(QStringLiteral("customDefinitionPanel"));
    auto *definitionLayout = new QVBoxLayout(m_definitionPanel);
    definitionLayout->setContentsMargins(0, 0, 0, 0);
    definitionLayout->setSpacing(6);
    auto *definitionHeader = new ButtonFlowLayout;
    definitionHeader->setSpacing(8);
    auto *definitionLabel = new QLabel(QStringLiteral("公式定义"), m_definitionPanel);
    definitionLabel->setObjectName(QStringLiteral("customDefinitionLabel"));
    definitionHeader->addWidget(definitionLabel);
    m_definitionStateBadge = new QLabel(QStringLiteral("未应用"), m_definitionPanel);
    m_definitionStateBadge->setObjectName(QStringLiteral("customDefinitionStateBadge"));
    definitionHeader->addWidget(m_definitionStateBadge);
    m_applyDefinitionButton = new QPushButton(QStringLiteral("应用公式"), m_definitionPanel);
    m_applyDefinitionButton->setObjectName(QStringLiteral("applyDefinitionButton"));
    m_applyDefinitionButton->setProperty("role", QStringLiteral("quiet"));
    m_applyDefinitionButton->setProperty("compact", true);
    m_applyDefinitionButton->setFocusPolicy(Qt::TabFocus);
    m_applyDefinitionButton->setToolTip(QStringLiteral("Enter 应用公式；Esc 还原未应用的修改"));
    connect(m_applyDefinitionButton, &QPushButton::clicked, this, &CalculatorPage::applyDefinition);
    definitionHeader->addWidget(m_applyDefinitionButton);
    definitionLayout->addLayout(definitionHeader);
    m_definitionInput = new QPlainTextEdit(m_definitionPanel);
    m_definitionInput->setObjectName(QStringLiteral("currentCustomDefinition"));
    m_definitionInput->setAccessibleName(QStringLiteral("自定义公式定义"));
    m_definitionInput->setPlaceholderText(QStringLiteral("例如 S=w*h · @ 补全"));
    m_definitionInput->setTabChangesFocus(true);
    m_definitionInput->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_definitionInput->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_definitionInput->installEventFilter(this);
    definitionLayout->addWidget(m_definitionInput);
    definitionLabel->setBuddy(m_definitionInput);
    m_definitionError = new QLabel(m_definitionPanel);
    m_definitionError->setObjectName(QStringLiteral("customDefinitionError"));
    m_definitionError->setTextFormat(Qt::PlainText);
    m_definitionError->setWordWrap(true);
    definitionLayout->addWidget(m_definitionError);
    composeLayout->addWidget(m_definitionPanel);

    m_parameterHeader = new QWidget(composer);
    m_parameterHeader->setObjectName(QStringLiteral("customParameterHeader"));
    auto *parameterHeaderLayout = new QHBoxLayout(m_parameterHeader);
    parameterHeaderLayout->setContentsMargins(0, 2, 0, 0);
    parameterHeaderLayout->setSpacing(8);
    auto *parameterLabel = new QLabel(QStringLiteral("填写参数"), m_parameterHeader);
    parameterLabel->setObjectName(QStringLiteral("parameterSectionLabel"));
    parameterHeaderLayout->addWidget(parameterLabel);
    parameterHeaderLayout->addStretch(1);
    composeLayout->addWidget(m_parameterHeader);

    m_input = new QPlainTextEdit(composer);
    m_input->setObjectName(QStringLiteral("formulaInput"));
    m_input->setAccessibleName(QStringLiteral("输入公式"));
    m_input->setPlaceholderText(QStringLiteral("例如 (128 + 256) / 3"));
    m_input->setTabChangesFocus(true);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(qMax(11.0, font().pointSizeF() + 1));
    m_input->setFont(mono);
    m_definitionInput->setFont(mono);
    m_input->installEventFilter(this);
    parameterLabel->setBuddy(m_input);
    m_editTarget = m_input;
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (!now || !isAncestorOf(now)) return;
        if (now == m_definitionInput || m_definitionInput->isAncestorOf(now)) m_editTarget = m_definitionInput;
        else if (now == m_input || m_input->isAncestorOf(now)) m_editTarget = m_input;
        else if (auto *result = qobject_cast<RecordText *>(now)) m_editTarget = result;
        else if (auto *label = qobject_cast<QLabel *>(now))
        {
            if (label->textInteractionFlags() & Qt::TextSelectableByKeyboard) m_editTarget = label;
        }
    });
    composeLayout->addWidget(m_input);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(16);
    m_status = new QLabel(composer);
    m_status->setObjectName(QStringLiteral("calculationStatus"));
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    footer->addWidget(m_status, 1);
    m_statusTimer = new QTimer(this);
    m_statusTimer->setObjectName(QStringLiteral("calculationStatusTimer"));
    m_statusTimer->setSingleShot(true);
    m_statusTimer->setInterval(4000);
    connect(m_statusTimer, &QTimer::timeout, this, [this]() { m_status->clear(); });

    auto *calculate = new QPushButton(QStringLiteral("计算"), composer);
    calculate->setObjectName(QStringLiteral("calculateButton"));
    calculate->setProperty("role", QStringLiteral("primary"));
    calculate->setMinimumWidth(96);
    calculate->setToolTip(QStringLiteral("计算并新增记录（Ctrl+Enter）"));
    connect(calculate, &QPushButton::clicked, this, &CalculatorPage::submit);
    footer->addWidget(calculate, 0, Qt::AlignVCenter);
    composeLayout->addLayout(footer);
    layout->addWidget(new ComposerScrollArea(composer, this));

    connect(m_input, &QPlainTextEdit::textChanged, this, [this]() {
        clearInputError();
        showStatus(QString());
        updateInputHeights();
    });
    m_completion = new FormulaCompletion(m_input);
    connect(m_completion, &FormulaCompletion::calculationRequested, this, &CalculatorPage::submit);
    connect(m_completion, &FormulaCompletion::hintChanged, this,
            [this](const QString &text) { showStatus(text, true); });
    connect(m_definitionInput, &QPlainTextEdit::textChanged, this, [this]() {
        m_definitionError->clear();
        m_definitionError->hide();
        updateDefinitionStateUi();
        showStatus(QString());
        updateInputHeights();
    });
    m_definitionCompletion = new FormulaCompletion(m_definitionInput, true);
    connect(m_definitionCompletion, &FormulaCompletion::calculationRequested, this, &CalculatorPage::applyDefinition);
    connect(m_definitionCompletion, &FormulaCompletion::hintChanged, this,
            [this](const QString &text) { showStatus(text, true); });
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(200);
    connect(m_saveTimer, &QTimer::timeout, this, [this]() { saveSession(); });
    connect(m_input, &QPlainTextEdit::textChanged, this, &CalculatorPage::scheduleSave);
    connect(m_input, &QPlainTextEdit::cursorPositionChanged, this, &CalculatorPage::scheduleSave);
    connect(m_input, &QPlainTextEdit::selectionChanged, this, &CalculatorPage::scheduleSave);
    setFocusProxy(m_input);
    // 显式串起焦点代理前创建的控件，保持输入后 Tab 仍先到“计算”。
    setTabOrder(m_input, calculate);
    setTabOrder(calculate, m_normalModeButton);
    setTabOrder(m_normalModeButton, m_customModeButton);
    setTabOrder(m_customModeButton, m_programmerModeButton);
    setTabOrder(m_programmerModeButton, m_bitWidth);
    setTabOrder(m_bitWidth, m_integerType);
    setTabOrder(m_integerType, m_applyDefinitionButton);
    setTabOrder(m_applyDefinitionButton, m_definitionInput);
    updateModeUi();
    updateRecordUi();
    applyTheme();
}

void CalculatorPage::focusInput()
{
    if (dialogOpen()) return;
    (hasPendingDefinition() ? m_definitionInput : m_input)->setFocus(Qt::OtherFocusReason);
}

bool CalculatorPage::dialogOpen() const
{
    return m_clearConfirmation || m_exportDialog || m_sessionDialog;
}

bool CalculatorPage::busy() const
{
    return m_composing || dialogOpen();
}

void CalculatorPage::showStatus(const QString &text, bool transient)
{
    m_statusTimer->stop();
    m_status->setText(text);
    if (transient && !text.isEmpty()) m_statusTimer->start();
}

void CalculatorPage::submit()
{
    if (busy()) return;
    if (hasPendingDefinition())
    {
        showStatus(QStringLiteral("请先在公式定义栏按 Enter 应用修改，再计算参数"));
        focusInput();
        return;
    }
    const QString inputText = m_input->toPlainText();
    const QString expression = inputText.trimmed();
    if (!customMode() && expression.isEmpty())
    {
        showStatus(QStringLiteral("请先输入公式"));
        focusInput();
        return;
    }
    if (!customMode() && expression.contains(QLatin1Char('@')))
    {
        showStatus(QStringLiteral("请先完成 @ 补全，或删除 @ 查询后再计算"));
        focusInput();
        return;
    }
    QString validationError;
    const CalculationRecord entry = programmerMode() ? m_history.calculateProgrammer(expression, programmerConfig()) : customMode()
        ? m_history.calculateCustom(m_definition, inputText, validationError) : m_history.calculate(expression);
    if (!validationError.isEmpty())
    {
        showStatus(validationError);
        focusInput();
        return;
    }
    appendLatestRecord(entry);
    const CalculationResult &result = entry.result;
    if (result.ok)
    {
        if (m_historyPosition >= 0)
        {
            restoreDraft();
            showStatus(QStringLiteral("计算完成 · 已返回草稿"), true);
        }
        else
        {
            if (!customMode()) m_input->clear();
            showStatus(QStringLiteral("计算完成"), true);
        }
    }
    else if (customMode())
    {
        // 数学错误位置属于历史代入式，不能当成参数输入框的字符偏移。
        clearInputError();
        showStatus(QStringLiteral("无法计算：%1 · 请检查参数或修改定义").arg(result.text));
    }
    else
    {
        showInputError(result, skippedWhitespace(inputText));
    }
    if (m_sessionFile) saveSession();
    focusInput();
    QTimer::singleShot(0, this, [this]() {
        if (m_followLatest) m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
    });
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
    showStatus(message);
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
    showStatus(QString());
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
    if (busy() || m_history.count() == 0) return;
    const bool markdown = format == CalculationExport::Format::Markdown;
    const QByteArray contents = CalculationExport::serialize(m_history.records(), format);
    // 使用可随页面关闭的异步 Qt 对话框。覆盖确认也使用 Qt 控件，保持相同生命周期。
    auto *dialog = createFileDialog(this, QStringLiteral("exportHistoryDialog"),
        markdown ? QStringLiteral("导出 Markdown 计算记录") : QStringLiteral("导出 TXT 计算记录"),
        QFileDialog::AcceptSave, QStringLiteral("导出"),
        markdown ? QStringLiteral("Markdown (*.md)") : QStringLiteral("文本文件 (*.txt)"),
        markdown ? QStringLiteral("md") : QStringLiteral("txt"));
    m_exportDialog = dialog;
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
        if (!m_exportDialog) refocusPage(this);
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
    QVBoxLayout *layout = nullptr;
    auto *dialog = createConfirmationDialog(this, QStringLiteral("exportOverwriteConfirmation"),
                                            QStringLiteral("确认覆盖导出文件"), layout);
    m_exportDialog = dialog;
    auto *question = new QLabel(QStringLiteral("文件已存在，是否用当前计算记录覆盖？\n%1").arg(path), dialog);
    question->setTextFormat(Qt::PlainText);
    question->setWordWrap(true);
    layout->addWidget(question);
    layout->addWidget(confirmationButtons(dialog, QStringLiteral("覆盖")));
    dialog->resize(480, dialog->sizeHint().height());
    connect(dialog, &QDialog::finished, this, [this, path, contents](int result) {
        m_exportDialog.clear();
        if (result == QDialog::Accepted) writeExport(path, contents);
        refocusPage(this);
    });
    dialog->open();
    CalculatorStyle::applyDialog(dialog, palette());
}

void CalculatorPage::writeExport(const QString &path, const QByteArray &contents)
{
    const QString error = CalculationExport::writeFile(path, contents);
    showStatus(error.isEmpty()
        ? QStringLiteral("已导出 %1 条记录：%2").arg(m_history.count()).arg(path)
        : QStringLiteral("导出失败：%1\n%2\n记录仍保留，可再次点击“导出记录”重试或选择其他位置。")
              .arg(path, error), error.isEmpty());
}

void CalculatorPage::requestClearSession()
{
    if (busy() || m_history.count() == 0) return;
    // 使用与帮助窗口一致的 Qt 控件对话框，避免 Qt 5.15.2 QMessageBox
    // 在 Windows offscreen 平台的原生系统菜单访问。
    QVBoxLayout *layout = nullptr;
    auto *dialog = createConfirmationDialog(this, QStringLiteral("clearSessionConfirmation"),
                                            QStringLiteral("清空会话"), layout);
    m_clearConfirmation = dialog;
    auto *question = new QLabel(
        QStringLiteral("清空当前窗口的 %1 条计算记录，并将数学与整数 ans 重置为 0？").arg(m_history.count()), dialog);
    question->setTextFormat(Qt::PlainText);
    question->setWordWrap(true);
    layout->addWidget(question);
    auto *details = new QLabel(m_historyPosition >= 0
        ? QStringLiteral("将返回召回前的草稿，并丢弃本轮历史公式的临时编辑。三种模式的原草稿及程序员设置保留。清空不可撤销；其他窗口不受影响。")
        : QStringLiteral("三种模式的草稿、光标和选区会保留，程序员位宽与符号设置不变。清空不可撤销；其他窗口不受影响。"), dialog);
    if (m_sessionFile) details->setText(details->text() + QStringLiteral("\n本地保存已开启：清空也会更新会话文件。"));
    details->setObjectName(QStringLiteral("clearSessionDetails"));
    details->setTextFormat(Qt::PlainText);
    details->setWordWrap(true);
    layout->addWidget(details);
    layout->addWidget(confirmationButtons(dialog, QStringLiteral("清空会话")));
    dialog->setMinimumWidth(380);
    applyTheme();
    connect(dialog, &QDialog::finished, this, [this](int result) {
        m_clearConfirmation.clear();
        if (result == QDialog::Accepted) clearSession();
        // 从补全弹窗进入确认时，先恢复所属窗口，避免焦点留在已隐藏的候选窗。
        refocusPage(this);
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
    clearRecordWidgets();
    m_recordStart = m_recordEnd = 0;
    m_history.clear();
    updateModeUi();
    updateRecordUi();
    m_empty->show();
    m_followLatest = true;
    m_scroll->verticalScrollBar()->setValue(0);
    showStatus(QStringLiteral("会话已清空 · 草稿已保留"), true);
    // 恢复只发生在未开启保存的空白页，因此此处 m_sessionFile 一定为空。
    if (m_sessionFile) saveSession();
}

// 记录数与输入法组词共同决定记录区按钮的可用性；集中派生避免各处状态漂移。
void CalculatorPage::updateRecordUi()
{
    m_count->setText(QStringLiteral("%1 条记录").arg(m_history.count()));
    if (m_historyPosition >= 0 && m_history.count() > 0)
    {
        m_historyBadge->setText(QStringLiteral("历史浏览 %1/%2 · Alt+↓ 返回草稿")
            .arg(m_historyPosition + 1, 2, 10, QLatin1Char('0'))
            .arg(m_history.count(), 2, 10, QLatin1Char('0')));
        m_historyBadge->show();
    }
    else
    {
        m_historyBadge->hide();
    }
    const QString ansText = programmerMode() ? m_history.programmerAnswer().text() : m_history.answer().text();
    m_answerBadge->setFullText(QStringLiteral("ans = %1").arg(ansText));
    const QString ansSource = programmerMode() ? m_history.programmerAnswer().sourceText() : m_history.answer().sourceText();
    m_answerBadge->setToolTip(ansSource.isEmpty()
        ? QStringLiteral("上次成功结果：ans = %1").arg(ansText)
        : QStringLiteral("上次成功结果：ans = %1\n%2").arg(ansText, ansSource));
    const bool available = !m_composing;
    const bool hasRecords = m_history.count() > 0;
    m_historyPager->setVisible(m_history.count() > RecordPageSize);
    m_historyPageRange->setText(QStringLiteral("第 %1–%2 条 · 共 %3 条")
        .arg(hasRecords ? m_recordStart + 1 : 0).arg(m_recordEnd).arg(m_history.count()));
    m_historyOlderPage->setEnabled(available && m_recordStart > 0);
    m_historyNewerPage->setEnabled(available && m_recordEnd < m_history.count());
    m_sessionButton->setEnabled(available);
    m_normalModeButton->setEnabled(available);
    m_customModeButton->setEnabled(available);
    m_programmerModeButton->setEnabled(available);
    m_bitWidth->setEnabled(available);
    m_integerType->setEnabled(available);
    m_applyDefinitionButton->setEnabled(available);
    m_clearButton->setEnabled(hasRecords && available);
    m_exportButton->setEnabled(hasRecords && available);
    if (auto *calculate = findChild<QPushButton *>(QStringLiteral("calculateButton")))
        calculate->setEnabled(available);
    for (auto *reuse : findChildren<QPushButton *>(QStringLiteral("reuseFormula")))
        reuse->setEnabled(available);
}

void CalculatorPage::clearRecordWidgets()
{
    if (m_editTarget && m_scroll->widget()->isAncestorOf(m_editTarget))
        m_editTarget = hasPendingDefinition() ? m_definitionInput : m_input;
    while (m_records->count() > 2)
    {
        QLayoutItem *item = m_records->takeAt(1); // 保留空状态提示和底部 stretch。
        delete item->widget();
        delete item;
    }
}

void CalculatorPage::showRecordPage(int end)
{
    m_recordEnd = qBound(0, end, m_history.count());
    m_recordStart = qMax(0, m_recordEnd - RecordPageSize);
    m_followLatest = m_recordEnd == m_history.count();
    // 批量重建只在末尾布局和刷新状态，避免每条卡片扫描已有控件。
    const bool updatesEnabled = m_scroll->updatesEnabled();
    m_scroll->setUpdatesEnabled(false);
    m_records->setEnabled(false);
    clearRecordWidgets();
    for (int i = m_recordStart; i < m_recordEnd; ++i) appendRecord(m_history.records().at(i));
    m_records->setEnabled(true);
    m_records->activate();
    m_scroll->setUpdatesEnabled(updatesEnabled);
    updateRecordUi();
    m_scroll->verticalScrollBar()->setValue(m_followLatest ? m_scroll->verticalScrollBar()->maximum() : 0);
}

void CalculatorPage::appendLatestRecord(const CalculationRecord &entry)
{
    if (m_recordEnd != m_history.count() - 1)
    {
        showRecordPage(m_history.count());
        return;
    }
    m_followLatest = true;
    if (m_recordEnd - m_recordStart == RecordPageSize)
    {
        QLayoutItem *item = m_records->takeAt(1);
        if (m_editTarget && item->widget()->isAncestorOf(m_editTarget))
            m_editTarget = hasPendingDefinition() ? m_definitionInput : m_input;
        delete item->widget();
        delete item;
        ++m_recordStart;
    }
    ++m_recordEnd;
    appendRecord(entry);
    updateRecordUi();
}

void CalculatorPage::appendRecord(const CalculationRecord &entry)
{
    const CalculationResult &result = entry.result;
    m_empty->hide();
    auto *record = new QFrame(m_scroll->widget());
    record->setObjectName(QStringLiteral("calculationRecord"));
    record->setProperty("error", !result.ok);
    auto *cardLayout = new QVBoxLayout(record);
    cardLayout->setContentsMargins(16, 12, 16, 12);
    cardLayout->setSpacing(6);

    auto *topBar = new ButtonFlowLayout;
    topBar->setSpacing(8);
    auto *number = new QLabel(QStringLiteral("%1").arg(entry.id, 2, 10, QLatin1Char('0')), record);
    number->setObjectName(QStringLiteral("recordNumber"));
    topBar->addWidget(number);
    if (entry.programmer || !entry.customDefinition.isEmpty())
    {
        const QString label = entry.programmer ? QStringLiteral("程序员 · %1 位 · %2")
            .arg(entry.programmerConfig.bitWidth)
            .arg(entry.programmerConfig.isSigned ? QStringLiteral("有符号") : QStringLiteral("无符号"))
            : QStringLiteral("自定义公式");
        auto *modeBadge = new QLabel(label, record);
        modeBadge->setObjectName(QStringLiteral("recordModeBadge"));
        topBar->addWidget(modeBadge);
    }
    if (!result.ok)
    {
        auto *statusBadge = new QLabel(QStringLiteral("需要修正"), record);
        statusBadge->setObjectName(QStringLiteral("recordStatusBadge"));
        topBar->addWidget(statusBadge);
    }

    auto *reuse = new QPushButton(result.ok ? QStringLiteral("再次使用") : QStringLiteral("修改公式"), record);
    reuse->setObjectName(QStringLiteral("reuseFormula"));
    reuse->setProperty("role", QStringLiteral("quiet"));
    reuse->setProperty("compact", true);
    reuse->setToolTip(QStringLiteral("回填这条记录，编辑后确认计算；Alt+↓ 向后浏览至草稿"));
    connect(reuse, &QPushButton::clicked, this, [this, id = entry.id]() { reuseFormula(id); });
    auto *more = new QToolButton(record);
    more->setObjectName(QStringLiteral("recordActions"));
    more->setText(QStringLiteral("记录操作"));
    more->setProperty("role", QStringLiteral("quiet"));
    more->setProperty("compact", true);
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
        if (entry.programmer)
        {
            for (int radix : {16, 2, 8})
            {
                const QString name = radix == 16 ? QStringLiteral("Hex") : radix == 2 ? QStringLiteral("Bin") : QStringLiteral("Oct");
                auto *copy = menu->addAction(QStringLiteral("复制 ") + name.toUpper());
                copy->setObjectName(QStringLiteral("copy") + name);
                copy->setToolTip(QStringLiteral("复制原记录的位模式，含进制前缀、无分组；不携带位宽、符号或回绕来源"));
                connect(copy, &QAction::triggered, this, [this, id = entry.id, radix]() { copyRecord(id, true, radix); });
            }
        }
    }
    auto *copyCalculation = menu->addAction(QStringLiteral("复制整条计算"));
    copyCalculation->setObjectName(QStringLiteral("copyCalculation"));
    connect(copyCalculation, &QAction::triggered, this, [this, id = entry.id]() { copyRecord(id, false); });
    if (result.ok)
    {
        auto *insert = menu->addAction(QStringLiteral("插入结果数值"));
        insert->setObjectName(QStringLiteral("insertResult"));
        insert->setToolTip(QStringLiteral("插入当时的十进制数值；程序员输入须为当前范围内的整数，不保留原类型和计算来源。首次定义公式时须先应用或取消定义"));
        connect(menu, &QMenu::aboutToShow, this, [this, insert]() {
            insert->setEnabled(!m_defining);
        });
        connect(insert, &QAction::triggered, this, [this, id = entry.id]() { insertResult(id); });
    }
    topBar->addWidget(reuse);
    topBar->addWidget(more);
    cardLayout->addLayout(topBar);

    auto *values = new QVBoxLayout;
    values->setSpacing(4);
    if (!entry.customDefinition.isEmpty())
    {
        QStringList params;
        for (const QString &line : entry.parameterInput.split(QLatin1Char('\n')))
            if (!line.trimmed().isEmpty()) params.append(line.trimmed());
        const QString metaText = params.isEmpty()
            ? QStringLiteral("定义：%1").arg(entry.customDefinition)
            : QStringLiteral("定义：%1  ·  参数：%2").arg(entry.customDefinition, params.join(QStringLiteral(", ")));
        auto *meta = new RecordText(metaText, record);
        meta->setObjectName(QStringLiteral("recordCustomMeta"));
        values->addWidget(meta);
    }
    auto *formula = new RecordText(entry.expression, record);
    formula->setObjectName(QStringLiteral("recordFormula"));
    if (!entry.customDefinition.isEmpty()) formula->setToolTip(entry.customDefinition + QLatin1Char('\n') + entry.parameterInput);
    formula->setFont(m_input->font());
    values->addWidget(formula);
    auto *value = new RecordText(result.ok ? QStringLiteral("= %1").arg(result.text) : QStringLiteral("无法计算：%1").arg(result.text), record);
    value->setObjectName(QStringLiteral("recordResult"));
    value->setProperty("error", !result.ok);
    QFont resultFont = result.ok ? m_input->font() : font();
    if (result.ok)
    {
        resultFont.setPointSizeF(m_input->font().pointSizeF() + 4);
        resultFont.setBold(true);
    }
    value->setFont(resultFont);
    values->addWidget(value);
    if (result.ok && !entry.sourceText().isEmpty())
    {
        auto *sources = new RecordText(entry.sourceText(), record);
        sources->setObjectName(QStringLiteral("recordSources"));
        values->addWidget(sources);
    }
    if (entry.programmer && result.ok)
    {
        auto *hex = new RecordText(QStringLiteral("HEX  ") + entry.programmerValue.hexText(), record);
        hex->setObjectName(QStringLiteral("recordHex"));
        hex->setFont(m_input->font());
        values->addWidget(hex);
        auto *toggle = new QToolButton(record);
        toggle->setObjectName(QStringLiteral("toggleRadixDetails"));
        toggle->setText(QStringLiteral("展开进制详情"));
        toggle->setCheckable(true);
        toggle->setFocusPolicy(Qt::TabFocus);
        toggle->setProperty("role", QStringLiteral("quiet"));
        toggle->setProperty("compact", true);
        values->addWidget(toggle, 0, Qt::AlignLeft);
        auto *details = new QWidget(record);
        details->setObjectName(QStringLiteral("radixDetails"));
        auto *detailsLayout = new QVBoxLayout(details);
        detailsLayout->setContentsMargins(0, 0, 0, 0);
        detailsLayout->setSpacing(4);
        auto *bin = new RecordText(QStringLiteral("BIN  ") + entry.programmerValue.binaryText(), details);
        bin->setObjectName(QStringLiteral("recordBin"));
        bin->setFont(m_input->font());
        detailsLayout->addWidget(bin);
        auto *oct = new RecordText(QStringLiteral("OCT  ") + entry.programmerValue.octalText(), details);
        oct->setObjectName(QStringLiteral("recordOct"));
        oct->setFont(m_input->font());
        detailsLayout->addWidget(oct);
        values->addWidget(details);
        details->hide();
        connect(toggle, &QToolButton::toggled, details, [this, details, toggle](bool expanded) {
            m_followLatest = false;
            details->setVisible(expanded);
            toggle->setText(expanded ? QStringLiteral("收起进制详情") : QStringLiteral("展开进制详情"));
        });
        CalculatorStyle::prepareButton(toggle);
    }
    cardLayout->addLayout(values);

    m_records->insertWidget(m_records->count() - 1, record);
    record->show();
    CalculatorStyle::prepareButton(reuse);
    CalculatorStyle::prepareButton(more);
}

CalculatorPage::InputState CalculatorPage::captureInput() const
{
    const QTextCursor cursor = m_input->textCursor();
    return {m_input->toPlainText(), cursor.position(), cursor.anchor(), m_definition,
            customMode() ? m_definitionInput->toPlainText() : QString(),
            m_programmerMode, m_programmerMode ? programmerConfig() : ProgrammerConfig()};
}

void CalculatorPage::restoreInput(const InputState &state)
{
    m_definition = state.customDefinition;
    m_programmerMode = state.programmer;
    m_defining = false;
    if (programmerMode())
    {
        const QSignalBlocker widthBlocker(m_bitWidth), typeBlocker(m_integerType);
        m_bitWidth->setCurrentIndex(m_bitWidth->findData(state.programmerConfig.bitWidth));
        m_integerType->setCurrentIndex(state.programmerConfig.isSigned ? 1 : 0);
    }
    resetDefinitionEditor(state.definitionDraft.isNull() ? m_definition : state.definitionDraft);
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
        m_draft = captureInput();
        modeDraft() = m_draft;
    }
    else m_recalledInputs.insert(records.at(m_historyPosition).id, captureInput());
    m_historyPosition = position;
    const auto &entry = records.at(position);
    const QString text = entry.customDefinition.isEmpty() ? entry.expression : entry.parameterInput;
    const InputState initial{text, text.size(), text.size(), entry.customDefinition, {},
                             entry.programmer, entry.programmerConfig};
    restoreInput(m_recalledInputs.value(entry.id, initial));
    if (position < m_recordStart || position >= m_recordEnd)
        showRecordPage(records.size() - ((records.size() - 1 - position) / RecordPageSize) * RecordPageSize);
    showStatus(QString());
    // 公式和程序员设置均未修改时才使用历史诊断；临时修改须重新提交才能定位。
    if (!customMode() && !entry.result.ok && m_input->toPlainText() == entry.expression
        && (!entry.programmer || programmerConfig() == entry.programmerConfig))
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
    if (dialogOpen()) return;
    if (m_history.count() == 0 || (!older && m_historyPosition < 0)) return;
    const int position = m_historyPosition < 0 ? m_history.count() - 1
                                               : m_historyPosition + (older ? -1 : 1);
    if (position < 0) return; // 最旧记录不循环跳到最新，也不覆盖当前编辑。
    if (position == m_history.count())
    {
        restoreDraft();
        showStatus(QStringLiteral("已返回草稿"), true);
    }
    else showHistory(position);
}

void CalculatorPage::reuseFormula(quint64 id)
{
    if (busy()) return;
    if (m_history.record(id)) showHistory(int(id - 1));
}

void CalculatorPage::copyRecord(quint64 id, bool valueOnly, int radix)
{
    if (m_exportDialog || m_sessionDialog) return;
    const CalculationRecord *entry = m_history.record(id);
    if (!entry || (valueOnly && !entry->result.ok)) return;
    QString text = valueOnly ? entry->valueText() : entry->calculationText();
    if (valueOnly && entry->programmer)
    {
        if (radix == 16) text = entry->programmerValue.hexText();
        else if (radix == 2) text = QStringLiteral("0b") + entry->programmerValue.binaryText().remove(QLatin1Char(' '));
        else if (radix == 8) text = entry->programmerValue.octalText();
    }
    QApplication::clipboard()->setText(text);
    showStatus(!valueOnly ? QStringLiteral("已复制整条计算及说明")
        : radix != 10 ? QStringLiteral("已复制原记录的位模式；重新输入按目标位宽与符号解释，不保留回绕来源")
        : !entry->sourceText().isEmpty() ? QStringLiteral("已复制数值文本；重新输入按新数字处理，不保留原计算来源")
                                       : QStringLiteral("已复制完整十进制数值"), true);
    if (m_status->parentWidget() && m_status->parentWidget()->layout()) m_status->parentWidget()->layout()->activate();
}

void CalculatorPage::insertResult(quint64 id)
{
    if (dialogOpen()) return;
    if (m_defining)
    {
        showStatus(QStringLiteral("请先应用或取消首次公式定义，再插入结果数值"));
        focusInput();
        return;
    }
    const CalculationRecord *entry = m_history.record(id);
    if (!entry || !entry->result.ok) return;
    if (m_composing)
    {
        showStatus(QStringLiteral("请先完成输入法组词，再插入结果数值"));
        return;
    }
    QString value = entry->valueText();
    if (programmerMode())
    {
        value = entry->programmer ? entry->programmerValue.text() : entry->result.value.integerText();
        if (value.isEmpty())
        {
            showStatus(QStringLiteral("无法插入：保存的结果不是整数，当前输入保持不变"));
            return;
        }
        const auto config = programmerConfig();
        // 无符号表达式的 -1 会回绕；插入要求保持数值，必须先拒绝负数。
        if ((!config.isSigned && value.startsWith(QLatin1Char('-')))
            || !ProgrammerEngine::evaluate(value, config).ok)
        {
            showStatus(QStringLiteral("无法插入：结果超出当前 %1 位%2数值范围，当前输入保持不变")
                .arg(config.bitWidth).arg(config.isSigned ? QStringLiteral("有符号") : QStringLiteral("无符号")));
            return;
        }
    }
    QTextCursor cursor = m_input->textCursor();
    cursor.beginEditBlock();
    cursor.insertText(customMode() ? value : entry->insertionText(programmerMode()));
    cursor.endEditBlock();
    m_input->setTextCursor(cursor);
    m_editTarget = m_input;
    focusInput();
    // QAction 在 QMenu 完全关闭前触发；待菜单完成自身的焦点恢复后再聚焦输入区。
    QTimer::singleShot(0, this, [this]() { refocusPage(this); });
    showStatus(!entry->sourceText().isEmpty()
        ? QStringLiteral("已插入数值文本；再次计算按新数字处理，不保留原计算来源")
        : QStringLiteral("已插入完整十进制数值，可继续编辑后计算"), true);
}

void CalculatorPage::routeEdit(const QString &command)
{
    if (dialogOpen()) return;
    const auto edit = [&command](auto *editor) {
        if (command == QStringLiteral("actioncopy")) editor->copy();
        else if (command == QStringLiteral("actioncut")) editor->cut();
        else if (command == QStringLiteral("actionpaste")) editor->paste();
        else if (command == QStringLiteral("actionundo")) editor->undo();
        else if (command == QStringLiteral("actionredo")) editor->redo();
        else if (command == QStringLiteral("actionselect_All")) editor->selectAll();
    };
    // Host menu proxies remain active while a modeless help window is focused.
    QWidget *focused = QApplication::focusWidget();
    if (focused && (focused->window()->objectName() == QStringLiteral("calctabddHelpDialog") ||
                    focused->window()->objectName() == QStringLiteral("calctabddAboutDialog")))
    {
        if (auto *line = qobject_cast<QLineEdit *>(focused))
            edit(line);
        else if (auto *text = qobject_cast<QTextEdit *>(focused))
        {
            if (command == QStringLiteral("actioncopy")) text->copy();
            else if (command == QStringLiteral("actionselect_All")) text->selectAll();
        }
        return;
    }
    if (command == QStringLiteral("actionpaste") && (qobject_cast<RecordText *>(m_editTarget.data())
        || qobject_cast<QLabel *>(m_editTarget.data())))
    {
        // 先选定写入目标，再转移焦点；待确认定义期间不能改动隐藏草稿或参数。
        auto *target = hasPendingDefinition() ? m_definitionInput : m_input;
        target->setFocus(Qt::OtherFocusReason);
        m_editTarget = target;
        target->paste();
        return;
    }
    if (auto *result = qobject_cast<RecordText *>(m_editTarget.data()))
    {
        if (command == QStringLiteral("actioncopy")) result->copy();
        else if (command == QStringLiteral("actionselect_All")) result->selectAll();
        return;
    }
    auto *selectedLabel = qobject_cast<QLabel *>(m_editTarget.data());
    if (selectedLabel)
    {
        if (command == QStringLiteral("actioncopy") && selectedLabel->hasSelectedText())
            QApplication::clipboard()->setText(selectedLabel->selectedText());
        else if (command == QStringLiteral("actionselect_All"))
            selectedLabel->setSelection(0, selectedLabel->text().size());
        return;
    }
    edit(m_editTarget == m_definitionInput ? m_definitionInput : m_input);
}

bool CalculatorPage::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_input || object == m_definitionInput)
    {
        if (event->type() == QEvent::Resize)
            QTimer::singleShot(0, this, &CalculatorPage::updateInputHeights);
        if (event->type() == QEvent::InputMethod)
        {
            m_composing = !static_cast<QInputMethodEvent *>(event)->preeditString().isEmpty();
            if (m_composing) clearInputError();
            updateRecordUi();
        }
        if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
        {
            auto *key = static_cast<QKeyEvent *>(event);
            const Qt::KeyboardModifiers modifiers = key->modifiers() & ~Qt::KeypadModifier;
            if (object == m_definitionInput)
            {
                const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
                const bool cancel = key->key() == Qt::Key_Escape && modifiers == Qt::NoModifier;
                if (enter || cancel)
                {
                    if (m_composing) return QWidget::eventFilter(object, event);
                    key->accept();
                    if (event->type() == QEvent::KeyPress)
                    {
                        if (cancel)
                        {
                            m_defining = false;
                            resetDefinitionEditor(m_definition);
                            updateModeUi();
                            focusInput();
                            showStatus(QStringLiteral("已还原公式"), true);
                        }
                        else if (modifiers == Qt::NoModifier || modifiers == Qt::ControlModifier) applyDefinition();
                    }
                    return true;
                }
                return QWidget::eventFilter(object, event);
            }
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
    if (m_sessionDirty) saveSession();
    QWidget::hideEvent(event);
}

void CalculatorPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!m_input || !m_definitionInput) return;
    if (event->type() == QEvent::FontChange)
    {
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSizeF(qMax(11.0, font().pointSizeF() + 1));
        if (m_input) m_input->setFont(mono);
        if (m_definitionInput) m_definitionInput->setFont(mono);
        if (auto *title = findChild<QLabel *>(QStringLiteral("calculatorTitle")))
        {
            QFont titleFont = font();
            titleFont.setPointSizeF(qMax(12.0, titleFont.pointSizeF() + 2));
            titleFont.setBold(true);
            title->setFont(titleFont);
        }
        updateModeUi();
        applyTheme();
    }
    else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
    {
        applyTheme();
    }
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
    CalculatorStyle::applyDialog(m_clearConfirmation, colors);
}

CalculatorPage::~CalculatorPage()
{
    disconnect(qApp, &QApplication::focusChanged, this, nullptr);
    // QWidget 的子控件此时仍在；即使没有先收到 hideEvent 也提交最后的已上屏草稿。
    if (m_sessionDirty) saveSession();
}

CalculationSession CalculatorPage::captureSession() const
{
    CalculationSession session;
    session.history = m_history;
    session.input = captureInput();
    session.normalInput = m_normalInput;
    session.customInput = m_customInput;
    session.programmerInput = m_programmerInput;
    if (m_historyPosition < 0)
        (session.input.programmer ? session.programmerInput
            : !session.input.customDefinition.isEmpty() ? session.customInput : session.normalInput) = session.input;
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
    clearSession();
    m_normalInput = session.normalInput;
    m_customInput = session.customInput;
    m_programmerInput = session.programmerInput;
    m_history = session.history;
    showRecordPage(m_history.count());
    m_historyPosition = session.historyPosition;
    m_draft = session.draft;
    m_recalledInputs = session.recalledInputs;
    restoreInput(session.input);
    const CalculationRecord *entry = m_historyPosition >= 0
        ? &m_history.records().at(m_historyPosition)
        : (m_history.count() > 0 ? &m_history.records().last() : nullptr);
    if (!customMode() && entry && !entry->result.ok && session.input.text.trimmed() == entry->expression
        && entry->programmer == programmerMode()
        && (!entry->programmer || entry->programmerConfig == programmerConfig()))
        showInputError(entry->result, skippedWhitespace(session.input.text), false);
    else showStatus(QStringLiteral("已恢复会话"), true);
}

void CalculatorPage::scheduleSave()
{
    if (!m_sessionFile) return;
    m_sessionDirty = true;
    m_saveTimer->start();
}

void CalculatorPage::updateSessionStatus(const QString &error)
{
    m_saveNowAction->setEnabled(bool(m_sessionFile));
    m_stopSavingAction->setEnabled(bool(m_sessionFile));
    m_sessionStatus->setProperty("error", !error.isEmpty());
    if (!error.isEmpty())
    {
        m_sessionStatus->setText(QStringLiteral("%1\n当前记录与草稿仍保留。请通过“本地会话”重试或另存新文件，成功保存前不要关闭标签。")
            .arg(error));
    }
    else if (m_sessionFile)
        m_sessionStatus->setText(QStringLiteral("本地保存已开启 · %1\n关闭后可在空白计算器中选择“恢复会话”。").arg(m_sessionFile->path()));
    else m_sessionStatus->setText(QStringLiteral("本地保存已停止，已保存文件保留；后续更改仅在当前标签有效。"));
    repolishWidget(m_sessionStatus);
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
    if (busy() || !m_sessionFile) return;
    // 失败时继续持有文件与可见会话，用户可以重试或另存。
    if (!saveSession()) return;
    m_sessionFile.reset();
    updateSessionStatus();
}

void CalculatorPage::requestSessionFile(bool restore)
{
    if (busy()) return;
    if (restore && (m_history.count() > 0 || !m_input->toPlainText().isEmpty() || m_sessionFile
        || !m_customInput.customDefinition.isEmpty() || customMode() || hasPendingDefinition() || !m_normalInput.text.isEmpty()
        || m_programmerMode || !m_programmerInput.text.isEmpty() || m_programmerInput.programmerConfig != ProgrammerConfig()))
    {
        updateSessionStatus(QStringLiteral("请在没有记录、没有草稿且未开启保存的空白计算器中恢复会话。"));
        return;
    }
    auto *dialog = createFileDialog(this, QStringLiteral("sessionFileDialog"),
        restore ? QStringLiteral("恢复本地会话") : QStringLiteral("选择新文件并开启自动保存"),
        restore ? QFileDialog::AcceptOpen : QFileDialog::AcceptSave,
        restore ? QStringLiteral("恢复") : QStringLiteral("开启保存"),
        QStringLiteral("CalcTabdd 会话 (*.calctabdd)"), QStringLiteral("calctabdd"));
    m_sessionDialog = dialog;
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
        refocusPage(this);
    });
    dialog->open();
    CalculatorStyle::applyDialog(dialog, palette());
}

void CalculatorPage::updateDefinitionStateUi()
{
    if (!m_definitionStateBadge) return;
    const bool pending = hasPendingDefinition();
    m_definitionStateBadge->setVisible(pending && (customMode() || m_defining));
    m_definitionStateBadge->setProperty("pending", pending);
    repolishWidget(m_definitionStateBadge);
}

void CalculatorPage::updateInputHeights()
{
    if (!m_input || !m_definitionInput) return;
    const auto fitPlaceholder = [](QPlainTextEdit *editor, int height) {
        if (editor->toPlainText().isEmpty() && editor->viewport()->width() > 0)
        {
            QTextDocument placeholder;
            placeholder.setDefaultFont(editor->font());
            placeholder.setDocumentMargin(editor->document()->documentMargin());
            placeholder.setPlainText(editor->placeholderText());
            placeholder.setTextWidth(editor->viewport()->width());
            // 包含样式表边框与内边距；空白提示换行时仍完整可见。
            height = qMax(height, qCeil(placeholder.size().height())
                + editor->height() - editor->viewport()->height());
        }
        if (editor->height() != height || editor->minimumHeight() != height)
            editor->setFixedHeight(height);
    };
    fitPlaceholder(m_definitionInput, QFontMetrics(m_definitionInput->font()).lineSpacing() + 26);
    CustomFormula formula;
    const bool custom = customMode() || m_defining;
    const bool validFormula = custom && CustomFormula::parse(m_definition, formula).isEmpty();
    const int rows = validFormula ? qBound(3, formula.parameters().size(), 6) : 0;
    fitPlaceholder(m_input, custom ? rows * QFontMetrics(m_input->font()).lineSpacing() + 28
        : qMax(76, 2 * QFontMetrics(m_input->font()).lineSpacing() + 28));
}

void CalculatorPage::updateModeUi()
{
    const bool custom = customMode() || m_defining;
    const bool programmer = programmerMode();
    m_normalModeAction->setChecked(!custom && !programmer);
    m_customModeAction->setChecked(custom);
    m_normalModeButton->setChecked(!custom && !programmer);
    m_customModeButton->setChecked(custom);
    m_programmerModeAction->setChecked(programmer);
    m_programmerModeButton->setChecked(programmer);
    m_programmerPanel->setVisible(programmer);
    m_input->setAccessibleName(m_defining ? QStringLiteral("定义公式")
        : custom ? QStringLiteral("填写参数") : programmer ? QStringLiteral("输入整数表达式") : QStringLiteral("输入公式"));
    m_input->setPlaceholderText(custom ? QStringLiteral("每行一个参数，例如 x=3\nAlt+↑↓ 召回历史")
        : programmer ? QStringLiteral("例如 (0x12 & 0x0F) << 4\n@ 补全 · Alt+↑↓ 召回历史")
                     : QStringLiteral("例如 (128 + 256) / 3\n@ 补全 · Alt+↑↓ 召回历史"));
    m_definitionPanel->setVisible(custom);
    m_definitionInput->setVisible(custom);
    m_definitionError->setVisible(custom && !m_definitionError->text().isEmpty());
    const bool showParams = !m_defining || customMode();
    m_parameterHeader->setVisible(custom && showParams);
    m_input->setVisible(showParams);
    m_definitionCompletion->setEnabled(custom);
    m_completion->setEnabled(!custom);
    m_completion->setProgrammerMode(programmer);
    updateInputHeights();
    updateDefinitionStateUi();
    updateRecordUi();
}

CalculatorPage::InputState &CalculatorPage::modeDraft()
{
    return m_programmerMode ? m_programmerInput : customMode() ? m_customInput : m_normalInput;
}

ProgrammerConfig CalculatorPage::programmerConfig() const
{
    return {m_bitWidth->currentData().toInt(), m_integerType->currentData().toBool()};
}

void CalculatorPage::switchMode(Mode mode)
{
    if (busy()) return;
    const bool custom = mode == Mode::Custom;
    const bool programmer = mode == Mode::Programmer;
    if (!custom) m_defining = false;
    if (custom == customMode() && programmer == programmerMode() && m_historyPosition < 0)
    {
        updateModeUi();
        focusInput();
        return;
    }
    // 手动切换先结束历史浏览；首次定义和 Esc 返回也使用原草稿，不保留历史临时编辑。
    if (m_historyPosition >= 0) restoreDraft();
    modeDraft() = captureInput();
    if (custom && m_customInput.customDefinition.isEmpty() && !customMode())
    {
        m_defining = true;
        updateModeUi();
        m_definitionInput->setFocus(Qt::OtherFocusReason);
        m_editTarget = m_definitionInput;
        showStatus(QString());
        return;
    }
    if (programmer) m_programmerInput.programmer = true;
    restoreInput(programmer ? m_programmerInput : custom ? m_customInput : m_normalInput);
    showStatus(QString());
    scheduleSave();
}

bool CalculatorPage::hasPendingDefinition() const
{
    return m_defining || (customMode() && m_definitionInput->toPlainText() != m_definition);
}

void CalculatorPage::resetDefinitionEditor(const QString &text)
{
    const QSignalBlocker blocker(m_definitionInput);
    m_definitionInput->setPlainText(text);
    m_definitionError->clear();
    m_definitionError->hide();
    updateDefinitionStateUi();
}

void CalculatorPage::applyDefinition()
{
    if (busy()) return;
    const QString definition = m_definitionInput->toPlainText();
    CustomFormula formula;
    const QString problem = definition.contains(QLatin1Char('@'))
        ? QStringLiteral("请先完成 @ 函数或常量补全，或删除 @ 查询")
        : CustomFormula::parse(definition, formula);
    if (!problem.isEmpty())
    {
        m_definitionError->setText(problem);
        m_definitionError->show();
        m_definitionInput->setFocus(Qt::OtherFocusReason);
        return;
    }
    // 仅确认后替换定义；同名参数保留原文，无效编辑不能触发旧公式计算。
    const InputState previous = customMode() ? captureInput() : m_customInput;
    if (m_historyPosition >= 0) restoreDraft();
    modeDraft() = captureInput();
    const QString input = formula.parameterTemplate(previous.text);
    const int position = input.isEmpty() ? 0 : input.indexOf(QLatin1Char('=')) + 1;
    m_customInput = {input, position, position, formula.definition()};
    restoreInput(m_customInput);
    showStatus(QStringLiteral("公式已应用"), true);
    scheduleSave();
}
