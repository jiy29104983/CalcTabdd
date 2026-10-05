#include "plugin_controller.h"
#include "calculator_page.h"
#include "calculator_help.h"
#include "document_selection.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVariant>

PluginController::PluginController(QWidget *host, NddHostCallback callback)
    : QObject(host), m_host(host), m_callback(std::move(callback))
{
    setObjectName(QStringLiteral("calctabddController"));
    m_tabs = host->findChild<QTabWidget *>(QStringLiteral("editTabWidget"));
    if (m_tabs)
        connect(m_tabs, &QTabWidget::currentChanged, this, [this]() {
            if (m_selectionDialog) m_selectionDialog->reject();
            syncActivePage();
            QTimer::singleShot(0, this, &PluginController::syncActivePage);
        });
}

PluginController::~PluginController()
{
    restoreRoutes();
    if (m_editor)
    {
        m_editor->removeEventFilter(this);
        m_editor->viewport()->removeEventFilter(this);
    }
}

bool PluginController::installMenu(QMenu *menu)
{
    if (!menu || !m_tabs || !m_callback) return false;
    if (!m_openAction)
    {
        m_openAction = new QAction(QStringLiteral("打开计算器"), this);
        m_openAction->setObjectName(QStringLiteral("calctabddOpen"));
        connect(m_openAction, &QAction::triggered, this, &PluginController::openCalculator);
        m_selectionAction = new QAction(QStringLiteral("查看文档选区（只读）"), this);
        m_selectionAction->setObjectName(QStringLiteral("calctabddInspectSelection"));
        connect(m_selectionAction, &QAction::triggered, this, &PluginController::inspectDocumentSelection);
        m_helpAction = new QAction(QStringLiteral("运算与精度帮助"), this);
        m_helpAction->setObjectName(QStringLiteral("calctabddHelp"));
        connect(m_helpAction, &QAction::triggered, this, [this]() { showCalculatorHelp(m_host); });
        m_aboutAction = new QAction(QStringLiteral("关于 CalcTabdd"), this);
        m_aboutAction->setObjectName(QStringLiteral("calctabddAbout"));
        connect(m_aboutAction, &QAction::triggered, this, [this]() { showCalculatorAbout(m_host); });
    }
    for (QAction *action : {m_openAction.data(), m_selectionAction.data(), m_helpAction.data(), m_aboutAction.data()})
        if (!menu->actions().contains(action)) menu->addAction(action);
    return true;
}

void PluginController::showStatus(const QString &message)
{
    if (!m_host) return;
    if (auto *status = m_host->findChild<QStatusBar *>()) status->showMessage(message, 12000);
    else m_host->setProperty("calctabddStatus", message);
}

