// Test-only plugin. Runs inside an extracted, official Windows host release.
// It is never packaged with CalcTabdd and only touches a newly created tab.
#include "document_selection.h"
#include "ndd_plugin_api.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTimer>
#include <QVariant>

namespace
{
void runProbe(QWidget *host, const NddHostCallback &callback)
{
    QJsonArray checks;
    bool passed = true;
    auto check = [&](const QString &name, bool ok, const QString &detail = QString()) {
        checks.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("passed"), ok},
                                 {QStringLiteral("detail"), detail}});
        passed = passed && ok;
    };
    auto *tabs = host->findChild<QTabWidget *>(QStringLiteral("editTabWidget"));
    QVariant name;
    const bool created = tabs && callback(host, 1, &name);
    QWidget *editor = created ? tabs->currentWidget() : nullptr;
    check(QStringLiteral("host_created_native_tab"), editor && editor->inherits("QsciScintilla"));
    if (editor)
    {
        auto setBool = [editor](const char *method, bool value) {
            return QMetaObject::invokeMethod(editor, method, Qt::DirectConnection, Q_ARG(bool, value));
        };
        const QString source = QStringLiteral("中文😀前缀 1+2\r\n3+4 后缀");
        check(QStringLiteral("set_utf8"), setBool("setUtf8", true));
        check(QStringLiteral("set_text"), QMetaObject::invokeMethod(editor, "setText", Qt::DirectConnection, Q_ARG(QString, source)));
        // QScintilla line/index positions count Unicode scalars, not UTF-8 bytes.
        const int firstIndex = QStringLiteral("中文😀前缀 ").toUcs4().size();
        check(QStringLiteral("select_multiline"), QMetaObject::invokeMethod(editor, "setSelection", Qt::DirectConnection,
            Q_ARG(int, 0), Q_ARG(int, firstIndex), Q_ARG(int, 1), Q_ARG(int, 3)));
        check(QStringLiteral("set_clean"), setBool("setModified", false));
        QApplication::clipboard()->setText(QStringLiteral("host probe clipboard"));
        const QString expected = QStringLiteral("1+2\r\n3+4");
        const auto selected = readDocumentSelection(tabs);
        check(QStringLiteral("read_existing_unicode_multiline"), selected.status == DocumentSelection::Status::Selected &&
              selected.text == expected, selected.error + selected.text);
        check(QStringLiteral("set_readonly"), setBool("setReadOnly", true));
        const auto readOnly = readDocumentSelection(tabs);
        check(QStringLiteral("read_readonly_selection"), readOnly.status == DocumentSelection::Status::Selected &&
              readOnly.text == expected && readOnly.readOnly, readOnly.error);
        auto *action = host->findChild<QAction *>(QStringLiteral("calctabddInspectSelection"));
        check(QStringLiteral("production_menu_installed"), action != nullptr);
        if (action)
        {
            action->trigger();
            auto *dialog = host->findChild<QDialog *>(QStringLiteral("calctabddSelectionDialog"));
            auto *preview = dialog ? dialog->findChild<QPlainTextEdit *>(QStringLiteral("selectionPreview")) : nullptr;
            check(QStringLiteral("production_dll_preview"), preview && preview->isReadOnly() &&
                  preview->toPlainText() == QStringLiteral("1+2\n3+4"));
            if (dialog) dialog->reject();
        }
        const auto after = readDocumentSelection(tabs);
        check(QStringLiteral("selection_preserved"), after.text == selected.text && after.startByte == selected.startByte &&
              after.endByte == selected.endByte && after.readOnly);
        check(QStringLiteral("clipboard_preserved"), QApplication::clipboard()->text() == QStringLiteral("host probe clipboard"));
        check(QStringLiteral("clear_selection"), QMetaObject::invokeMethod(editor, "setSelection", Qt::DirectConnection,
            Q_ARG(int, 1), Q_ARG(int, 2), Q_ARG(int, 1), Q_ARG(int, 2)));
        check(QStringLiteral("empty_selection"), readDocumentSelection(tabs).status == DocumentSelection::Status::Empty);
        // Reading the whole buffer after the checks also proves it was not modified.
        QMetaObject::invokeMethod(editor, "selectAll", Qt::DirectConnection, Q_ARG(bool, true));
        check(QStringLiteral("source_preserved"), readDocumentSelection(tabs).text == source);
        setBool("setReadOnly", false);
        QMetaObject::invokeMethod(editor, "clear", Qt::DirectConnection);
        setBool("setModified", false);
    }
    const QString output = qEnvironmentVariable("CALCTABDD_HOST_PROBE_OUTPUT");
    QFile file(output);
    if (!output.isEmpty() && file.open(QIODevice::WriteOnly))
    {
        const QJsonObject result{{QStringLiteral("passed"), passed}, {QStringLiteral("checks"), checks},
            {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
            {QStringLiteral("host"), QCoreApplication::applicationFilePath()}};
        file.write(QJsonDocument(result).toJson());
        file.close();
    }
    else passed = false;
    QCoreApplication::exit(passed ? 0 : 7);
}
}
extern "C" {
CALCTABDD_EXPORT bool NDD_PROC_IDENTIFY(NddProcData *data)
{
    if (!data) return false;
    data->pluginName = QStringLiteral("CalcTabdd release verification");
    data->menuType = 1;
    return true;
}
CALCTABDD_EXPORT int NDD_PROC_MAIN(QWidget *host, const QString &, NddGetCurrentEditor,
                                  NddHostCallback callback, NddProcData *)
{
    if (!host || !callback || qEnvironmentVariableIsEmpty("CALCTABDD_HOST_PROBE_OUTPUT")) return -1;
    QTimer::singleShot(1000, host, [host, callback]() { runProbe(host, callback); });
    return 0;
}
}
