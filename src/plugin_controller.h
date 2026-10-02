#pragma once

#include "ndd_plugin_api.h"
#include <QObject>
#include <QPointer>
#include <QVector>

class CalculatorPage;
class QAbstractScrollArea;
class QTabWidget;

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
    void syncActivePage();
    void installRoutes();
    void restoreRoutes();
    void showStatus(const QString &message);
    QPointer<QWidget> m_host;
    QPointer<QTabWidget> m_tabs;
    QPointer<QAbstractScrollArea> m_editor;
    QPointer<CalculatorPage> m_page;
    QPointer<QAction> m_openAction;
    QPointer<QAction> m_helpAction;
    QPointer<QAction> m_aboutAction;
    NddHostCallback m_callback;
    QVector<ActionRoute> m_routes;
    bool m_routing = false;
};