void PluginController::inspectDocumentSelection()
{
    if (!m_host || !m_tabs) return;
    if (m_selectionDialog) m_selectionDialog->reject();
    const DocumentSelection selection = readDocumentSelection(m_tabs);
    if (selection.status == DocumentSelection::Status::Unsupported)
    {
        showStatus(QStringLiteral("请切换到普通文本文档后查看选区；计算器、二进制及分页大文件标签不支持。"));
        return;
    }
    if (selection.status == DocumentSelection::Status::Empty)
    {
        showStatus(QStringLiteral("当前文档没有选中文字。"));
        return;
    }

    auto *dialog = new QDialog(m_host);
    m_selectionDialog = dialog;
    dialog->setAttribute(Qt::WA_WindowPropagation);
    dialog->setPalette(m_host->palette());
    dialog->setObjectName(QStringLiteral("calctabddSelectionDialog"));
    dialog->setWindowTitle(QStringLiteral("查看文档选区（只读）"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    const QSize available = m_host->screen() ? m_host->screen()->availableGeometry().size() : QSize(1024, 768);
    dialog->resize(qMin(620, available.width() - 40), qMin(380, available.height() - 60));
    auto *layout = new QVBoxLayout(dialog);
    auto *source = new QLabel(QStringLiteral("来源：%1").arg(selection.sourceName), dialog);
    source->setObjectName(QStringLiteral("selectionSource"));
    source->setTextFormat(Qt::PlainText);
    source->setWordWrap(true);
    layout->addWidget(source);
    const int crlf = selection.text.count(QStringLiteral("\r\n"));
    auto *details = new QLabel(QStringLiteral("UTF-16 长度：%1；换行 CRLF：%2，LF：%3，CR：%4")
        .arg(selection.text.size()).arg(crlf)
        .arg(selection.text.count(QLatin1Char('\n')) - crlf)
        .arg(selection.text.count(QLatin1Char('\r')) - crlf), dialog);
    details->setObjectName(QStringLiteral("selectionDetails"));
    details->setWordWrap(true);
    layout->addWidget(details);
    auto *preview = new QPlainTextEdit(dialog);
    preview->setObjectName(QStringLiteral("selectionPreview"));
    preview->setReadOnly(true);
    preview->setPlainText(selection.text);
    layout->addWidget(preview);
    auto *note = new QLabel(QStringLiteral("这是打开时的选区快照，仅供核对，未计算或自动复制。\n列选区／多选区按宿主规则合并，NUL 显示为空格；源文档保持不变。"), dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);
    connect(selection.editor, &QObject::destroyed, dialog, &QDialog::reject);
    dialog->show();
}

void PluginController::openCalculator()
{
    if (!m_host || !m_tabs) return;
    if (m_editor && m_page && m_tabs->indexOf(m_editor) >= 0)
    {
        m_tabs->setCurrentWidget(m_editor);
        m_page->focusInput();
        return;
    }
    QSet<QWidget *> existing;
    for (int index = 0; index < m_tabs->count(); ++index) existing.insert(m_tabs->widget(index));
    QVariant createdName;
    if (!m_callback(m_host, 1, &createdName))
    {
        showStatus(QStringLiteral("无法创建计算器：宿主没有完成新建标签。"));
        return;
    }
    QWidget *candidate = m_tabs->currentWidget();
    // Resolve through the actual tab widget, avoiding casts across the QScintilla ABI.
    auto *editor = qobject_cast<QAbstractScrollArea *>(candidate);
    if (!editor || existing.contains(candidate) || !candidate->inherits("QsciScintilla") ||
        editor->metaObject()->indexOfSlot("setReadOnly(bool)") < 0)
    {
        showStatus(QStringLiteral("无法接入计算器：宿主返回的标签不符合兼容接口。"));
        return;
    }
    if (!QMetaObject::invokeMethod(editor, "setReadOnly", Qt::DirectConnection, Q_ARG(bool, true)))
    {
        showStatus(QStringLiteral("无法保护计算器的底层文档。"));
        return;
    }
    m_editor = editor;
    m_page = new CalculatorPage(editor);
    m_page->setGeometry(editor->rect());
    editor->installEventFilter(this);
    editor->viewport()->installEventFilter(this);
    editor->setProperty("calctabddNativeTab", true);
    const int index = m_tabs->indexOf(editor);
    m_tabs->setTabText(index, QStringLiteral("计算器"));
    m_tabs->setTabToolTip(index, QStringLiteral("CalcTabdd · 可在“本地会话”中开启保存或恢复历史"));
    connect(editor, &QObject::destroyed, this, [this, editor]() {
        if (m_editor && m_editor.data() != editor) return;
        m_page.clear();
        m_editor.clear();
        restoreRoutes();
    });
    m_page->show();
    m_page->raise();
    m_page->focusInput();
    syncActivePage();
}

void PluginController::syncActivePage()
{
    if (!m_tabs || !m_page || !m_editor || m_tabs->currentWidget() != m_editor)
    {
        restoreRoutes();
        return;
    }
    m_page->setGeometry(m_editor->rect());
    m_page->raise();
    installRoutes();
}

void PluginController::installRoutes()
{
    if (!m_routes.isEmpty() || !m_host || !m_page) return;
    const QSet<QString> editCommands = {
        QStringLiteral("actioncopy"), QStringLiteral("actioncut"), QStringLiteral("actionpaste"),
        QStringLiteral("actionundo"), QStringLiteral("actionredo"), QStringLiteral("actionselect_All")};
    const QSet<QString> blockedCommands = {
        QStringLiteral("actionSave"), QStringLiteral("actionSave_as"), QStringLiteral("actionSave_All"),
        QStringLiteral("actionSaveAll"), QStringLiteral("actionFind"), QStringLiteral("actionFindNext"),
        QStringLiteral("actionFindPrev"), QStringLiteral("actionReplace"), QStringLiteral("actionGoline"),
        QStringLiteral("actionOpen_In_Text"), QStringLiteral("actionOpen_In_Bin")};
    const auto actions = m_host->findChildren<QAction *>();
    m_routing = true;
    for (QAction *original : actions)
    {
        const QString name = original->objectName();
        if (!editCommands.contains(name) && !blockedCommands.contains(name)) continue;
        ActionRoute route;
        route.original = original;
        route.enabled = original->isEnabled();
        route.proxy = new QAction(original->icon(), original->text(), this);
        route.proxy->setObjectName(QStringLiteral("calctabddRoute_") + name);
        route.proxy->setShortcuts(original->shortcuts());
        route.proxy->setShortcutContext(original->shortcutContext());
        route.proxy->setEnabled(editCommands.contains(name));
        route.proxy->setToolTip(editCommands.contains(name) ? original->toolTip() : QStringLiteral("计算器标签不使用此文档命令"));
        connect(route.proxy, &QAction::triggered, this, [this, name]() { if (m_page) m_page->routeEdit(name); });
        for (QWidget *widget : original->associatedWidgets())
        {
            route.widgets.append(widget);
            widget->insertAction(original, route.proxy);
            widget->removeAction(original);
        }
        original->setEnabled(false);
        m_routes.append(route);
        connect(original, &QAction::changed, this, [this, original]() {
            if (m_routing) return;
            for (ActionRoute &entry : m_routes)
            {
                if (entry.original == original && original->isEnabled())
                {
                    entry.enabled = true;
                    m_routing = true;
                    original->setEnabled(false);
                    m_routing = false;
                    break;
                }
            }
        });
    }
    m_routing = false;
}

void PluginController::restoreRoutes()
{
    if (m_routes.isEmpty()) return;
    m_routing = true;
    for (const ActionRoute &route : m_routes)
    {
        if (route.original)
        {
            disconnect(route.original, &QAction::changed, this, nullptr);
            for (const QPointer<QWidget> &widget : route.widgets)
            {
                if (!widget) continue;
                widget->insertAction(route.proxy, route.original);
                widget->removeAction(route.proxy);
            }
            route.original->setEnabled(route.enabled);
        }
        if (route.proxy) route.proxy->deleteLater();
    }
    m_routes.clear();
    m_routing = false;
}

bool PluginController::eventFilter(QObject *object, QEvent *event)
{
    if (!m_editor || !m_page) return QObject::eventFilter(object, event);
    if (object == m_editor && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
    {
        m_page->setGeometry(m_editor->rect());
        m_page->raise();
    }
    if (object == m_editor || object == m_editor->viewport())
    {
        if (event->type() == QEvent::FocusIn && m_page->isVisible())
            QTimer::singleShot(0, m_page, [page = QPointer<CalculatorPage>(m_page)]() { if (page && page->isVisible()) page->focusInput(); });
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease || event->type() == QEvent::InputMethod)
        {
            event->accept();
            return true;
        }
        if (event->type() == QEvent::ContextMenu || event->type() == QEvent::Drop)
        {
            event->accept();
            return true;
        }
    }
    return QObject::eventFilter(object, event);
}
