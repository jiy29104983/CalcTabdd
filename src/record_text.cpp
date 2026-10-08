#include "record_text.h"
#include <QAbstractTextDocumentLayout>
#include <QResizeEvent>
#include <QTextDocument>
#include <QtMath>

RecordText::RecordText(const QString &text, QWidget *parent) : QTextBrowser(parent)
{
    setFrameShape(QFrame::NoFrame);
    setReadOnly(true);
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setUndoRedoEnabled(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    document()->setDocumentMargin(0);
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, [this]() { updateHeight(); });
    setPlainText(text);
}

void RecordText::setSelection(int start, int length)
{
    QTextCursor cursor(document());
    cursor.setPosition(qBound(0, start, toPlainText().size()));
    cursor.setPosition(qBound(0, start + length, toPlainText().size()), QTextCursor::KeepAnchor);
    setTextCursor(cursor);
}

void RecordText::resizeEvent(QResizeEvent *event)
{
    QTextBrowser::resizeEvent(event);
    updateHeight();
}

void RecordText::updateHeight()
{
    const int required = qMax(fontMetrics().height(), qCeil(document()->documentLayout()->documentSize().height())) + 2 * frameWidth();
    if (height() != required || minimumHeight() != required) setFixedHeight(required);
}
