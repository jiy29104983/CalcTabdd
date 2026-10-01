#include "plugin_controller.h"
#include "calculator_page.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMainWindow>
#include <QPlainTextEdit>
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
        auto *help = new QAction(QStringLiteral("运算说明"), this);
        help->setObjectName(QStringLiteral("calctabddHelp"));
        connect(help, &QAction::triggered, this, [this]() {
            showStatus(QStringLiteral("支持 + - * / % ^、括号、科学计数法、pi / e / ans；sqrt、abs、sin/cos/tan（弧度）、ln/log、exp、floor/ceil/round、min/max/pow。"));
        });
        menu->addAction(m_openAction);
        menu->addAction(help);
    }
    else if (!menu->actions().contains(m_openAction)) menu->addAction(m_openAction);
    return true;
}

void PluginController::showStatus(const QString &message)
{
    if (!m_host) return;
    if (auto *status = m_host->findChild<QStatusBar *>()) status->showMessage(message, 12000);
    else m_host->setProperty("calctabddStatus", message);
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
    m_tabs->setTabToolTip(index, QStringLiteral("CalcTabdd · 关闭标签后清空计算记录"));
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
