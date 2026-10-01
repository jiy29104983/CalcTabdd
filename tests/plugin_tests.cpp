#include "ndd_plugin_api.h"
#include <Qsci/qsciscintilla.h>
#include <plugin.h>
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QLibrary>
#include <QMainWindow>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QtTest>
#include <cstddef>
#include <type_traits>

static_assert(sizeof(NddProcData) == sizeof(NDD_PROC_DATA), "Host plugin ABI size mismatch");
#define ABI_FIELD(ours, host) static_assert(offsetof(NddProcData, ours) == offsetof(NDD_PROC_DATA, host), "ABI field mismatch")
ABI_FIELD(pluginName, m_strPlugName);
ABI_FIELD(filePath, m_strFilePath);
ABI_FIELD(comment, m_strComment);
ABI_FIELD(version, m_version);
ABI_FIELD(author, m_auther);
ABI_FIELD(menuType, m_menuType);
ABI_FIELD(rootMenu, m_rootMenu);
ABI_FIELD(action, m_pAction);

struct Host : QMainWindow
{
    QTabWidget *tabs;
    QMenu *plugins;
    QMenu *edit;
    QsciScintilla *ordinary;
    int hostEditCalls = 0;
    int creations = 0;
    bool failCreate = false;
    Host()
    {
        tabs = new QTabWidget(this);
        tabs->setObjectName(QStringLiteral("editTabWidget"));
        setCentralWidget(tabs);
        plugins = menuBar()->addMenu(QStringLiteral("CalcTabdd"));
        edit = menuBar()->addMenu(QStringLiteral("编辑"));
        for (const auto &name : {QStringLiteral("actioncopy"), QStringLiteral("actioncut"), QStringLiteral("actionpaste"), QStringLiteral("actionundo"), QStringLiteral("actionredo"), QStringLiteral("actionselect_All"), QStringLiteral("actionSave"), QStringLiteral("actionSave_as"), QStringLiteral("actionFind"), QStringLiteral("actionReplace")})
        {
            auto *action = edit->addAction(name);
            action->setObjectName(name);
            connect(action, &QAction::triggered, this, [this]() { ++hostEditCalls; });
        }
        findChild<QAction *>(QStringLiteral("actionSave"))->setShortcut(QKeySequence::Save);
        findChild<QAction *>(QStringLiteral("actioncopy"))->setShortcut(QKeySequence::Copy);
        ordinary = new QsciScintilla(tabs);
        ordinary->setText(QStringLiteral("普通文档，不得改动"));
        ordinary->setModified(false);
        tabs->addTab(ordinary, QStringLiteral("普通文档"));
        statusBar();
        resize(960, 740);
        show();
    }
    NddGetCurrentEditor getter()
    {
        return [this](QWidget *) { return qobject_cast<QsciScintilla *>(tabs->currentWidget()); };
    }
    NddHostCallback callback()
    {
        return [this](QWidget *window, int command, void *) {
            if (window != this || command != 1 || failCreate) return false;
            auto *editor = new QsciScintilla(tabs);
            editor->setModified(false);
            tabs->setCurrentIndex(tabs->addTab(editor, QStringLiteral("New %1").arg(++creations)));
            return true;
        };
    }
    QPlainTextEdit *input() { return findChild<QPlainTextEdit *>(QStringLiteral("formulaInput")); }
    QAction *openAction() { return findChild<QAction *>(QStringLiteral("calctabddOpen")); }
    QWidget *page() { return findChild<QWidget *>(QStringLiteral("calctabddPage")); }
    void closeCurrent()
    {
        auto *widget = tabs->currentWidget();
        tabs->removeTab(tabs->currentIndex());
        widget->deleteLater();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};

class PluginTests : public QObject
{
    Q_OBJECT
    using Identify = bool (*)(NddProcData *);
    using Main = int (*)(QWidget *, const QString &, NddGetCurrentEditor, NddHostCallback, NddProcData *);
    QLibrary m_library;
    Identify m_identify = nullptr;
    Main m_main = nullptr;
    int initialize(Host &host)
    {
        NddProcData data;
        if (!m_identify(&data)) return -99;
        data.rootMenu = host.plugins;
        return m_main(&host, QStringLiteral(CALCTABDD_PLUGIN_PATH), host.getter(), host.callback(), &data);
    }
private slots:
    void initTestCase()
    {
        m_library.setFileName(QStringLiteral(CALCTABDD_PLUGIN_PATH));
        m_library.setLoadHints(QLibrary::PreventUnloadHint);
        QVERIFY2(m_library.load(), qPrintable(m_library.errorString()));
        m_identify = reinterpret_cast<Identify>(m_library.resolve("NDD_PROC_IDENTIFY"));
        m_main = reinterpret_cast<Main>(m_library.resolve("NDD_PROC_MAIN"));
        QVERIFY(m_identify);
        QVERIFY(m_main);
    }
    void exportsAndAbi()
    {
        QVERIFY(!m_identify(nullptr));
        NddProcData data;
        QVERIFY(m_identify(&data));
        QCOMPARE(data.version, QStringLiteral(CALCTABDD_VERSION));
        QCOMPARE(data.menuType, 1);
        QCOMPARE(m_main(nullptr, QString(), {}, {}, &data), -1);
        Host host;
        QCOMPARE(initialize(host), 0);
        QCOMPARE(initialize(host), 0);
        QCOMPARE(host.findChildren<QAction *>(QStringLiteral("calctabddOpen")).size(), 1);
    }
    void nativeEmbeddingAndKeyIsolation()
    {
        Host host;
        QCOMPARE(initialize(host), 0);
        host.openAction()->trigger();
        QCOMPARE(host.tabs->count(), 2);
        auto *native = qobject_cast<QsciScintilla *>(host.tabs->currentWidget());
        QVERIFY(native);
        QVERIFY(native->isReadOnly());
        QVERIFY(host.page()->parentWidget() == native);
        QTRY_COMPARE(host.page()->geometry(), native->rect());
        QVERIFY(host.input());
        host.input()->setFocus();
        QTest::keyClicks(host.input(), "1+2*3");
        QTest::keyClick(host.input(), Qt::Key_Return);
        QCOMPARE(host.input()->toPlainText(), QStringLiteral("1+2*3"));
        QVERIFY(host.findChildren<QLabel *>(QStringLiteral("recordResult")).isEmpty());
        QTest::keyClick(host.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(host.findChild<QLabel *>(QStringLiteral("recordResult"))->text(), QStringLiteral("= 7"));
        QTest::keyClicks(host.input(), "2+3");
        QTest::keyClick(host.input(), Qt::Key_Enter, Qt::ControlModifier | Qt::KeypadModifier);
        QCOMPARE(host.findChildren<QLabel *>(QStringLiteral("recordResult")).size(), 2);
        QTest::keyClick(native->viewport(), Qt::Key_Return);
        QCOMPARE(native->text(), QString());
        QVERIFY(!native->isModified());
        QCOMPARE(host.ordinary->text(), QStringLiteral("普通文档，不得改动"));
        host.resize(650, 530);
        QTRY_COMPARE(host.page()->geometry(), native->rect());
        host.openAction()->trigger();
        QCOMPARE(host.creations, 1);
    }
    void switchingAndMenuRouting()
    {
        Host host;
        QCOMPARE(initialize(host), 0);
        auto *save = host.findChild<QAction *>(QStringLiteral("actionSave"));
        host.openAction()->trigger();
        auto *native = qobject_cast<QsciScintilla *>(host.tabs->currentWidget());
        QVERIFY(!save->isEnabled());
        QVERIFY(!host.edit->actions().contains(save));
        QApplication::clipboard()->setText(QStringLiteral("42+1"));
        host.findChild<QAction *>(QStringLiteral("calctabddRoute_actionpaste"))->trigger();
        QCOMPARE(host.input()->toPlainText(), QStringLiteral("42+1"));
        QCOMPARE(host.hostEditCalls, 0);
        QTest::keyClick(host.input(), Qt::Key_S, Qt::ControlModifier);
        QCOMPARE(host.hostEditCalls, 0);
        QTest::keyClick(host.input(), Qt::Key_Return, Qt::ControlModifier);
        host.tabs->setCurrentWidget(host.ordinary);
        QVERIFY(!host.page()->isVisible());
        QVERIFY(save->isEnabled());
        QVERIFY(host.edit->actions().contains(save));
        save->trigger();
        QCOMPARE(host.hostEditCalls, 1);
        host.tabs->setCurrentWidget(native);
        QTRY_VERIFY(host.page()->isVisible());
        native->viewport()->setFocus();
        QTRY_VERIFY(host.input()->hasFocus());
        QCOMPARE(host.findChild<QLabel *>(QStringLiteral("recordResult"))->text(), QStringLiteral("= 43"));
        QVERIFY(!native->isModified());
    }
    void lifecycleAndWindowsAreIndependent()
    {
        Host first;
        Host second;
        QCOMPARE(initialize(first), 0);
        QCOMPARE(initialize(second), 0);
        first.openAction()->trigger();
        second.openAction()->trigger();
        first.input()->setPlainText(QStringLiteral("123"));
        first.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        QCOMPARE(first.findChildren<QLabel *>(QStringLiteral("recordResult")).size(), 1);
        QCOMPARE(second.findChildren<QLabel *>(QStringLiteral("recordResult")).size(), 0);
        QPointer<QWidget> page = first.page();
        QPointer<QWidget> editor = first.tabs->currentWidget();
        first.closeCurrent();
        QVERIFY(page.isNull());
        QVERIFY(editor.isNull());
        QVERIFY(second.page()->isVisible());
        first.openAction()->trigger();
        QVERIFY(first.page());
        QCOMPARE(first.findChildren<QLabel *>(QStringLiteral("recordResult")).size(), 0);
    }
    void failedCreationPreservesExistingDocument()
    {
        Host host;
        host.failCreate = true;
        QCOMPARE(initialize(host), 0);
        host.openAction()->trigger();
        QCOMPARE(host.tabs->count(), 1);
        QVERIFY(!host.page());
        QVERIFY(!host.ordinary->isReadOnly());
        QCOMPARE(host.ordinary->text(), QStringLiteral("普通文档，不得改动"));
    }
    void reopenBeforeOldEditorIsDeleted()
    {
        Host host;
        QCOMPARE(initialize(host), 0);
        host.openAction()->trigger();
        QPointer<QWidget> previous = host.tabs->currentWidget();
        host.tabs->removeTab(host.tabs->currentIndex());
        previous->deleteLater();
        host.openAction()->trigger();
        auto *current = host.tabs->currentWidget();
        QVERIFY(current != previous);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(previous.isNull());
        auto *input = current->findChild<QPlainTextEdit *>(QStringLiteral("formulaInput"));
        QVERIFY(input);
        input->setPlainText(QStringLiteral("6*7"));
        QTest::keyClick(input, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(current->findChild<QLabel *>(QStringLiteral("recordResult"))->text(), QStringLiteral("= 42"));
        host.openAction()->trigger();
        QCOMPARE(host.creations, 2);
    }
    void windowDestructionWithPendingCallbacks()
    {
        auto *host = new Host;
        QCOMPARE(initialize(*host), 0);
        host->openAction()->trigger();
        host->input()->setPlainText(QStringLiteral("123"));
        host->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        QPointer<QWidget> page = host->page();
        delete host;
        QCoreApplication::processEvents();
        QVERIFY(page.isNull());
    }
};
QTEST_MAIN(PluginTests)
#include "plugin_tests.moc"
