#pragma once

#include <QMenu>
#include <QString>
#include <functional>

class QsciScintilla;

// Binary layout of notepad-- pluginGl.h at 91105f68. Do not reorder fields.
struct NddProcData
{
    QString pluginName;
    QString filePath;
    QString comment;
    QString version;
    QString author;
    int menuType = 0;
    QMenu *rootMenu = nullptr;
    QAction *action = nullptr;
};
using NddGetCurrentEditor = std::function<QsciScintilla *(QWidget *)>;
using NddHostCallback = std::function<bool(QWidget *, int, void *)>;

#if defined(Q_OS_WIN)
#define CALCTABDD_EXPORT __declspec(dllexport)
#else
#define CALCTABDD_EXPORT __attribute__((visibility("default")))
#endif
