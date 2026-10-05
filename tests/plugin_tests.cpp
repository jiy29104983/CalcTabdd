#include "ndd_plugin_api.h"
#include "calculation_session.h"
#include "document_selection.h"
#include <Qsci/qsciscintilla.h>
#include <plugin.h>
#include <QApplication>
#include <QAbstractItemView>
#include <QCompleter>
#include <QDialog>
#include <QFileDialog>
#include <QFile>
#include <QTemporaryDir>
#include <QLineEdit>
#include <QInputMethodEvent>
#include <QToolButton>
#include <QTextBrowser>
#include <QClipboard>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QLibrary>
#include <QMainWindow>
#include <QDialogButtonBox>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QScrollBar>
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
        ordinary->setProperty("type", 1);
        ordinary->setUtf8(true);
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
private slots:
    void selectionSnapshotPreservesTextAndEditorState_data()
    {
        QTest::addColumn<QString>("selected");
        QTest::addColumn<bool>("reverse");
        QTest::addColumn<bool>("readOnly");
        const QStringList texts = {QStringLiteral("1+2"), QStringLiteral("中文😀é\t×π"),
            QStringLiteral("1+2\r\n3+4\n5+6\r7+8"), QString(QChar::Null) + QStringLiteral("1+2") + QChar::Null,
            QStringLiteral(" "), QString()};
        for (int i = 0; i < texts.size(); ++i)
            for (bool reverse : {false, true})
                for (bool readOnly : {false, true})
                    QTest::newRow(qPrintable(QStringLiteral("text%1-reverse%2-readonly%3").arg(i).arg(reverse).arg(readOnly)))
                        << texts.at(i) << reverse << readOnly;
    }
    void selectionSnapshotPreservesTextAndEditorState()
    {
        QFETCH(QString, selected);
        QFETCH(bool, reverse);
        QFETCH(bool, readOnly);
        Host host;
        auto *editor = host.ordinary;
        const QString prefix = QStringLiteral("前缀😀\r\n第2行 ");
        const QString original = prefix + selected + QStringLiteral(" 后缀");
        // add text through the byte API so embedded NULs are retained.
        editor->clear();
        const QByteArray bytes = original.toUtf8();
        editor->SendScintilla(QsciScintillaBase::SCI_ADDTEXT, static_cast<quintptr>(bytes.size()), bytes.constData());
        const int start = prefix.toUtf8().size(), end = start + selected.toUtf8().size();
        editor->SendScintilla(QsciScintillaBase::SCI_SETSEL, reverse ? end : start, reverse ? start : end);
        editor->setReadOnly(readOnly);
        const long anchor = editor->SendScintilla(QsciScintillaBase::SCI_GETANCHOR);
        const long caret = editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS);
        const bool modified = editor->isModified(), undo = editor->isUndoAvailable(), redo = editor->isRedoAvailable();
        const int vertical = editor->verticalScrollBar()->value(), horizontal = editor->horizontalScrollBar()->value();
        QApplication::clipboard()->setText(QStringLiteral("clipboard unchanged"));
        QSignalSpy clipboardChanges(QApplication::clipboard(), &QClipboard::dataChanged);
        QSignalSpy textChanges(editor, &QsciScintilla::textChanged);
        QSignalSpy selectionChanges(editor, &QsciScintilla::selectionChanged);
        const auto result = readDocumentSelection(host.tabs);
        QVERIFY2(result.status == (selected.isEmpty() ? DocumentSelection::Status::Empty : DocumentSelection::Status::Selected), qPrintable(result.error));
        QCOMPARE(result.text, selected);
        QCOMPARE(result.startByte, qintptr(start));
        QCOMPARE(result.endByte, qintptr(end));
        QCOMPARE(result.readOnly, readOnly);
        QCOMPARE(result.sourceName, QStringLiteral("普通文档"));
        QCOMPARE(editor->text(), original);
        QCOMPARE(editor->SendScintilla(QsciScintillaBase::SCI_GETANCHOR), anchor);
        QCOMPARE(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS), caret);
        QCOMPARE(editor->isModified(), modified);
        QCOMPARE(editor->isReadOnly(), readOnly);
        QCOMPARE(editor->isUndoAvailable(), undo);
        QCOMPARE(editor->isRedoAvailable(), redo);
        QCOMPARE(editor->verticalScrollBar()->value(), vertical);
        QCOMPARE(editor->horizontalScrollBar()->value(), horizontal);
        QCOMPARE(textChanges.count(), 0);
        QCOMPARE(selectionChanges.count(), 0);
        QCOMPARE(clipboardChanges.count(), 0);
    }
    void unsupportedSelectionModesAreNotFlattened_data()
    {
        QTest::addColumn<int>("mode");
        for (int mode = 0; mode < 4; ++mode) QTest::newRow(qPrintable(QString::number(mode))) << mode;
    }
    void unsupportedSelectionModesAreNotFlattened()
    {
        QFETCH(int, mode);
        Host host;
        auto *editor = host.ordinary;
        editor->setText(QStringLiteral("12345\n67890\nabcde"));
        editor->SendScintilla(QsciScintillaBase::SCI_SETSEL, 1, 3);
        if (mode == 0) editor->SendScintilla(QsciScintillaBase::SCI_ADDSELECTION, 10, 8);
        else if (mode == 1) editor->SendScintilla(QsciScintillaBase::SCI_SETSELECTIONMODE, 1);
        else if (mode == 2) editor->SendScintilla(QsciScintillaBase::SCI_SETSELECTIONMODE, 2);
        else
        {
            editor->SendScintilla(QsciScintillaBase::SCI_SETVIRTUALSPACEOPTIONS, 2);
            editor->SendScintilla(QsciScintillaBase::SCI_SETSEL, 5, 5);
            editor->SendScintilla(QsciScintillaBase::SCI_SETSELECTIONNCARETVIRTUALSPACE, 0, 3);
        }
        const auto before = editor->selectedText();
        const long count = editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONS);
        const auto result = readDocumentSelection(host.tabs);
        QCOMPARE(result.status, DocumentSelection::Status::Unsupported);
        QVERIFY(result.error.contains(QStringLiteral("连续文本选区")));
        QVERIFY(result.text.isEmpty());
        QCOMPARE(editor->selectedText(), before);
        QCOMPARE(editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONS), count);
    }
    void selectionRejectsUnknownEditorsAndMissingExports()
    {
        QCOMPARE(readDocumentSelection(nullptr).status, DocumentSelection::Status::Unsupported);
        Host host;
        for (int type : {0, 2, 3, 4, 5})
        {
            host.ordinary->setProperty("type", type);
            QCOMPARE(readDocumentSelection(host.tabs).status, DocumentSelection::Status::Unsupported);
        }
        host.ordinary->setProperty("type", 1);
        host.ordinary->setProperty("calctabddNativeTab", true);
        QCOMPARE(readDocumentSelection(host.tabs).status, DocumentSelection::Status::Unsupported);
        QProcess process;
        process.start(QStringLiteral(CALCTABDD_SELECTION_PROBE_PATH),
            {QStringLiteral("exported-unavailable"), QStringLiteral("unicode"), QStringLiteral("-platform"), QStringLiteral("offscreen")});
        QVERIFY(process.waitForFinished(15000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        QVERIFY(process.readAllStandardOutput().contains("unsupported"));
    }
    void selectionPreviewUsesCurrentWindowAndTracksLifetime()
    {
        Host first, second;
        for (Host *host : {&first, &second})
        {
            QCOMPARE(initialize(*host), 0);
            host->ordinary->setText(host == &first ? QStringLiteral("first 1+2") : QStringLiteral("second 3+4"));
            host->ordinary->setSelection(0, host == &first ? 6 : 7, 0, host == &first ? 9 : 10);
            host->ordinary->setModified(false);
            host->findChild<QAction *>(QStringLiteral("calctabddInspectSelection"))->trigger();
        }
        QPointer<QDialog> one = first.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog"));
        QPointer<QDialog> two = second.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog"));
        QVERIFY(one && two);
        QCOMPARE(one->findChild<QPlainTextEdit *>(QStringLiteral("selectionPreview"))->toPlainText(), QStringLiteral("1+2"));
        QCOMPARE(two->findChild<QPlainTextEdit *>(QStringLiteral("selectionPreview"))->toPlainText(), QStringLiteral("3+4"));
        QVERIFY(one->findChild<QPlainTextEdit *>(QStringLiteral("selectionPreview"))->isReadOnly());
        first.openAction()->trigger();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!one);
        QVERIFY(two);
        first.findChild<QAction *>(QStringLiteral("calctabddInspectSelection"))->trigger();
        QVERIFY(!first.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog")));
        second.closeCurrent();
        QVERIFY(!two);
        QCOMPARE(first.ordinary->text(), QStringLiteral("first 1+2"));
        QVERIFY(!first.ordinary->isModified());
    }
    void selectionPreviewHandlesEmptyReadOnlyAndRepeatedOpen()
    {
        Host host;
        QCOMPARE(initialize(host), 0);
        auto *action = host.findChild<QAction *>(QStringLiteral("calctabddInspectSelection"));
        action->trigger();
        QVERIFY(!host.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog")));
        QVERIFY(host.statusBar()->currentMessage().contains(QStringLiteral("没有选中")));
        host.ordinary->selectAll();
        host.ordinary->setReadOnly(true);
        action->trigger();
        QPointer<QDialog> first = host.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog"));
        QVERIFY(first);
        action->trigger();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!first);
        QCOMPARE(host.findChildren<QDialog *>(QStringLiteral("calctabddSelectionDialog")).size(), 1);
        QVERIFY(host.ordinary->isReadOnly());
        QVERIFY(!host.ordinary->isModified());
    }
    void selectionRejectsInvalidUtf8AndPreservesLatin1()
    {
        Host host;
        auto *editor = host.ordinary;
        editor->clear();
        const QByteArray invalid = QByteArray::fromHex("c328");
        editor->SendScintilla(QsciScintillaBase::SCI_ADDTEXT, static_cast<quintptr>(invalid.size()), invalid.constData());
        editor->SendScintilla(QsciScintillaBase::SCI_SETSEL, 0, 2);
        const auto rejected = readDocumentSelection(host.tabs);
        QCOMPARE(rejected.status, DocumentSelection::Status::Unsupported);
        QVERIFY(rejected.error.contains(QStringLiteral("UTF-8")));
        QVERIFY(rejected.text.isEmpty());
        editor->setUtf8(false);
        const auto latin1 = readDocumentSelection(host.tabs);
        QCOMPARE(latin1.status, DocumentSelection::Status::Selected);
        QCOMPARE(latin1.text, QString::fromLatin1(invalid.constData(), invalid.size()));
    }
    void selectionPreviewTheme_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void selectionPreviewTheme()
    {
        QFETCH(bool, dark);
        Host host;
        QPalette palette = host.palette();
        if (dark)
        {
            palette.setColor(QPalette::Window, QColor(35, 38, 43));
            palette.setColor(QPalette::WindowText, QColor(235, 237, 240));
            palette.setColor(QPalette::Base, QColor(25, 28, 33));
            palette.setColor(QPalette::Text, QColor(235, 237, 240));
            palette.setColor(QPalette::Button, QColor(45, 48, 53));
            palette.setColor(QPalette::ButtonText, QColor(235, 237, 240));
            host.setPalette(palette);
        }
        QCOMPARE(initialize(host), 0);
        host.ordinary->setText(QStringLiteral("中文😀前缀 1+2\r\n3+4 后缀"));
        host.ordinary->setSelection(0, 6, 1, 3);
        host.findChild<QAction *>(QStringLiteral("calctabddInspectSelection"))->trigger();
        auto *dialog = host.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog"));
        QVERIFY(dialog);
        auto *preview = dialog->findChild<QPlainTextEdit *>(QStringLiteral("selectionPreview"));
        QCOMPARE(dialog->palette().color(QPalette::Window), palette.color(QPalette::Window));
        QCOMPARE(preview->palette().color(QPalette::Text), palette.color(QPalette::Text));
        QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("selectionDetails"))->text().contains(QStringLiteral("CRLF：1")));
        const QString directory = qEnvironmentVariable("CALCTABDD_SCREENSHOT_DIR");
        if (!directory.isEmpty())
        {
            QDir().mkpath(directory);
            QCoreApplication::processEvents();
            QVERIFY(dialog->grab().save(directory + (dark ? QStringLiteral("/selection-dark.png") : QStringLiteral("/selection-light.png"))));
        }
    }
    void exportedInterfaceReadsExistingSelection()
    {
        Host host;
        const QString original = QStringLiteral("中文😀前缀 1+2\r\n3+4 后缀");
        host.ordinary->setText(original);
        const QString expected = QStringLiteral("1+2\r\n3+4");
        const int start = original.left(original.indexOf(expected)).toUtf8().size();
        host.ordinary->SendScintilla(QsciScintillaBase::SCI_SETSEL, start, start + expected.toUtf8().size());
        host.ordinary->setReadOnly(true);
        host.ordinary->setModified(false);
        QApplication::clipboard()->setText(QStringLiteral("unchanged clipboard"));
        const auto result = readDocumentSelection(host.tabs);
        QVERIFY2(result.status == DocumentSelection::Status::Selected, qPrintable(result.error));
        QCOMPARE(result.text, expected);
        QCOMPARE(host.ordinary->text(), original);
        QVERIFY(!host.ordinary->isModified());
        QVERIFY(result.readOnly);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("unchanged clipboard"));
    }
    void selectionInterfacesAreProbedOutsideTheHost_data()
    {
        QTest::addColumn<QString>("route");
        QTest::addColumn<QString>("scenario");
        for (const QString &route : {QStringLiteral("input-method"), QStringLiteral("accessible-early"), QStringLiteral("accessible-late")})
            for (const QString &scenario : {QStringLiteral("ascii"), QStringLiteral("unicode"), QStringLiteral("empty"), QStringLiteral("readonly")})
                QTest::newRow(qPrintable(route + QLatin1Char('-') + scenario)) << route << scenario;
    }
    void selectionInterfacesAreProbedOutsideTheHost()
    {
        QFETCH(QString, route);
        QFETCH(QString, scenario);
        QProcess process;
        process.start(QStringLiteral(CALCTABDD_SELECTION_PROBE_PATH),
            {route, scenario, QStringLiteral("-platform"), QStringLiteral("offscreen")});
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        QVERIFY(process.waitForFinished(15000));
        const QByteArray output = process.readAllStandardOutput();
        QVERIFY2(output.startsWith("probe-ready\n") || output.startsWith("probe-ready\r\n"), output.constData());
#ifdef Q_OS_WIN
        if (route == QStringLiteral("input-method"))
        {
            // Pinned InputMethod.cpp casts buffer.data() to sptr_t, then selects
            // SendScintilla(..., long): on LLP64 that truncates the pointer.
            QCOMPARE(process.exitStatus(), QProcess::CrashExit);
            QCOMPARE(static_cast<quint32>(process.exitCode()), quint32(0xc0000005));
            return;
        }
#endif
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        const int newline = output.indexOf('\n');
        const auto json = QJsonDocument::fromJson(output.mid(newline + 1)).object();
        QVERIFY(!json.isEmpty());
        QVERIFY(json.value(QStringLiteral("unchanged")).toBool());
        qInfo().noquote() << route << scenario << QJsonDocument(json).toJson(QJsonDocument::Compact);
        QCOMPARE(json.value(QStringLiteral("native_selected_text")), json.value(QStringLiteral("expected")));
        if (route.startsWith(QStringLiteral("accessible")) &&
            json.value(QStringLiteral("selection_count")).toInt() == 0)
        {
            // Characterization only: a valid interface can miss selection
            // state. A zero count is not proof that the document has none.
            QVERIFY(json.value(QStringLiteral("text")).toString().isEmpty());
        }
        else
            QCOMPARE(json.value(QStringLiteral("text")), json.value(QStringLiteral("expected")));
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
            QVERIFY(second.page()->findChildren<QLabel *>(QStringLiteral("recordResult")).isEmpty());
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
            QCOMPARE(saved.history.answer(), 0.1 + 0.2);
            QCOMPARE(saved.input.text, QStringLiteral("ans+2"));
            QCOMPARE(saved.draft.text, QStringLiteral("原草稿😀"));
            QCOMPARE(readSession(secondPath).history.answer(), 21.0);
            first.openAction()->trigger();
            QVERIFY(first.page()->findChildren<QLabel *>(QStringLiteral("recordResult")).isEmpty());
            QVERIFY(first.input()->toPlainText().isEmpty());
            QVERIFY(selectSession(first, firstPath, true));
            QCOMPARE(first.input()->toPlainText(), QStringLiteral("ans+2"));
            first.page()->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
            QCOMPARE(first.input()->toPlainText(), QStringLiteral("原草稿😀"));
            QCOMPARE(readSession(firstPath).history.answer(), 0.1 + 0.2 + 2);
            QCOMPARE(readSession(secondPath).history.answer(), 21.0);
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
        QCOMPARE(reopened.page()->findChild<QLabel *>(QStringLiteral("recordResult"))->text(), QStringLiteral("= 21"));
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
            QVERIFY(host->page()->findChildren<QLabel *>(QStringLiteral("recordResult")).isEmpty());
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
        QCOMPARE(host.plugins->actions().size(), 4);
        QVERIFY(host.findChild<QAction *>(QStringLiteral("calctabddInspectSelection")));
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
        QCOMPARE(host.findChild<QLabel *>(QStringLiteral("recordResult"))->text(), QStringLiteral("= 5"));
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
        QCOMPARE(first.findChildren<QLabel *>(QStringLiteral("recordResult")).last()->text(), QStringLiteral("= 43"));
        QCOMPARE(second.findChildren<QLabel *>(QStringLiteral("recordResult")).last()->text(), QStringLiteral("= 5"));
        QCOMPARE(first.findChildren<QLabel *>(QStringLiteral("recordNumber")).last()->text(), QStringLiteral("03"));
        first.findChildren<QPushButton *>(QStringLiteral("reuseFormula")).last()->click();
        QTest::keyClick(first.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(first.findChildren<QLabel *>(QStringLiteral("recordResult")).last()->text(), QStringLiteral("= 44"));
        QVERIFY(native->text().isEmpty());
        QVERIFY(!native->isModified());
        first.closeCurrent();
        first.openAction()->trigger();
        submit(first, QStringLiteral("ans"));
        QCOMPARE(first.findChildren<QLabel *>(QStringLiteral("recordResult")).size(), 1);
        QCOMPARE(first.findChild<QLabel *>(QStringLiteral("recordResult"))->text(), QStringLiteral("= 0"));
        QCOMPARE(first.findChild<QLabel *>(QStringLiteral("recordNumber"))->text(), QStringLiteral("01"));
        submit(second, QStringLiteral("ans+1"));
        QCOMPARE(second.findChildren<QLabel *>(QStringLiteral("recordResult")).last()->text(), QStringLiteral("= 6"));
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
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("0.30000000000000004"));
        first.page()->findChild<QAction *>(QStringLiteral("copyCalculation"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("0.1+0.2\n= 0.30000000000000004"));
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
        QCOMPARE(first.findChildren<QLabel *>(QStringLiteral("recordResult")).last()->text(), QStringLiteral("= 4"));
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
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("0.30000000000000004"));
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
        QCOMPARE(first.findChildren<QLabel *>(QStringLiteral("recordResult")).last()->text(), QStringLiteral("= 43"));
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
        QCOMPARE(first.findChildren<QLabel *>(QStringLiteral("recordResult")).last()->text(), QStringLiteral("= 43"));
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
        QCOMPARE(second.findChildren<QLabel *>(QStringLiteral("recordResult")).last()->text(), QStringLiteral("= 102"));
        second.input()->setPlainText(QStringLiteral("second draft"));
        first.findChild<QAction *>(QStringLiteral("calctabddRoute_actioncut"))->trigger();
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("42+9"));
        dialog->activateWindow();
        QTest::mouseClick(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
        QVERIFY(first.page()->findChildren<QLabel *>(QStringLiteral("recordResult")).isEmpty());
        QCOMPARE(first.input()->toPlainText(), QStringLiteral("ans+1"));
        QCOMPARE(second.input()->toPlainText(), QStringLiteral("second draft"));
        QCOMPARE(second.findChildren<QLabel *>(QStringLiteral("recordResult")).size(), 2);
        first.activateWindow();
        first.input()->setFocus();
        QTRY_VERIFY(first.input()->hasFocus());
        QTest::keyClick(first.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(first.findChild<QLabel *>(QStringLiteral("recordResult"))->text(), QStringLiteral("= 1"));
        QCOMPARE(first.findChild<QLabel *>(QStringLiteral("recordNumber"))->text(), QStringLiteral("01"));
        QVERIFY(native->text().isEmpty());
        QVERIFY(!native->isModified());
        QCOMPARE(first.hostEditCalls, 0);
        first.tabs->setCurrentWidget(first.ordinary);
        QVERIFY(first.findChild<QAction *>(QStringLiteral("actionSave"))->isEnabled());
        first.tabs->setCurrentWidget(native);
        first.closeCurrent();
        first.openAction()->trigger();
        QVERIFY(first.page()->findChildren<QLabel *>(QStringLiteral("recordResult")).isEmpty());
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
        QCOMPARE(host.findChild<QLabel *>(QStringLiteral("recordResult"))->text(), QStringLiteral("= 42"));
        QCOMPARE(host.input()->toPlainText(), QStringLiteral("ans+1"));
        host.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        QCOMPARE(host.findChildren<QLabel *>(QStringLiteral("recordResult")).last()->text(), QStringLiteral("= 43"));
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
        QCOMPARE(host->findChild<QLabel *>(QStringLiteral("recordResult"))->text(), QStringLiteral("= 1"));
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
        QCOMPARE(second.findChildren<QLabel *>(QStringLiteral("recordResult")).last()->text(), QStringLiteral("= 102"));
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
        QCOMPARE(first.findChildren<QLabel *>(QStringLiteral("recordResult")).last()->text(), QStringLiteral("= 43"));
        QCOMPARE(second.input()->toPlainText(), QStringLiteral("第二窗口草稿"));
        QCOMPARE(second.findChildren<QLabel *>(QStringLiteral("recordResult")).size(), 2);
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
                QCOMPARE(host->findChildren<QLabel *>(QStringLiteral("recordResult")).last()->text(), QStringLiteral("= 43"));
            }
            else
            {
                QVERIFY(host->page()->findChildren<QLabel *>(QStringLiteral("recordResult")).isEmpty());
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
