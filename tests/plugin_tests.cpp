#include "ndd_plugin_api.h"
#include "calculation_session.h"
#include "document_selection.h"
#include <Qsci/qsciscintilla.h>
#include <plugin.h>
#include <QApplication>
#include <QAbstractItemView>
#include <QCompleter>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QTemporaryDir>
#include <QLineEdit>
#include <QInputMethodEvent>
#include <QToolButton>
#include <QTextBrowser>
#include <QClipboard>
#include <QMimeData>
#include <QScrollBar>
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
    int getterCalls = 0;
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
        ordinary->setText(QStringLiteral("普通文档，不得改动"));
        ordinary->setModified(false);
        tabs->addTab(ordinary, QStringLiteral("普通文档"));
        statusBar();
        resize(960, 740);
        show();
    }
    NddGetCurrentEditor getter()
    {
        return [this](QWidget *window) {
            ++getterCalls;
            if (window != this) return static_cast<QsciScintilla *>(nullptr);
            auto *editor = qobject_cast<QsciScintilla *>(tabs->currentWidget());
            // Match CCNotePad::getCurEditView: read-only documents are rejected.
            return editor && !editor->isReadOnly() ? editor : nullptr;
        };
    }
    NddHostCallback callback()
    {
        return [this](QWidget *window, int command, void *) {
            if (window != this || command != 1 || failCreate) return false;
            auto *editor = new QsciScintilla(tabs);
            editor->setProperty("type", 1);
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
    void documentSelectionSnapshot_data()
    {
        QTest::addColumn<QString>("prefix");
        QTest::addColumn<QString>("selected");
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<bool>("reverse");
        QTest::addColumn<bool>("readOnly");
        QTest::newRow("ascii") << QStringLiteral("before ") << QStringLiteral("1+2*3") << QStringLiteral(" after") << false << false;
        QTest::newRow("chinese-prefix") << QStringLiteral("说明：单价 ") << QStringLiteral("1299*0.85") << QStringLiteral(" 元") << false << false;
        QTest::newRow("unicode-and-reverse") << QStringLiteral("前😀缀 ") << QStringLiteral("中文😀 × ÷ −") << QStringLiteral("尾部") << true << false;
        QTest::newRow("lf") << QStringLiteral("前缀\n") << QStringLiteral("1+2\n3+4\n") << QStringLiteral("末尾") << false << false;
        QTest::newRow("crlf-reverse") << QStringLiteral("说明\r\n") << QStringLiteral("1+2\r\n3+4\r\n") << QStringLiteral("末尾") << true << false;
        QTest::newRow("cr") << QStringLiteral("说明\r") << QStringLiteral("1+2\r3+4") << QString() << false << false;
        QTest::newRow("mixed-eol") << QStringLiteral("前缀") << QStringLiteral("a\r\nb\nc\rd") << QStringLiteral("后缀") << false << false;
        QTest::newRow("whitespace") << QStringLiteral("前缀") << QStringLiteral(" \t\n ") << QStringLiteral("后缀") << false << false;
        QTest::newRow("no-selection") << QStringLiteral("整段中文😀 1+2") << QString() << QStringLiteral("后缀") << false << false;
        QTest::newRow("empty-document") << QString() << QString() << QString() << false << false;
        QTest::newRow("read-only") << QStringLiteral("只读前缀😀") << QStringLiteral("2^3\r\n中文") << QStringLiteral("后缀") << true << true;
        QTest::newRow("read-only-empty") << QStringLiteral("只读文档") << QString() << QStringLiteral("后缀") << false << true;
        QTest::newRow("beyond-stack-buffer") << QStringLiteral("中文前缀") << QString(5000, QChar(0x4e2d)) << QStringLiteral("尾部") << false << false;
        QTest::newRow("embedded-nul") << QStringLiteral("前缀") << (QStringLiteral("1+") + QChar::Null + QStringLiteral("2")) << QStringLiteral("后缀") << false << false;
        QTest::newRow("trailing-nul") << QStringLiteral("前缀") << (QStringLiteral("12") + QChar::Null) << QStringLiteral("后缀") << false << true;
        QTest::newRow("only-nul") << QStringLiteral("前缀") << QString(QChar::Null) << QStringLiteral("后缀") << false << false;
    }
    void documentSelectionSnapshot()
    {
        QFETCH(QString, prefix);
        QFETCH(QString, selected);
        QFETCH(QString, suffix);
        QFETCH(bool, reverse);
        QFETCH(bool, readOnly);
        Host host;
        QCOMPARE(initialize(host), 0);
        auto *editor = host.ordinary;
        editor->setUtf8(true);
        editor->clear();
        editor->SendScintilla(QsciScintillaBase::SCI_EMPTYUNDOBUFFER);
        const QByteArray bytes = (prefix + selected + suffix).toUtf8();
        editor->SendScintilla(QsciScintillaBase::SCI_ADDTEXT,
            static_cast<unsigned long>(bytes.size()), bytes.constData());
        const int start = prefix.toUtf8().size();
        const int end = start + selected.toUtf8().size();
        editor->SendScintilla(QsciScintillaBase::SCI_SETSEL, reverse ? end : start, reverse ? start : end);
        if (readOnly) editor->setModified(false);
        editor->setReadOnly(readOnly);
        const QString original = editor->text();
        const bool modified = editor->isModified();
        const bool undo = editor->isUndoAvailable();
        const bool redo = editor->isRedoAvailable();
        const long anchor = editor->SendScintilla(QsciScintillaBase::SCI_GETANCHOR);
        const long caret = editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS);
        const int scroll = editor->verticalScrollBar()->value();
        QWidget *const active = host.tabs->currentWidget();
        if (readOnly) QVERIFY(!host.getter()(&host));
        host.getterCalls = 0;
        // Preserve every MIME format, not only clipboard text.
        auto *mime = new QMimeData;
        mime->setText(QStringLiteral("原剪贴板😀"));
        mime->setHtml(QStringLiteral("<b>原剪贴板</b>"));
        mime->setData(QStringLiteral("application/x-calctabdd-test"), QByteArray("a\0b", 3));
        QApplication::clipboard()->setMimeData(mime);
        QSignalSpy clipboardChanges(QApplication::clipboard(), &QClipboard::dataChanged);
        QSignalSpy textChanges(editor, &QsciScintilla::textChanged);
        QSignalSpy selectionChanges(editor, &QsciScintilla::selectionChanged);
        const auto snapshot = readDocumentSelection(host.tabs);
        QCOMPARE(snapshot.status, selected.isEmpty() ? DocumentSelection::Status::Empty : DocumentSelection::Status::Selected);
        QString expected = selected;
        // The pinned Scintilla SelectionText::Copy converts NULs to spaces.
        expected.replace(QChar::Null, QLatin1Char(' '));
        QCOMPARE(snapshot.text, expected);
        QCOMPARE(snapshot.editor.data(), static_cast<QWidget *>(editor));
        QCOMPARE(snapshot.sourceName, QStringLiteral("普通文档"));
        // Exercise the same path from the actual loaded plugin's menu.
        host.findChild<QAction *>(QStringLiteral("calctabddInspectSelection"))->trigger();
        auto *dialog = host.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog"));
        if (selected.isEmpty())
        {
            QVERIFY(!dialog);
            QVERIFY(host.statusBar()->currentMessage().contains(QStringLiteral("没有选中文字")));
        }
        else
        {
            QVERIFY(dialog && dialog->isVisible());
            auto *preview = dialog->findChild<QPlainTextEdit *>(QStringLiteral("selectionPreview"));
            QVERIFY(preview && preview->isReadOnly());
            QString displayed = expected;
            displayed.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            displayed.replace(QLatin1Char('\r'), QLatin1Char('\n'));
            QCOMPARE(preview->toPlainText(), displayed);
            const int crlf = selected.count(QStringLiteral("\r\n"));
            QCOMPARE(dialog->findChild<QLabel *>(QStringLiteral("selectionDetails"))->text(),
                QStringLiteral("UTF-16 长度：%1；换行 CRLF：%2，LF：%3，CR：%4")
                    .arg(selected.size()).arg(crlf).arg(selected.count(QLatin1Char('\n')) - crlf)
                    .arg(selected.count(QLatin1Char('\r')) - crlf));
            dialog->reject();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        QCOMPARE(host.tabs->currentWidget(), active);
        QCOMPARE(host.creations, 0);
        QCOMPARE(host.hostEditCalls, 0);
        QCOMPARE(host.getterCalls, 0);
        QCOMPARE(editor->text(), original);
        QCOMPARE(editor->isReadOnly(), readOnly);
        QCOMPARE(editor->isModified(), modified);
        QCOMPARE(editor->isUndoAvailable(), undo);
        QCOMPARE(editor->isRedoAvailable(), redo);
        QCOMPARE(editor->SendScintilla(QsciScintillaBase::SCI_GETANCHOR), anchor);
        QCOMPARE(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS), caret);
        QCOMPARE(editor->verticalScrollBar()->value(), scroll);
        QCOMPARE(textChanges.count(), 0);
        QCOMPARE(selectionChanges.count(), 0);
        QCOMPARE(clipboardChanges.count(), 0);
        const QMimeData *after = QApplication::clipboard()->mimeData();
        QCOMPARE(after->text(), QStringLiteral("原剪贴板😀"));
        QCOMPARE(after->html(), QStringLiteral("<b>原剪贴板</b>"));
        QCOMPARE(after->data(QStringLiteral("application/x-calctabdd-test")), QByteArray("a\0b", 3));
        editor->setReadOnly(false);
        if (undo)
        {
            editor->undo();
            QVERIFY(editor->text().isEmpty());
            editor->redo();
            QCOMPARE(editor->text(), original);
        }
    }
    void documentSelectionHostJoiningRules_data()
    {
        QTest::addColumn<bool>("rectangle");
        QTest::newRow("rectangle") << true;
        QTest::newRow("multiple") << false;
    }
    void documentSelectionHostJoiningRules()
    {
        QFETCH(bool, rectangle);
        Host host;
        QCOMPARE(initialize(host), 0);
        auto *editor = host.ordinary;
        editor->setUtf8(true);
        editor->setText(QStringLiteral("x12z\nx34z\n"));
        editor->setEolMode(QsciScintilla::EolUnix);
        if (rectangle)
        {
            editor->SendScintilla(QsciScintillaBase::SCI_SETSELECTIONMODE, QsciScintillaBase::SC_SEL_RECTANGLE);
            editor->SendScintilla(QsciScintillaBase::SCI_SETRECTANGULARSELECTIONANCHOR, 1);
            editor->SendScintilla(QsciScintillaBase::SCI_SETRECTANGULARSELECTIONCARET, 8);
        }
        else
        {
            editor->SendScintilla(QsciScintillaBase::SCI_SETMULTIPLESELECTION, 1);
            editor->SendScintilla(QsciScintillaBase::SCI_SETSELECTION, 3, 1);
            editor->SendScintilla(QsciScintillaBase::SCI_ADDSELECTION, 8, 6);
        }
        const auto mode = editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONMODE);
        const int count = static_cast<int>(editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONS));
        QCOMPARE(count, 2);
        QVector<long> positions;
        for (int i = 0; i < count; ++i)
        {
            positions << editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONNANCHOR, i);
            positions << editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONNCARET, i);
        }
        QApplication::clipboard()->setText(QStringLiteral("clipboard"));
        QSignalSpy changed(QApplication::clipboard(), &QClipboard::dataChanged);
        const auto snapshot = readDocumentSelection(host.tabs);
        QCOMPARE(snapshot.text, rectangle ? QStringLiteral("12\n34\n") : QStringLiteral("1234"));
        QCOMPARE(editor->text(), QStringLiteral("x12z\nx34z\n"));
        QCOMPARE(editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONMODE), mode);
        QCOMPARE(editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONS), static_cast<long>(count));
        for (int i = 0; i < count; ++i)
        {
            QCOMPARE(editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONNANCHOR, i), positions.at(i * 2));
            QCOMPARE(editor->SendScintilla(QsciScintillaBase::SCI_GETSELECTIONNCARET, i), positions.at(i * 2 + 1));
        }
        QCOMPARE(changed.count(), 0);
    }
    void documentSelectionPreviewThemes_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void documentSelectionPreviewThemes()
    {
        QFETCH(bool, dark);
        Host host;
        QPalette palette = host.palette();
        palette.setColor(QPalette::Window, dark ? QColor("#252932") : QColor("#f3f4f6"));
        palette.setColor(QPalette::Base, dark ? QColor("#171b24") : QColor("#ffffff"));
        palette.setColor(QPalette::Text, dark ? QColor("#eef2f6") : QColor("#20252b"));
        palette.setColor(QPalette::WindowText, palette.color(QPalette::Text));
        palette.setColor(QPalette::Button, palette.color(QPalette::Window));
        palette.setColor(QPalette::ButtonText, palette.color(QPalette::Text));
        host.setPalette(palette);
        QCOMPARE(initialize(host), 0);
        host.ordinary->setUtf8(true);
        host.ordinary->setText(QStringLiteral("报价说明：\r\n(1299+899)*0.85\r\n数量 × 单价\r\n"));
        host.ordinary->setSelection(1, 0, 3, 0);
        host.ordinary->setReadOnly(true);
        host.findChild<QAction *>(QStringLiteral("calctabddInspectSelection"))->trigger();
        auto *dialog = host.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog"));
        QVERIFY(dialog && dialog->isVisible());
        auto *preview = dialog->findChild<QPlainTextEdit *>();
        QCOMPARE(preview->palette().color(QPalette::Text), palette.color(QPalette::Text));
        QCOMPARE(preview->palette().color(QPalette::Base), palette.color(QPalette::Base));
        QVERIFY(preview->height() > 100);
        const QString directory = qEnvironmentVariable("CALCTABDD_SCREENSHOT_DIR");
        if (!directory.isEmpty())
        {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(dialog->grab().save(directory + (dark ? QStringLiteral("/selection-dark.png") : QStringLiteral("/selection-light.png"))));
        }
    }
    void documentSelectionRejectsUnsupportedTabs()
    {
        QCOMPARE(readDocumentSelection(nullptr).status, DocumentSelection::Status::Unsupported);
        QTabWidget empty;
        QCOMPARE(readDocumentSelection(&empty).status, DocumentSelection::Status::Unsupported);
        Host host;
        QCOMPARE(initialize(host), 0);
        host.ordinary->selectAll();
        for (const QVariant type : {QVariant(), QVariant(2), QVariant(3), QVariant(4), QVariant(5), QVariant(6)})
        {
            host.ordinary->setProperty("type", type);
            QCOMPARE(readDocumentSelection(host.tabs).status, DocumentSelection::Status::Unsupported);
            host.findChild<QAction *>(QStringLiteral("calctabddInspectSelection"))->trigger();
            QVERIFY(!host.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog")));
        }
        host.ordinary->setProperty("type", 1);
        host.openAction()->trigger();
        host.input()->setPlainText(QStringLiteral("123+456"));
        host.input()->selectAll();
        QCOMPARE(readDocumentSelection(host.tabs).status, DocumentSelection::Status::Unsupported);
        host.findChild<QAction *>(QStringLiteral("calctabddInspectSelection"))->trigger();
        QVERIFY(!host.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog")));
        QCOMPARE(host.input()->toPlainText(), QStringLiteral("123+456"));
        QCOMPARE(host.input()->textCursor().selectedText(), QStringLiteral("123+456"));
        auto *other = new QPlainTextEdit(host.tabs);
        other->setProperty("type", 1);
        other->setPlainText(QStringLiteral("other tab"));
        other->selectAll();
        host.tabs->setCurrentIndex(host.tabs->addTab(other, QStringLiteral("其他")));
        QCOMPARE(readDocumentSelection(host.tabs).status, DocumentSelection::Status::Unsupported);
    }
    void documentSelectionUsesOwningWindowAndCurrentTab()
    {
        Host first;
        Host second;
        QCOMPARE(initialize(first), 0);
        QCOMPARE(initialize(second), 0);
        first.ordinary->setText(QStringLiteral("first 1+2"));
        second.ordinary->setText(QStringLiteral("second 3+4"));
        first.ordinary->selectAll();
        second.ordinary->selectAll();
        second.activateWindow();
        auto *action = first.findChild<QAction *>(QStringLiteral("calctabddInspectSelection"));
        action->trigger();
        QPointer<QDialog> firstDialog = first.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog"));
        QVERIFY(firstDialog);
        QCOMPARE(firstDialog->findChild<QPlainTextEdit *>()->toPlainText(), QStringLiteral("first 1+2"));
        second.findChild<QAction *>(QStringLiteral("calctabddInspectSelection"))->trigger();
        QPointer<QDialog> secondDialog = second.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog"));
        QVERIFY(secondDialog);
        QCOMPARE(secondDialog->findChild<QPlainTextEdit *>()->toPlainText(), QStringLiteral("second 3+4"));
        auto *next = new QsciScintilla(first.tabs);
        next->setProperty("type", 1);
        next->setText(QStringLiteral("new tab 5+6"));
        next->selectAll();
        first.tabs->setCurrentIndex(first.tabs->addTab(next, QStringLiteral("<b>新标签</b>")));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(firstDialog.isNull());
        QVERIFY(secondDialog && secondDialog->isVisible());
        action->trigger();
        auto *current = first.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog"));
        QVERIFY(current);
        QCOMPARE(current->findChild<QPlainTextEdit *>()->toPlainText(), QStringLiteral("new tab 5+6"));
        QCOMPARE(current->findChild<QLabel *>(QStringLiteral("selectionSource"))->textFormat(), Qt::PlainText);
        QCOMPARE(current->findChild<QLabel *>(QStringLiteral("selectionSource"))->text(), QStringLiteral("来源：<b>新标签</b>"));
        current->reject();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        next->setText(QStringLiteral("changed 7+8"));
        next->selectAll();
        action->trigger();
        current = first.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog"));
        QCOMPARE(current->findChild<QPlainTextEdit *>()->toPlainText(), QStringLiteral("changed 7+8"));
        QCOMPARE(first.creations, 0);
        QCOMPARE(second.creations, 0);
    }
    void documentSelectionPreservesExistingCalculation()
    {
        Host host;
        QCOMPARE(initialize(host), 0);
        host.openAction()->trigger();
        QWidget *calculator = host.tabs->currentWidget();
        host.input()->setPlainText(QStringLiteral("42"));
        host.page()->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        host.input()->setPlainText(QStringLiteral("未提交的草稿"));
        host.input()->selectAll();
        host.tabs->setCurrentWidget(host.ordinary);
        host.ordinary->selectAll();
        host.findChild<QAction *>(QStringLiteral("calctabddInspectSelection"))->trigger();
        QPointer<QDialog> dialog = host.findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog"));
        QVERIFY(dialog && dialog->isVisible());
        host.tabs->setCurrentWidget(calculator);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog.isNull());
        QCOMPARE(host.creations, 1);
        QCOMPARE(host.page()->findChildren<QLabel *>(QStringLiteral("recordResult")).size(), 1);
        QCOMPARE(host.input()->toPlainText(), QStringLiteral("未提交的草稿"));
        QCOMPARE(host.input()->textCursor().selectedText(), QStringLiteral("未提交的草稿"));
        host.input()->setPlainText(QStringLiteral("ans+1"));
        host.page()->findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        const auto results = host.page()->findChildren<QLabel *>(QStringLiteral("recordResult"));
        QCOMPARE(results.size(), 2);
        QCOMPARE(results.last()->text(), QStringLiteral("= 43"));
        QCOMPARE(host.hostEditCalls, 0);
    }
    void documentSelectionDialogLifetime_data()
    {
        QTest::addColumn<bool>("closeWindow");
        QTest::newRow("close-tab") << false;
        QTest::newRow("close-window") << true;
    }
    void documentSelectionDialogLifetime()
    {
        QFETCH(bool, closeWindow);
        auto *host = new Host;
        QCOMPARE(initialize(*host), 0);
        host->ordinary->selectAll();
        const auto snapshot = readDocumentSelection(host->tabs);
        host->findChild<QAction *>(QStringLiteral("calctabddInspectSelection"))->trigger();
        QPointer<QDialog> dialog = host->findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog"));
        QVERIFY(dialog && dialog->isVisible());
        if (closeWindow) { delete host; host = nullptr; }
        else host->closeCurrent();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog.isNull());
        QVERIFY(snapshot.editor.isNull());
        QCOMPARE(snapshot.text, QStringLiteral("普通文档，不得改动"));
        delete host;
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
        QCOMPARE(host.findChildren<QAction *>(QStringLiteral("calctabddInspectSelection")).size(), 1);
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
