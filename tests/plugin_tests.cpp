#include "numeric_test_helpers.h"
#include "ndd_plugin_api.h"
#include "calculation_session.h"
#include <Qsci/qsciscintilla.h>
#include <plugin.h>
#include <QApplication>
#include <QAbstractItemView>
#include <QCompleter>
#include <QDialog>
#include <QFileDialog>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QLineEdit>
#include <QInputMethodEvent>
#include <QToolButton>
#include <QTextBrowser>
#include <QClipboard>
#include <QLabel>
#include <QLibrary>
#include <QMainWindow>
#include <QDialogButtonBox>
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
    bool selectSession(Host &host, const QString &path, bool restore)
    {
        host.page()->findChild<QAction *>(restore ? QStringLiteral("restoreSession") : QStringLiteral("saveSessionAs"))->trigger();
        auto *dialog = host.page()->findChild<QFileDialog *>(QStringLiteral("sessionFileDialog"));
        if (!dialog || !dialog->isVisible()) return false;
        dialog->setDirectory(QFileInfo(path).absolutePath());
        dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))->setText(QFileInfo(path).fileName());
        auto *button = dialog->findChild<QDialogButtonBox *>()->button(restore ? QDialogButtonBox::Open : QDialogButtonBox::Save);
        if (!button || !button->isEnabled()) return false;
        button->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return !host.page()->findChild<QFileDialog *>(QStringLiteral("sessionFileDialog"));
    }
    CalculationSession readSession(const QString &path)
    {
        QFile file(path);
        CalculationSession session;
        if (!file.open(QIODevice::ReadOnly)) qFatal("Saved session cannot be read");
        const auto error = SessionFormat::decode(file.readAll(), session);
        if (!error.isEmpty()) qFatal("%s", qPrintable(error));
        return session;
    }
    bool defineFormula(Host &host, const QString &definition)
    {
        host.page()->findChild<QAction *>(QStringLiteral("defineCustomFormula"))->trigger();
        auto *dialog = host.page()->findChild<QDialog *>(QStringLiteral("customDefinitionDialog"));
        if (!dialog || !dialog->isVisible()) return false;
        dialog->findChild<QLineEdit *>(QStringLiteral("customDefinitionInput"))->setText(definition);
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return !host.page()->findChild<QDialog *>(QStringLiteral("customDefinitionDialog"));
    }
