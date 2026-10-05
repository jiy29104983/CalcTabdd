#include "document_selection.h"

#include <QAbstractScrollArea>
#include <QTabWidget>
#include <QVariant>

DocumentSelection readDocumentSelection(QTabWidget *tabs)
{
    DocumentSelection result;
    if (!tabs) return result;
    QWidget *editor = tabs->currentWidget();
    if (!editor || !qobject_cast<QAbstractScrollArea *>(editor) ||
        !editor->inherits("QsciScintilla") ||
        editor->property("calctabddNativeTab").toBool()) return result;
    // The pinned host uses type=1 for ordinary text, including read-only text.
    // Hex and paged large-file tabs are outside this reader's contract.
    if (editor->property("type").toInt() != 1) return result;

    // getCurEditView() rejects read-only documents. Resolve the tab in this
    // window instead, and dispatch through QWidget's Qt ABI without linking
    // QScintilla or casting its opaque callback pointer across DLL boundaries.
    const QVariant value = editor->inputMethodQuery(Qt::ImCurrentSelection);
    if (value.userType() != QMetaType::QString) return result;
    result.text = value.toString();
    // InputMethod.cpp at host 91105f68 includes SCI_GETSELTEXT's terminator in
    // its explicit-length QString conversion. Remove exactly that one NUL;
    // preserve all other returned characters. Scintilla itself converts document
    // NULs to spaces and joins multiple selections (see SelectionText::Copy).
    if (result.text.endsWith(QChar::Null)) result.text.chop(1);
    result.editor = editor;
    result.sourceName = tabs->tabText(tabs->currentIndex());
    result.status = result.text.isEmpty() ? DocumentSelection::Status::Empty
                                         : DocumentSelection::Status::Selected;
    return result;
}
