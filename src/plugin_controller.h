#pragma once

#include "ndd_plugin_api.h"
#include <QObject>
#include <QPointer>
#include <QVector>

class CalculatorPage;
class QAbstractScrollArea;
class QTabWidget;
class QTabBar;
class QToolBar;

class PluginController : public QObject
{
    Q_OBJECT
public:
    PluginController(QWidget *host, NddHostCallback callback);
    ~PluginController() override;
    bool installMenu(QMenu *menu);

public slots:
    void openCalculator();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    struct ActionRoute
    {
        QPointer<QAction> original;
        QPointer<QAction> proxy;
        QVector<QPointer<QWidget>> widgets;
        bool enabled = false;
    };
    struct ToolbarRoute
    {
        QPointer<QToolBar> toolbar;
        QPointer<QAction> original;
        QPointer<QAction> proxy;
    };
    void installToolbarRoutes();
    void showTabMenu(const QPoint &position);
    void syncActivePage();
    void installRoutes();
    void restoreRoutes();
    void showStatus(const QString &message);
    QPointer<QWidget> m_host;
    QPointer<QTabWidget> m_tabs;
    QPointer<QTabBar> m_tabBar;
    QPointer<QMenu> m_tabMenu;
    QPointer<QAbstractScrollArea> m_editor;
    QPointer<CalculatorPage> m_page;
    QPointer<QAction> m_openAction;
    QPointer<QAction> m_helpAction;
    QPointer<QAction> m_aboutAction;
    NddHostCallback m_callback;
    QVector<ActionRoute> m_routes;
    QVector<ToolbarRoute> m_toolbarRoutes;
    bool m_routing = false;
};
