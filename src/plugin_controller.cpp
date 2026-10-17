#include "plugin_controller.h"
#include "calculator_page.h"
#include "calculator_help.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>
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
    {
        m_tabBar = m_tabs->tabBar();
        m_tabBar->installEventFilter(this);
        connect(m_tabs, &QTabWidget::currentChanged, this, [this]() {
            syncActivePage();
            QTimer::singleShot(0, this, &PluginController::syncActivePage);
        });
    }
}

PluginController::~PluginController()
{
    restoreRoutes();
    if (m_tabBar) m_tabBar->removeEventFilter(this);
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
        m_helpAction = new QAction(QStringLiteral("运算与精度帮助"), this);
        m_helpAction->setObjectName(QStringLiteral("calctabddHelp"));
        connect(m_helpAction, &QAction::triggered, this, [this]() { showCalculatorHelp(m_host); });
        m_aboutAction = new QAction(QStringLiteral("关于 CalcTabdd"), this);
        m_aboutAction->setObjectName(QStringLiteral("calctabddAbout"));
        connect(m_aboutAction, &QAction::triggered, this, [this]() { showCalculatorAbout(m_host); });
    }
    for (QAction *action : {m_openAction.data(), m_helpAction.data(), m_aboutAction.data()})
        if (!menu->actions().contains(action)) menu->addAction(action);
    return true;
}