private slots:
    void typedResultsSurviveNativeHostCloseAndRestore()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("numeric.calctabdd"));
        Host first, second;
        QCOMPARE(initialize(first), 0);
        QCOMPARE(initialize(second), 0);
        first.openAction()->trigger();
        second.openAction()->trigger();
        auto submit = [](Host &host, const QString &formula) {
            host.input()->setPlainText(formula);
            host.page()->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        };
        submit(first, QStringLiteral("0.1+0.2"));
        QVERIFY(first.input());
        QVERIFY(first.findChild<QTextBrowser *>(QStringLiteral("recordResult")));
        QCOMPARE(first.findChild<QTextBrowser *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 0.3"));
        submit(first, QStringLiteral("ans-0.3"));
        QCOMPARE(first.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 0"));
        QVERIFY(defineFormula(first, QStringLiteral("A=sin(x/y)")));
        submit(first, QStringLiteral("x=1\ny=3"));
        auto *sources = first.page()->findChild<QTextBrowser *>(QStringLiteral("recordSources"));
        QVERIFY(sources);
        QCOMPARE(sources->toPlainText(), QStringLiteral("来源：含除法舍入；含近似计算；含转换损失"));
        QVERIFY(selectSession(first, path, false));
        const auto saved = readSession(path).history.answer();
        QVERIFY(saved.isBinary());
        QCOMPARE(saved.sources(), unsigned(NumericValue::Rounded | NumericValue::Approximate | NumericValue::ConversionLoss));
        first.closeCurrent();
        first.openAction()->trigger();
        QVERIFY(selectSession(first, path, true));
        QCOMPARE(first.page()->findChild<QTextBrowser *>(QStringLiteral("recordSources"))->toPlainText(), saved.sourceText());
        first.page()->findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        submit(first, QStringLiteral("ans"));
        QCOMPARE(readSession(path).history.answer(), saved);
        submit(second, QStringLiteral("ans"));
        QCOMPARE(second.findChild<QTextBrowser *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 0"));
        for (Host *host : {&first, &second})
        {
            const auto *native = qobject_cast<QsciScintilla *>(host->tabs->currentWidget());
            QVERIFY(native && native->isReadOnly() && native->text().isEmpty() && !native->isModified());
            QCOMPARE(host->ordinary->text(), QStringLiteral("普通文档，不得改动"));
            QCOMPARE(host->hostEditCalls, 0);
        }
    }
    void customFormulaModesAndFilesStayWithinTheirHostWindow()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("custom.calctabdd"));
        Host first;
        Host second;
        QCOMPARE(initialize(first), 0);
        QCOMPARE(initialize(second), 0);
        first.openAction()->trigger();
        second.openAction()->trigger();
        first.input()->setPlainText(QStringLiteral("99+"));
        QVERIFY(defineFormula(first, QStringLiteral("A=x^2+y")));
        first.input()->setPlainText(QStringLiteral("x=-2 y=3"));
        first.page()->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        QVERIFY(first.page()->findChildren<QTextBrowser *>(QStringLiteral("recordResult")).isEmpty());
        QVERIFY(first.page()->findChild<QLabel *>(QStringLiteral("calculationStatus"))->text().contains(QStringLiteral("每行只能填写一个参数")));
        first.input()->setPlainText(QStringLiteral("x=-2\ny=3"));
        first.page()->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        QCOMPARE(first.page()->findChild<QLabel *>(QStringLiteral("recordFormula"))->text(), QStringLiteral("A=(-2)^2+3"));
        QCOMPARE(first.page()->findChild<QTextBrowser *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 7"));
        QVERIFY(defineFormula(second, QStringLiteral("B=t*3")));
        second.input()->setPlainText(QStringLiteral("t=10"));
        second.page()->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        QVERIFY(selectSession(first, path, false));
        first.input()->setPlainText(QStringLiteral("x=4\ny=5"));
        first.page()->findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("99+"));
        first.closeCurrent();
        QCOMPARE(readSession(path).input.customDefinition, QString());
        QCOMPARE(readSession(path).customInput.text, QStringLiteral("x=4\ny=5"));
        first.openAction()->trigger();
        QVERIFY(first.input()->toPlainText().isEmpty());
        QVERIFY(!first.page()->findChild<QLabel *>(QStringLiteral("currentCustomDefinition"))->isVisible());
        QVERIFY(selectSession(first, path, true));
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("99+"));
        first.page()->findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("x=4\ny=5"));
        auto *native = qobject_cast<QsciScintilla *>(first.tabs->currentWidget());
        first.tabs->setCurrentWidget(first.ordinary);
        first.tabs->setCurrentWidget(native);
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("x=4\ny=5"));
        first.activateWindow();
        first.input()->setFocus();
        first.input()->moveCursor(QTextCursor::End);
        QTest::keyClick(first.input(), Qt::Key_Return);
        QTest::keyClick(first.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(asDouble(readSession(path).history.answer()), 21.0);
        QCOMPARE(second.page()->findChild<QLabel *>(QStringLiteral("currentCustomDefinition"))->text(), QStringLiteral("B=t*3"));
        QCOMPARE(second.page()->findChild<QTextBrowser *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 30"));
        for (Host *host : {&first, &second})
        {
            const auto *editor = qobject_cast<QsciScintilla *>(host->tabs->currentWidget());
            QVERIFY(editor->text().isEmpty());
            QVERIFY(!editor->isModified());
            QVERIFY(editor->isReadOnly());
            QCOMPARE(host->ordinary->text(), QStringLiteral("普通文档，不得改动"));
            QCOMPARE(host->hostEditCalls, 0);
        }
        second.closeCurrent();
        second.openAction()->trigger();
        QVERIFY(!second.page()->findChild<QLabel *>(QStringLiteral("currentCustomDefinition"))->isVisible());
        QVERIFY(second.input()->toPlainText().isEmpty());
        QVERIFY(second.page()->findChildren<QTextBrowser *>(QStringLiteral("recordResult")).isEmpty());
    }
    void customDefinitionDialogLifetime_data()
    {
        QTest::addColumn<int>("closeMode");
        QTest::newRow("switch") << 0;
        QTest::newRow("close-tab") << 1;
        QTest::newRow("close-window") << 2;
    }
    void customDefinitionDialogLifetime()
    {
        QFETCH(int, closeMode);
        auto *host = new Host;
        QCOMPARE(initialize(*host), 0);
        host->openAction()->trigger();
        host->input()->setPlainText(QStringLiteral("old draft"));
        auto *native = host->tabs->currentWidget();
        host->page()->findChild<QAction *>(QStringLiteral("defineCustomFormula"))->trigger();
        QPointer<QDialog> dialog = host->page()->findChild<QDialog *>(QStringLiteral("customDefinitionDialog"));
        QVERIFY(dialog && dialog->isVisible());
        dialog->findChild<QLineEdit *>()->setText(QStringLiteral("A=x+y"));
        if (closeMode == 0) host->tabs->setCurrentWidget(host->ordinary);
        else if (closeMode == 1) host->closeCurrent();
        else { delete host; host = nullptr; }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog.isNull());
        if (host)
        {
            if (closeMode == 0)
            {
                host->tabs->setCurrentWidget(native);
                QCOMPARE(host->input()->toPlainText(), QStringLiteral("old draft"));
            }
            else host->openAction()->trigger();
            QVERIFY(!host->page()->findChild<QLabel *>(QStringLiteral("currentCustomDefinition"))->isVisible());
            delete host;
        }
    }
    void savedSessionsSurviveTabAndWindowLifetimeIndependently()
    {
        QTemporaryDir directory;
        const QString firstPath = directory.filePath(QStringLiteral("first.calctabdd"));
        const QString secondPath = directory.filePath(QStringLiteral("second.calctabdd"));
        {
            Host first;
            Host second;
            QCOMPARE(initialize(first), 0);
            QCOMPARE(initialize(second), 0);
            first.openAction()->trigger();
            second.openAction()->trigger();
            first.input()->setPlainText(QStringLiteral("0.1+0.2"));
            first.page()->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
            QVERIFY(selectSession(first, firstPath, false));
            // 第二窗口不能恢复正在使用的会话，也不能覆盖它。
            QVERIFY(selectSession(second, firstPath, true));
            QVERIFY(second.page()->findChild<QLabel *>(QStringLiteral("sessionSaveStatus"))->text().contains(QStringLiteral("其他窗口或进程")));
            QVERIFY(second.page()->findChildren<QTextBrowser *>(QStringLiteral("recordResult")).isEmpty());
            second.input()->setPlainText(QStringLiteral("21"));
            second.page()->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
            QVERIFY(selectSession(second, secondPath, false));
            first.input()->setPlainText(QStringLiteral("原草稿😀"));
            first.input()->selectAll();
            first.activateWindow();
            first.input()->setFocus();
            QTest::keyClick(first.input(), Qt::Key_Up, Qt::AltModifier);
            first.input()->setPlainText(QStringLiteral("ans+2"));
            first.closeCurrent();
            const auto saved = readSession(firstPath);
            QCOMPARE(saved.history.count(), 1);
            QCOMPARE(asDouble(saved.history.answer()), 0.3);
            QCOMPARE(saved.input.text, QStringLiteral("ans+2"));
            QCOMPARE(saved.draft.text, QStringLiteral("原草稿😀"));
            QCOMPARE(asDouble(readSession(secondPath).history.answer()), 21.0);
            first.openAction()->trigger();
            QVERIFY(first.page()->findChildren<QTextBrowser *>(QStringLiteral("recordResult")).isEmpty());
            QVERIFY(first.input()->toPlainText().isEmpty());
            QVERIFY(selectSession(first, firstPath, true));
            QCOMPARE(first.input()->toPlainText(), QStringLiteral("ans+2"));
            first.page()->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
            QCOMPARE(first.input()->toPlainText(), QStringLiteral("原草稿😀"));
            QCOMPARE(asDouble(readSession(firstPath).history.answer()), 2.3);
            QCOMPARE(asDouble(readSession(secondPath).history.answer()), 21.0);
            for (Host *host : {&first, &second})
            {
                auto *native = qobject_cast<QsciScintilla *>(host->tabs->currentWidget());
                QVERIFY(native->text().isEmpty());
                QVERIFY(!native->isModified());
                QVERIFY(native->isReadOnly());
                QCOMPARE(host->ordinary->text(), QStringLiteral("普通文档，不得改动"));
                QCOMPARE(host->hostEditCalls, 0);
            }
            second.input()->setPlainText(QStringLiteral("窗口退出前草稿"));
        }
        QVERIFY(!QFileInfo::exists(firstPath + QStringLiteral(".lock")));
        QVERIFY(!QFileInfo::exists(secondPath + QStringLiteral(".lock")));
        Host reopened;
        QCOMPARE(initialize(reopened), 0);
        reopened.openAction()->trigger();
        QVERIFY(selectSession(reopened, secondPath, true));
        QCOMPARE(reopened.input()->toPlainText(), QStringLiteral("窗口退出前草稿"));
        QCOMPARE(reopened.page()->findChild<QTextBrowser *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 21"));
    }
    void sessionFileDialogsFollowPageLifetime_data()
    {
        QTest::addColumn<bool>("restore");
        QTest::addColumn<int>("closeMode");
        for (const bool restore : {false, true})
        {
            QTest::newRow(restore ? "restore-switch" : "save-switch") << restore << 0;
            QTest::newRow(restore ? "restore-close-tab" : "save-close-tab") << restore << 1;
            QTest::newRow(restore ? "restore-close-window" : "save-close-window") << restore << 2;
        }
    }
    void sessionFileDialogsFollowPageLifetime()
    {
        QFETCH(bool, restore);
        QFETCH(int, closeMode);
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("session.calctabdd"));
        CalculationSession original;
        original.history.calculate(QStringLiteral("19"));
        if (restore)
        {
            CalculationSessionFile file;
            QVERIFY(file.create(path, original).isEmpty());
        }
        auto *host = new Host;
        QCOMPARE(initialize(*host), 0);
        host->openAction()->trigger();
        QWidget *native = host->tabs->currentWidget();
        host->page()->findChild<QAction *>(restore ? QStringLiteral("restoreSession") : QStringLiteral("saveSessionAs"))->trigger();
        QPointer<QFileDialog> dialog = host->page()->findChild<QFileDialog *>(QStringLiteral("sessionFileDialog"));
        QVERIFY(dialog && dialog->isVisible());
        dialog->setDirectory(directory.path());
        dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))->setText(QFileInfo(path).fileName());
        if (closeMode == 0) host->tabs->setCurrentWidget(host->ordinary);
        else if (closeMode == 1) host->closeCurrent();
        else { delete host; host = nullptr; }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog.isNull());
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".lock")));
        if (restore) QCOMPARE(SessionFormat::encode(readSession(path)), SessionFormat::encode(original));
        else QVERIFY(!QFileInfo::exists(path));
        if (host)
        {
            if (closeMode == 0) host->tabs->setCurrentWidget(native);
            else host->openAction()->trigger();
            QVERIFY(host->page()->findChildren<QTextBrowser *>(QStringLiteral("recordResult")).isEmpty());
            delete host;
        }
    }
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
    void helpAboutMenusAndSharedWindow()
    {
        Host host;
        QCOMPARE(initialize(host), 0);
        QCOMPARE(initialize(host), 0);
        QCOMPARE(host.findChildren<QAction *>(QStringLiteral("calctabddHelp")).size(), 1);
        QCOMPARE(host.findChildren<QAction *>(QStringLiteral("calctabddAbout")).size(), 1);
        QCOMPARE(host.plugins->actions().size(), 3);
        host.findChild<QAction *>(QStringLiteral("calctabddHelp"))->trigger();
        QPointer<QDialog> help = host.findChild<QDialog *>(QStringLiteral("calctabddHelpDialog"));
        QVERIFY(help && help->isVisible());
        QCOMPARE(host.creations, 0);
        help->hide();
        host.openAction()->trigger();
        host.page()->findChild<QPushButton *>(QStringLiteral("helpButton"))->click();
        QCOMPARE(host.findChildren<QDialog *>(QStringLiteral("calctabddHelpDialog")).size(), 1);
        QVERIFY(help->isVisible());
        auto *search = help->findChild<QLineEdit *>();
        search->setText(QStringLiteral("精度"));
        help->activateWindow();
        search->setFocus();
        QTRY_VERIFY(search->hasFocus());
        search->selectAll();
        host.findChild<QAction *>(QStringLiteral("calctabddRoute_actioncopy"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("精度"));
        QApplication::clipboard()->setText(QStringLiteral("平方根"));
        host.findChild<QAction *>(QStringLiteral("calctabddRoute_actionpaste"))->trigger();
        QCOMPARE(search->text(), QStringLiteral("平方根"));
        QVERIFY(host.input()->toPlainText().isEmpty());
        host.findChild<QAction *>(QStringLiteral("calctabddAbout"))->trigger();
        auto *about = host.findChild<QDialog *>(QStringLiteral("calctabddAboutDialog"));
        QVERIFY(about && about->isVisible());
        NddProcData data;
        QVERIFY(m_identify(&data));
        QVERIFY(about->findChild<QTextBrowser *>()->toPlainText().contains(data.version));
        QCOMPARE(host.ordinary->text(), QStringLiteral("普通文档，不得改动"));
    }
    void completionKeepsNativeDocumentClean()
    {
        Host host;
        QCOMPARE(initialize(host), 0);
        host.openAction()->trigger();
        host.activateWindow();
        host.input()->setFocus();
        QTRY_VERIFY(host.input()->hasFocus());
        auto *native = qobject_cast<QsciScintilla *>(host.tabs->currentWidget());
        QTest::keyClicks(host.input(), "2+@sq");
        auto *popup = host.page()->findChild<QCompleter *>()->popup();
        QTRY_VERIFY(popup->isVisible());
        QTest::keyClick(host.input(), Qt::Key_Tab);
        QCOMPARE(host.input()->toPlainText(), QStringLiteral("2+sqrt()"));
        QTest::keyClicks(host.input(), "9");
        QTest::keyClick(host.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(host.findChild<QTextBrowser *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 5"));
        QCOMPARE(native->text(), QString());
        QVERIFY(!native->isModified());
        QTest::keyClicks(host.input(), "@");
        QTRY_VERIFY(popup->isVisible());
        host.tabs->setCurrentWidget(host.ordinary);
        QVERIFY(!popup->isVisible());
        host.tabs->setCurrentWidget(native);
        QPointer<QAbstractItemView> popupGuard = popup;
        host.closeCurrent();
        QVERIFY(popupGuard.isNull());
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
        QVERIFY(host.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).isEmpty());
        QTest::keyClick(host.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(host.findChild<QTextBrowser *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 7"));
        QTest::keyClicks(host.input(), "2+3");
        QTest::keyClick(host.input(), Qt::Key_Enter, Qt::ControlModifier | Qt::KeypadModifier);
        QCOMPARE(host.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).size(), 2);
        QTest::keyClick(native->viewport(), Qt::Key_Return);
        QCOMPARE(native->text(), QString());
        QVERIFY(!native->isModified());
        QCOMPARE(host.ordinary->text(), QStringLiteral("普通文档，不得改动"));
        host.resize(650, 530);
        QTRY_COMPARE(host.page()->geometry(), native->rect());
        host.openAction()->trigger();
        QCOMPARE(host.creations, 1);
    }
    void pageBackgroundCoversNativeEditor()
    {
        QTemporaryDir directory;
        Host host;
        QCOMPARE(initialize(host), 0);
        host.openAction()->trigger();
        auto *native = qobject_cast<QsciScintilla *>(host.tabs->currentWidget());
        QVERIFY(native);
        // 鲜明的底层颜色让行号栏、当前行和正文的任何透出都可被像素检查发现。
        native->setMarginLineNumbers(0, true);
        native->setMarginWidth(0, 68);
        native->setMarginsBackgroundColor(QColor("#fd0099"));
        native->setPaper(QColor("#00a8fc"));
        native->setCaretLineVisible(true);
        native->setCaretLineBackgroundColor(QColor("#e800e8"));
        native->SendScintilla(QsciScintillaBase::SCI_SETCARETLINEVISIBLEALWAYS, 1);
        for (const auto &formula : {QStringLiteral("1+2"), QStringLiteral("2+3")})
        {
            host.input()->setPlainText(formula);
            host.page()->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        }
        QCOMPARE(host.page()->findChild<QLabel *>(QStringLiteral("recordCount"))->text(), QStringLiteral("2 条记录"));
        QVERIFY(selectSession(host, directory.filePath(QStringLiteral("background.calctabdd")), false));
        QWidget *page = host.page();
        auto *header = page->findChild<QWidget *>(QStringLiteral("calculatorHeader"));
        auto *composer = page->findChild<QWidget *>(QStringLiteral("formulaComposer"));
        auto *status = page->findChild<QLabel *>(QStringLiteral("sessionSaveStatus"));
        QVERIFY(header && composer && status && status->isVisible());
        const QString screenshots = qEnvironmentVariable("CALCTABDD_SCREENSHOT_DIR");
        if (!screenshots.isEmpty()) QVERIFY(QDir().mkpath(screenshots));
        for (bool dark : {false, true, false})
        {
            QPalette colors = host.palette();
            const QColor background(dark ? "#202329" : "#f0f0f0");
            const QColor base(dark ? "#292d34" : "#ffffff");
            colors.setColor(QPalette::Window, background);
            colors.setColor(QPalette::Base, base);
            colors.setColor(QPalette::Text, QColor(dark ? "#e4e8ef" : "#202329"));
            page->setPalette(colors);
            for (int width : {960, 560})
            {
                host.resize(width, 740);
                host.tabs->setCurrentWidget(host.ordinary);
                host.tabs->setCurrentWidget(native);
                QCoreApplication::processEvents();
                QTRY_COMPARE(page->geometry(), native->rect());
                const QImage image = native->grab().toImage();
                const auto colorAt = [&image, native](QWidget *widget, const QPoint &point) {
                    const QPoint position = widget->mapTo(native, point);
                    return image.pixelColor(qRound(position.x() * image.devicePixelRatio()),
                                            qRound(position.y() * image.devicePixelRatio()));
                };
                if (!screenshots.isEmpty())
                    QVERIFY(image.save(screenshots + QStringLiteral("/native-background-%1-%2.png")
                        .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")).arg(width)));
                // 标题与输入区覆盖整行；透明状态标签下必须由页面绘制连续背景。
                QCOMPARE(colorAt(header, QPoint(8, 8)), base);
                QCOMPARE(colorAt(composer, QPoint(8, 8)), base);
                QCOMPARE(colorAt(status, QPoint(8, 4)), background);
                QCOMPARE(colorAt(status, QPoint(status->width() - 8, 4)), background);
            }
        }
        QVERIFY(native->isReadOnly());
        QCOMPARE(native->text(), QString());
        QVERIFY(!native->isModified());
        QCOMPARE(host.ordinary->text(), QStringLiteral("普通文档，不得改动"));
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
        QCOMPARE(host.findChild<QTextBrowser *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 43"));
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
        QCOMPARE(first.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).size(), 1);
        QCOMPARE(second.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).size(), 0);
        QPointer<QWidget> page = first.page();
        QPointer<QWidget> editor = first.tabs->currentWidget();
        first.closeCurrent();
        QVERIFY(page.isNull());
        QVERIFY(editor.isNull());
        QVERIFY(second.page()->isVisible());
        first.openAction()->trigger();
        QVERIFY(first.page());
        QCOMPARE(first.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).size(), 0);
    }
    void recordSessionsSurviveSwitchingAndResetOnReopen()
    {
        Host first;
        Host second;
        QCOMPARE(initialize(first), 0);
        QCOMPARE(initialize(second), 0);
        first.openAction()->trigger();
        second.openAction()->trigger();
        auto submit = [](Host &host, const QString &formula) {
            host.input()->setPlainText(formula);
            host.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        };
        submit(first, QStringLiteral("6*7"));
        submit(first, QStringLiteral("1/0"));
        submit(second, QStringLiteral("ans+5"));
        auto *native = qobject_cast<QsciScintilla *>(first.tabs->currentWidget());
        first.tabs->setCurrentWidget(first.ordinary);
        first.tabs->setCurrentWidget(native);
        submit(first, QStringLiteral("ans+1"));
        QCOMPARE(first.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 43"));
        QCOMPARE(second.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 5"));
        QCOMPARE(first.findChildren<QLabel *>(QStringLiteral("recordNumber")).last()->text(), QStringLiteral("03"));
        first.findChildren<QPushButton *>(QStringLiteral("reuseFormula")).last()->click();
        QTest::keyClick(first.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(first.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 44"));
        QVERIFY(native->text().isEmpty());
        QVERIFY(!native->isModified());
        first.closeCurrent();
        first.openAction()->trigger();
        submit(first, QStringLiteral("ans"));
        QCOMPARE(first.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).size(), 1);
        QCOMPARE(first.findChild<QTextBrowser *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 0"));
        QCOMPARE(first.findChild<QLabel *>(QStringLiteral("recordNumber"))->text(), QStringLiteral("01"));
        submit(second, QStringLiteral("ans+1"));
        QCOMPARE(second.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 6"));
        QCOMPARE(second.findChildren<QLabel *>(QStringLiteral("recordNumber")).last()->text(), QStringLiteral("02"));
        QCOMPARE(first.ordinary->text(), QStringLiteral("普通文档，不得改动"));
        QCOMPARE(second.ordinary->text(), QStringLiteral("普通文档，不得改动"));
    }
    void historyActionsKeepHostAndOtherWindowsIsolated()
    {
        Host first;
        Host second;
        QCOMPARE(initialize(first), 0);
        QCOMPARE(initialize(second), 0);
        first.openAction()->trigger();
        second.openAction()->trigger();
        first.activateWindow();
        first.input()->setFocus();
        QTRY_VERIFY(first.input()->hasFocus());
        first.input()->setPlainText(QStringLiteral("0.1+0.2"));
        QTest::keyClick(first.input(), Qt::Key_Return, Qt::ControlModifier);
        first.page()->findChild<QAction *>(QStringLiteral("copyValue"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("0.3"));
        first.page()->findChild<QAction *>(QStringLiteral("copyCalculation"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("0.1+0.2\n= 0.3"));
        first.input()->setPlainText(QStringLiteral("-2"));
        QTest::keyClick(first.input(), Qt::Key_Return, Qt::ControlModifier);
        first.input()->setPlainText(QStringLiteral("99^2"));
        QTextCursor cursor = first.input()->textCursor();
        cursor.setPosition(0);
        cursor.setPosition(2, QTextCursor::KeepAnchor);
        first.input()->setTextCursor(cursor);
        first.page()->findChildren<QAction *>(QStringLiteral("insertResult")).last()->trigger();
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("(-2)^2"));
        first.findChild<QAction *>(QStringLiteral("calctabddRoute_actionundo"))->trigger();
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("99^2"));
        first.findChild<QAction *>(QStringLiteral("calctabddRoute_actionredo"))->trigger();
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("(-2)^2"));
        QTest::keyClick(first.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(first.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 4"));
        auto *native = qobject_cast<QsciScintilla *>(first.tabs->currentWidget());
        QVERIFY(native->text().isEmpty());
        QVERIFY(!native->isModified());
        QCOMPARE(first.hostEditCalls, 0);
        QVERIFY(second.input()->toPlainText().isEmpty());
        QVERIFY(second.page()->findChildren<QToolButton *>(QStringLiteral("recordActions")).isEmpty());
        first.tabs->setCurrentWidget(first.ordinary);
        QVERIFY(first.findChild<QAction *>(QStringLiteral("actioncopy"))->isEnabled());
        first.tabs->setCurrentWidget(native);
        first.page()->findChild<QAction *>(QStringLiteral("copyValue"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("0.3"));
        QPointer<QToolButton> menuOwner = first.page()->findChild<QToolButton *>(QStringLiteral("recordActions"));
        first.closeCurrent();
        QVERIFY(menuOwner.isNull());
        first.openAction()->trigger();
        QVERIFY(first.page()->findChildren<QToolButton *>(QStringLiteral("recordActions")).isEmpty());
        QCOMPARE(first.ordinary->text(), QStringLiteral("普通文档，不得改动"));
        QCOMPARE(second.ordinary->text(), QStringLiteral("普通文档，不得改动"));
    }
    void diagnosticRangesStayInsideCalculatorInput()
    {
        Host first;
        Host second;
        QCOMPARE(initialize(first), 0);
        QCOMPARE(initialize(second), 0);
        first.openAction()->trigger();
        second.openAction()->trigger();
        first.input()->setPlainText(QStringLiteral("42"));
        first.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        second.input()->setPlainText(QStringLiteral("第二窗口草稿"));
        first.activateWindow();
        first.input()->setFocus();
        QTRY_VERIFY(first.input()->hasFocus());
        first.input()->setPlainText(QStringLiteral(" \n ln(0) "));
        QTest::keyClick(first.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(first.input()->extraSelections().size(), 1);
        QCOMPARE(first.input()->extraSelections().first().cursor.selectedText(), QStringLiteral("0"));
        QCOMPARE(first.input()->textCursor().position(), 6);
        QVERIFY(first.page()->findChild<QLabel *>(QStringLiteral("calculationStatus"))->text().contains(QStringLiteral("第 2 行，第 5 列")));
        QVERIFY(second.input()->extraSelections().isEmpty());
        QCOMPARE(second.input()->toPlainText(), QStringLiteral("第二窗口草稿"));
        auto *native = qobject_cast<QsciScintilla *>(first.tabs->currentWidget());
        QVERIFY(native);
        first.tabs->setCurrentWidget(first.ordinary);
        first.tabs->setCurrentWidget(native);
        QTRY_VERIFY(first.input()->hasFocus());
        QCOMPARE(first.input()->extraSelections().size(), 1);
        QTest::keyClick(first.input(), Qt::Key_Delete);
        QTest::keyClicks(first.input(), "1");
        QVERIFY(first.input()->extraSelections().isEmpty());
        QCOMPARE(first.input()->toPlainText(), QStringLiteral(" \n ln(1) "));
        first.findChild<QAction *>(QStringLiteral("calctabddRoute_actionundo"))->trigger();
        QVERIFY(first.input()->extraSelections().isEmpty());
        first.input()->setPlainText(QStringLiteral("ans+1"));
        QTest::keyClick(first.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(first.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 43"));
        QVERIFY(native->text().isEmpty());
        QVERIFY(!native->isModified());
        QCOMPARE(first.hostEditCalls, 0);
        first.closeCurrent();
        first.openAction()->trigger();
        QVERIFY(first.input()->extraSelections().isEmpty());
        QVERIFY(first.input()->accessibleDescription().isEmpty());
        QCOMPARE(second.input()->toPlainText(), QStringLiteral("第二窗口草稿"));
        QCOMPARE(first.ordinary->text(), QStringLiteral("普通文档，不得改动"));
        QCOMPARE(second.ordinary->text(), QStringLiteral("普通文档，不得改动"));
    }
    void historyRecallKeepsWindowDraftsAndNativeBuffersIndependent()
    {
        Host first;
        Host second;
        QCOMPARE(initialize(first), 0);
        QCOMPARE(initialize(second), 0);
        first.openAction()->trigger();
        second.openAction()->trigger();
        first.input()->setPlainText(QStringLiteral("6*7"));
        first.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        second.input()->setPlainText(QStringLiteral("100"));
        second.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        first.input()->setPlainText(QStringLiteral("第一份草稿"));
        second.input()->setPlainText(QStringLiteral("第二份草稿"));
        first.activateWindow();
        first.input()->setFocus();
        QTRY_VERIFY(first.input()->hasFocus());
        QTest::keyClick(first.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("6*7"));
        QCOMPARE(second.input()->toPlainText(), QStringLiteral("第二份草稿"));
        auto *native = qobject_cast<QsciScintilla *>(first.tabs->currentWidget());
        first.tabs->setCurrentWidget(first.ordinary);
        first.tabs->setCurrentWidget(native);
        QTRY_VERIFY(first.input()->hasFocus());
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("6*7"));
        QTest::keyClicks(first.input(), "+1");
        QTest::keyClick(first.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(first.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 43"));
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("第一份草稿"));
        second.activateWindow();
        second.input()->setFocus();
        QTRY_VERIFY(second.input()->hasFocus());
        QTest::keyClick(second.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(second.input()->toPlainText(), QStringLiteral("100"));
        QVERIFY(native->text().isEmpty());
        QVERIFY(!native->isModified());
        first.closeCurrent();
        first.openAction()->trigger();
        first.activateWindow();
        first.input()->setFocus();
        QTRY_VERIFY(first.input()->hasFocus());
        QTest::keyClick(first.input(), Qt::Key_Up, Qt::AltModifier);
        QVERIFY(first.input()->toPlainText().isEmpty());
        second.activateWindow();
        second.input()->setFocus();
        QTRY_VERIFY(second.input()->hasFocus());
        QTest::keyClick(second.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(second.input()->toPlainText(), QStringLiteral("第二份草稿"));
        QCOMPARE(first.ordinary->text(), QStringLiteral("普通文档，不得改动"));
        QCOMPARE(second.ordinary->text(), QStringLiteral("普通文档，不得改动"));
    }
    void historyShortcutOverridesHostOnlyInInput()
    {
        Host host;
        auto *conflict = host.edit->addAction(QStringLiteral("宿主 Alt+Up 命令"));
        conflict->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
        connect(conflict, &QAction::triggered, &host, [&host]() { ++host.hostEditCalls; });
        QCOMPARE(initialize(host), 0);
        host.openAction()->trigger();
        host.activateWindow();
        host.input()->setFocus();
        QTRY_VERIFY(host.input()->hasFocus());
        host.input()->setPlainText(QStringLiteral("42"));
        QTest::keyClick(host.input(), Qt::Key_Return, Qt::ControlModifier);
        host.input()->setPlainText(QStringLiteral("draft"));
        QTest::keyClick(host.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(host.input()->toPlainText(), QStringLiteral("42"));
        QCOMPARE(host.hostEditCalls, 0);
        QTest::keyClick(host.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(host.input()->toPlainText(), QStringLiteral("draft"));
        host.input()->setPlainText(QStringLiteral("@p"));
        host.input()->moveCursor(QTextCursor::End);
        auto *popup = host.page()->findChild<QCompleter *>()->popup();
        QTRY_VERIFY(popup->isVisible());
        QTest::keyClick(popup, Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(host.hostEditCalls, 0);
        QCOMPARE(host.input()->toPlainText(), QStringLiteral("@p"));
        QTest::keyClick(popup, Qt::Key_Escape);
        host.input()->setPlainText(QStringLiteral("9+"));
        host.input()->moveCursor(QTextCursor::End);
        QInputMethodEvent preedit(QStringLiteral("中"), {});
        QApplication::sendEvent(host.input(), &preedit);
        QTest::keyClick(host.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(host.input()->toPlainText(), QStringLiteral("9+"));
        QCOMPARE(host.hostEditCalls, 0);
        QInputMethodEvent finish;
        QApplication::sendEvent(host.input(), &finish);
        QTest::keyClick(host.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(host.input()->toPlainText(), QStringLiteral("42"));
        host.tabs->setCurrentWidget(host.ordinary);
        host.ordinary->setFocus();
        QTRY_VERIFY(host.ordinary->hasFocus());
        QTest::keyClick(host.ordinary, Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(host.hostEditCalls, 1);
        QCOMPARE(host.input()->toPlainText(), QStringLiteral("42"));
        QCOMPARE(host.ordinary->text(), QStringLiteral("普通文档，不得改动"));
    }
    void clearSessionKeepsOtherWindowAndNativeBuffersIndependent()
    {
        Host first;
        Host second;
        QCOMPARE(initialize(first), 0);
        QCOMPARE(initialize(second), 0);
        first.openAction()->trigger();
        second.openAction()->trigger();
        first.input()->setPlainText(QStringLiteral("42"));
        first.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        second.input()->setPlainText(QStringLiteral("100"));
        second.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        first.input()->setPlainText(QStringLiteral("ans+1"));
        second.input()->setPlainText(QStringLiteral("second draft"));
        first.activateWindow();
        first.input()->setFocus();
        QTRY_VERIFY(first.input()->hasFocus());
        QTest::keyClick(first.input(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClicks(first.input(), "+9");
        auto *native = qobject_cast<QsciScintilla *>(first.tabs->currentWidget());
        auto *clear = first.page()->findChild<QPushButton *>(QStringLiteral("clearSessionButton"));
        QTest::mouseClick(clear, Qt::LeftButton);
        auto *dialog = first.page()->findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
        QVERIFY(dialog && dialog->isVisible());
        QCOMPARE(dialog->windowModality(), Qt::WindowModal);
        // 当前窗口确认期间，另一个宿主仍可正常接收键盘并计算。
        second.activateWindow();
        second.input()->setFocus();
        QTRY_VERIFY(second.input()->hasFocus());
        second.input()->setPlainText(QStringLiteral("ans+2"));
        QTest::keyClick(second.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(second.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 102"));
        second.input()->setPlainText(QStringLiteral("second draft"));
        first.findChild<QAction *>(QStringLiteral("calctabddRoute_actioncut"))->trigger();
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("42+9"));
        dialog->activateWindow();
        QTest::mouseClick(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
        QVERIFY(first.page()->findChildren<QTextBrowser *>(QStringLiteral("recordResult")).isEmpty());
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("ans+1"));
        QCOMPARE(second.input()->toPlainText(), QStringLiteral("second draft"));
        QCOMPARE(second.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).size(), 2);
        first.activateWindow();
        first.input()->setFocus();
        QTRY_VERIFY(first.input()->hasFocus());
        QTest::keyClick(first.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(first.findChild<QTextBrowser *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 1"));
        QCOMPARE(first.findChild<QLabel *>(QStringLiteral("recordNumber"))->text(), QStringLiteral("01"));
        QVERIFY(native->text().isEmpty());
        QVERIFY(!native->isModified());
        QCOMPARE(first.hostEditCalls, 0);
        first.tabs->setCurrentWidget(first.ordinary);
        QVERIFY(first.findChild<QAction *>(QStringLiteral("actionSave"))->isEnabled());
        first.tabs->setCurrentWidget(native);
        first.closeCurrent();
        first.openAction()->trigger();
        QVERIFY(first.page()->findChildren<QTextBrowser *>(QStringLiteral("recordResult")).isEmpty());
        QVERIFY(!first.page()->findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->isEnabled());
        QCOMPARE(first.ordinary->text(), QStringLiteral("普通文档，不得改动"));
        QCOMPARE(second.ordinary->text(), QStringLiteral("普通文档，不得改动"));
        auto *secondNative = qobject_cast<QsciScintilla *>(second.tabs->currentWidget());
        QVERIFY(secondNative->text().isEmpty());
        QVERIFY(!secondNative->isModified());
    }
    void hidingPageCancelsClearConfirmation()
    {
        Host host;
        QCOMPARE(initialize(host), 0);
        host.openAction()->trigger();
        host.input()->setPlainText(QStringLiteral("42"));
        host.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        host.input()->setPlainText(QStringLiteral("ans+1"));
        auto *native = host.tabs->currentWidget();
        host.page()->findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->click();
        QPointer<QDialog> dialog = host.page()->findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
        QVERIFY(dialog && dialog->isVisible());
        host.tabs->setCurrentWidget(host.ordinary);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog.isNull());
        host.tabs->setCurrentWidget(native);
        QCOMPARE(host.findChild<QTextBrowser *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 42"));
        QCOMPARE(host.input()->toPlainText(), QStringLiteral("ans+1"));
        host.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        QCOMPARE(host.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 43"));
    }
    void destroyingPageWithClearConfirmationIsSafe()
    {
        auto *host = new Host;
        QCOMPARE(initialize(*host), 0);
        host->openAction()->trigger();
        host->input()->setPlainText(QStringLiteral("42"));
        host->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        host->page()->findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->click();
        QPointer<QDialog> dialog = host->page()->findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
        QVERIFY(dialog);
        host->closeCurrent();
        QVERIFY(dialog.isNull());
        host->openAction()->trigger();
        host->input()->setPlainText(QStringLiteral("ans+1"));
        host->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        QCOMPARE(host->findChild<QTextBrowser *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 1"));
        host->page()->findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->click();
        dialog = host->page()->findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
        QVERIFY(dialog);
        delete host;
        QCoreApplication::processEvents();
        QVERIFY(dialog.isNull());
    }
    void exportUsesCurrentWindowAndKeepsNativeBuffersClean()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Host first;
        Host second;
        QCOMPARE(initialize(first), 0);
        QCOMPARE(initialize(second), 0);
        first.openAction()->trigger();
        second.openAction()->trigger();
        first.input()->setPlainText(QStringLiteral("42"));
        first.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        second.input()->setPlainText(QStringLiteral("100"));
        second.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        first.input()->setPlainText(QStringLiteral("ans+1"));
        auto *native = qobject_cast<QsciScintilla *>(first.tabs->currentWidget());
        first.page()->findChild<QAction *>(QStringLiteral("exportMarkdown"))->trigger();
        QPointer<QFileDialog> dialog = first.page()->findChild<QFileDialog *>(QStringLiteral("exportHistoryDialog"));
        QVERIFY(dialog && dialog->isVisible());
        QCOMPARE(dialog->windowModality(), Qt::WindowModal);
        second.activateWindow();
        second.input()->setFocus();
        QTRY_VERIFY(second.input()->hasFocus());
        second.input()->setPlainText(QStringLiteral("ans+2"));
        QTest::keyClick(second.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(second.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 102"));
        second.input()->setPlainText(QStringLiteral("第二窗口草稿"));
        first.findChild<QAction *>(QStringLiteral("calctabddRoute_actioncut"))->trigger();
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("ans+1"));
        dialog->setDirectory(directory.path());
        dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))->setText(QStringLiteral("第一窗口.md"));
        dialog->activateWindow();
        QTest::mouseClick(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save), Qt::LeftButton);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog.isNull());
        QFile file(directory.filePath(QStringLiteral("第一窗口.md")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray contents = file.readAll();
        QVERIFY(contents.startsWith("# CalcTabdd"));
        QVERIFY(contents.contains("42\n= 42"));
        QVERIFY(!contents.contains("100"));
        QVERIFY(!contents.contains("102"));
        QVERIFY(!contents.contains("ans+1"));
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("ans+1"));
        first.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        QCOMPARE(first.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 43"));
        QCOMPARE(second.input()->toPlainText(), QStringLiteral("第二窗口草稿"));
        QCOMPARE(second.findChildren<QTextBrowser *>(QStringLiteral("recordResult")).size(), 2);
        QVERIFY(native->text().isEmpty());
        QVERIFY(!native->isModified());
        QCOMPARE(first.hostEditCalls, 0);
        first.tabs->setCurrentWidget(first.ordinary);
        QVERIFY(first.findChild<QAction *>(QStringLiteral("actionSave"))->isEnabled());
        QCOMPARE(first.ordinary->text(), QStringLiteral("普通文档，不得改动"));
        QCOMPARE(second.ordinary->text(), QStringLiteral("普通文档，不得改动"));
        auto *secondNative = qobject_cast<QsciScintilla *>(second.tabs->currentWidget());
        QVERIFY(secondNative->text().isEmpty());
        QVERIFY(!secondNative->isModified());
    }
    void exportDialogsFollowPageLifetime_data()
    {
        QTest::addColumn<bool>("overwrite");
        QTest::addColumn<int>("closeMode");
        for (const bool overwrite : {false, true})
        {
            QTest::newRow(overwrite ? "overwrite-switch" : "file-switch") << overwrite << 0;
            QTest::newRow(overwrite ? "overwrite-close-tab" : "file-close-tab") << overwrite << 1;
            QTest::newRow(overwrite ? "overwrite-close-window" : "file-close-window") << overwrite << 2;
        }
    }
    void exportDialogsFollowPageLifetime()
    {
        QFETCH(bool, overwrite);
        QFETCH(int, closeMode);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("result.txt"));
        QFile original(path);
        QVERIFY(original.open(QIODevice::WriteOnly));
        original.write("old file");
        original.close();
        auto *host = new Host;
        QCOMPARE(initialize(*host), 0);
        host->openAction()->trigger();
        host->input()->setPlainText(QStringLiteral("42"));
        host->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        host->input()->setPlainText(QStringLiteral("ans+1"));
        auto *native = host->tabs->currentWidget();
        host->page()->findChild<QAction *>(QStringLiteral("exportText"))->trigger();
        auto *fileDialog = host->page()->findChild<QFileDialog *>(QStringLiteral("exportHistoryDialog"));
        QVERIFY(fileDialog);
        QPointer<QDialog> dialog = fileDialog;
        if (overwrite)
        {
            fileDialog->setDirectory(directory.path());
            fileDialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))->setText(QStringLiteral("result.txt"));
            fileDialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            dialog = host->page()->findChild<QDialog *>(QStringLiteral("exportOverwriteConfirmation"));
            QVERIFY(dialog);
        }
        if (closeMode == 0) host->tabs->setCurrentWidget(host->ordinary);
        else if (closeMode == 1) host->closeCurrent();
        else { delete host; host = nullptr; }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog.isNull());
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("old file"));
        if (host)
        {
            if (closeMode == 0) host->tabs->setCurrentWidget(native);
            else host->openAction()->trigger();
            QVERIFY(!host->page()->findChild<QFileDialog *>());
            if (closeMode == 0)
            {
                QCOMPARE(host->input()->toPlainText(), QStringLiteral("ans+1"));
                host->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
                QCOMPARE(host->findChildren<QTextBrowser *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 43"));
            }
            else
            {
                QVERIFY(host->page()->findChildren<QTextBrowser *>(QStringLiteral("recordResult")).isEmpty());
                QVERIFY(!host->page()->findChild<QToolButton *>(QStringLiteral("exportHistoryButton"))->isEnabled());
            }
            delete host;
        }
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
        QCOMPARE(current->findChild<QTextBrowser *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 42"));
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
