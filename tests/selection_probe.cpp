// Isolate candidate host APIs: the pinned input-method query truncates a buffer
// pointer on Windows x64. Never invoke it in the production plugin process.
#include <Qsci/qsciscintilla.h>
#include "document_selection.h"
#include <QTabWidget>
#include <QAccessible>
#include <QApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    const QStringList args = application.arguments();
    if (args.size() < 3) return 2;
    const QString route = args.at(1);
    const QString scenario = args.at(2);
    QTabWidget tabs;
    QsciScintilla editor;
    editor.setProperty("type", 1);
    tabs.addTab(&editor, QStringLiteral("probe"));
    editor.setUtf8(true);
    editor.show();
    application.processEvents();
    QAccessibleTextInterface *textInterface = nullptr;
    if (route == QStringLiteral("accessible-early"))
    {
        auto *accessible = QAccessible::queryAccessibleInterface(&editor);
        textInterface = accessible ? accessible->textInterface() : nullptr;
        if (!textInterface) return 3;
    }
    const bool empty = scenario == QStringLiteral("empty");
    const QString original = scenario == QStringLiteral("unicode")
        ? QStringLiteral("中文😀前缀 1+2\r\n3+4 后缀") : QStringLiteral("before 1+2 after");
    editor.setText(original);
    const QString selected = scenario == QStringLiteral("unicode")
        ? QStringLiteral("1+2\r\n3+4") : QStringLiteral("1+2");
    const int start = original.left(original.indexOf(selected)).toUtf8().size();
    const int end = empty ? start : start + selected.toUtf8().size();
    editor.SendScintilla(QsciScintillaBase::SCI_SETSEL, start, end);
    editor.setModified(false);
    editor.setReadOnly(scenario == QStringLiteral("readonly"));
    application.processEvents();
    QApplication::clipboard()->setText(QStringLiteral("clipboard marker"));
    const long anchor = editor.SendScintilla(QsciScintillaBase::SCI_GETANCHOR);
    const long caret = editor.SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS);
    if (route == QStringLiteral("accessible-late"))
    {
        auto *accessible = QAccessible::queryAccessibleInterface(&editor);
        textInterface = accessible ? accessible->textInterface() : nullptr;
        if (!textInterface) return 3;
    }

    // Flushed marker proves a Windows access violation happens within the query,
    // after setup, and not during executable/DLL loading or editor construction.
    QTextStream output(stdout);
    output << "probe-ready\n";
    output.flush();
    QString actual;
    int selectionCount = -1;
    if (route == QStringLiteral("exported-unavailable"))
    {
        // This executable deliberately does not export the editor entry points.
        // Even a real QScintilla object must be rejected if ownership/API cannot
        // be verified. No input-method fallback is allowed.
        const auto selection = readDocumentSelection(&tabs);
        output << (selection.status == DocumentSelection::Status::Unsupported &&
                   !selection.error.isEmpty() ? "unsupported\n" : "unexpected\n");
        output.flush();
        return selection.status == DocumentSelection::Status::Unsupported ? 0 : 6;
    }
    if (route == QStringLiteral("input-method"))
    {
        QWidget *widget = &editor;
        actual = widget->inputMethodQuery(Qt::ImCurrentSelection).toString();
        if (actual.endsWith(QChar::Null)) actual.chop(1);
    }
    else if (textInterface)
    {
        selectionCount = textInterface->selectionCount();
        if (selectionCount)
        {
            int first = 0, last = 0;
            textInterface->selection(0, &first, &last);
            actual = textInterface->text(first, last);
        }
    }
    else return 4;
    const bool unchanged = editor.text() == original && !editor.isModified() &&
        editor.isReadOnly() == (scenario == QStringLiteral("readonly")) &&
        editor.SendScintilla(QsciScintillaBase::SCI_GETANCHOR) == anchor &&
        editor.SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS) == caret &&
        QApplication::clipboard()->text() == QStringLiteral("clipboard marker");
    const QJsonObject result{
        {QStringLiteral("text"), actual}, {QStringLiteral("expected"), empty ? QString() : selected},
        {QStringLiteral("selection_count"), selectionCount}, {QStringLiteral("unchanged"), unchanged},
        {QStringLiteral("accessible_character_count"), textInterface ? textInterface->characterCount() : -1},
        {QStringLiteral("native_selected_text"), editor.selectedText()}};
    output << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
    output.flush();
    return unchanged ? 0 : 5;
}
