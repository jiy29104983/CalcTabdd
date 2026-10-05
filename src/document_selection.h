#pragma once

#include <QPointer>
#include <QString>
#include <QWidget>

class QTabWidget;

// An immediate, read-only snapshot. Byte offsets identify this snapshot only;
// they are not a document revision and must never be used for later writeback.
struct DocumentSelection
{
    enum class Status { Unsupported, Empty, Selected };
    Status status = Status::Unsupported;
    QPointer<QWidget> editor;
    QString sourceName;
    QString text;
    QString error;
    qintptr startByte = 0;
    qintptr endByte = 0;
    bool readOnly = false;
};

DocumentSelection readDocumentSelection(QTabWidget *tabs);