void PluginController::showStatus(const QString &message)
{
    if (!m_host) return;
    if (auto *status = m_host->findChild<QStatusBar *>()) status->showMessage(message, 12000);
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
    if (m_editor) disconnect(m_editor, &QObject::destroyed, this, nullptr);
    m_editor = editor;
    m_page = new CalculatorPage(editor);
    m_page->setGeometry(editor->rect());
    editor->installEventFilter(this);
    editor->viewport()->installEventFilter(this);
    const int index = m_tabs->indexOf(editor);
    m_tabs->setTabText(index, QStringLiteral("计算器"));
    m_tabs->setTabToolTip(index, QStringLiteral("CalcTabdd · 可在“本地会话”中开启保存或恢复历史"));
    connect(editor, &QObject::destroyed, this, [this]() {
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
        if (m_tabMenu) m_tabMenu->hide();
        restoreRoutes();
        return;
    }
    m_page->setGeometry(m_editor->rect());
    m_page->raise();
    installRoutes();
}

void PluginController::installRoutes()
{
    if (!m_routes.isEmpty() || !m_toolbarRoutes.isEmpty() || !m_host || !m_page) return;
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
    installToolbarRoutes();
}

void PluginController::installToolbarRoutes()
{
    // 固定宿主的主工具栏使用 addWidget + clicked 直连槽函数，没有 action 名称。
    // 只识别该工具栏内的已知按钮，支持宿主加载的翻译；不改动原有信号连接。
    auto *toolbar = m_host->findChild<QToolBar *>(QStringLiteral("mainToolBar"));
    if (!toolbar) return;
    struct Command { const char *tooltip; const char *name; bool editable; };
    static const Command commands[] = {
        {"Cut", "actioncut", true}, {"Copy", "actioncopy", true},
        {"Paste", "actionpaste", true}, {"Undo", "actionundo", true}, {"Redo", "actionredo", true},
        {"Save File", "actionSave", false}, {"Save All File", "actionSaveAll", false},
        {"Cycle Auto Save", "actionAutoSave", false}, {"Find", "actionFind", false},
        {"Replace", "actionReplace", false}, {"Mark", "actionMark", false}
    };
    const auto actions = toolbar->actions();
    for (QAction *action : actions)
    {
        auto *widgetAction = qobject_cast<QWidgetAction *>(action);
        auto *button = widgetAction ? qobject_cast<QToolButton *>(widgetAction->defaultWidget()) : nullptr;
        if (!button || button->defaultAction()) continue;
        for (const auto &command : commands)
        {
            if (button->toolTip() != QLatin1String(command.tooltip)
                && button->toolTip() != QCoreApplication::translate("CCNotePad", command.tooltip)) continue;
            auto *proxy = new QAction(button->icon(), button->text(), this);
            const QString name = QString::fromLatin1(command.name);
            proxy->setObjectName(QStringLiteral("calctabddToolbar_") + name);
            proxy->setToolTip(command.editable ? button->toolTip() : QStringLiteral("计算器标签不使用此文档命令；保存请使用“本地会话”"));
            proxy->setEnabled(command.editable);
            connect(proxy, &QAction::triggered, this, [this, name]() { if (m_page) m_page->routeEdit(name); });
            toolbar->insertAction(action, proxy);
            if (auto *replacement = qobject_cast<QToolButton *>(toolbar->widgetForAction(proxy)))
            {
                replacement->setIconSize(button->iconSize());
                replacement->setMinimumSize(button->minimumSize());
                replacement->setMaximumSize(button->maximumSize());
                replacement->setToolButtonStyle(button->toolButtonStyle());
                replacement->setFocusPolicy(button->focusPolicy());
            }
            // QWidgetAction 保留默认按钮；隐藏期间宿主仍可更新它的状态，切回时原样恢复。
            toolbar->removeAction(action);
            m_toolbarRoutes.append({toolbar, action, proxy});
            break;
        }
    }
}

void PluginController::showTabMenu(const QPoint &position)
{
    if (!m_tabs || !m_editor || !m_page || !m_tabBar) return;
    if (!m_tabMenu)
    {
        m_tabMenu = new QMenu(m_page);
        m_tabMenu->setObjectName(QStringLiteral("calctabddTabMenu"));
        auto *hint = m_tabMenu->addAction(QStringLiteral("保存：请使用“本地会话”或“导出记录”"));
        hint->setEnabled(false);
        m_tabMenu->addSeparator();
        auto *close = m_tabMenu->addAction(QStringLiteral("关闭计算器"));
        close->setObjectName(QStringLiteral("calctabddCloseTab"));
        connect(close, &QAction::triggered, this, [this]() {
            if (!m_tabs || !m_editor || !m_tabBar) return;
            const int index = m_tabs->indexOf(m_editor);
            // 沿用宿主已连接的关闭信号，保留其文档登记与生命周期处理。
            if (index >= 0) emit m_tabBar->tabCloseRequested(index);
        });
    }
    m_tabs->setCurrentWidget(m_editor);
    m_tabMenu->popup(position);
}

void PluginController::restoreRoutes()
{
    if (m_routes.isEmpty() && m_toolbarRoutes.isEmpty()) return;
    m_routing = true;
    for (const auto &route : m_toolbarRoutes)
    {
        if (route.toolbar)
        {
            if (route.original) route.toolbar->insertAction(route.proxy, route.original);
            if (route.proxy) route.toolbar->removeAction(route.proxy);
        }
        if (route.proxy)
        {
            route.proxy->setObjectName(QString());
            route.proxy->deleteLater();
        }
    }
    m_toolbarRoutes.clear();
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
    if (object == m_tabBar && m_tabs && m_tabs->indexOf(m_editor) >= 0)
    {
        // 宿主在 tabBarClicked 中临时创建匿名“另存为”等文档命令。
        // 只替换计算器标签的右键入口，普通标签继续完整交给宿主。
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseButtonDblClick)
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::RightButton && m_tabBar->tabAt(mouse->pos()) == m_tabs->indexOf(m_editor))
            {
                if (event->type() == QEvent::MouseButtonPress) showTabMenu(mouse->globalPos());
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::ContextMenu)
        {
            auto *context = static_cast<QContextMenuEvent *>(event);
            const int index = context->reason() == QContextMenuEvent::Keyboard
                ? m_tabs->currentIndex() : m_tabBar->tabAt(context->pos());
            if (index == m_tabs->indexOf(m_editor))
            {
                if (!m_tabMenu || !m_tabMenu->isVisible()) showTabMenu(context->globalPos());
                event->accept();
                return true;
            }
        }
    }
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
