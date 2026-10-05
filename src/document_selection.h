#pragma once

#include <QPointer>
#include <QString>
#include <QWidget>

class QTabWidget;

// A synchronous snapshot of the current ordinary text tab. No source positions
// or document revision are supplied by this interface; never use it for writeback.
// Text follows the host selection representation, including NUL-to-space and
// rectangular/multiple-selection joining. It is not an exact byte extraction API.
struct DocumentSelection
{
    enum class Status { Unsupported, Empty, Selected };
    Status status = Status::Unsupported;
    QPointer<QWidget> editor;
    QString sourceName;
    QString text;
};

DocumentSelection readDocumentSelection(QTabWidget *tabs);
