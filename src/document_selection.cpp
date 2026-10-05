#include "document_selection.h"

#include <QAbstractScrollArea>
#include <QTabWidget>
#include <QThread>
#include <QVariant>
#include <cstring>
#include <limits>

#if defined(Q_OS_WIN) && defined(_WIN64) && defined(_MSC_VER)
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <dlfcn.h>
#endif

namespace
{
// Public Scintilla message ABI. No QScintilla object layout or private fields
// are duplicated. Sci_PositionCR is long, including on Windows x64.
struct TextRange
{
    struct { long first; long last; } range;
    char *text;
};
using PointerQuery = void *(*)(const void *, unsigned int);
using DirectFunction = qintptr (*)(qintptr, unsigned int, quintptr, qintptr);

struct Reader
{
    DirectFunction function = nullptr;
    qintptr document = 0;
    qintptr get(unsigned int message, quintptr first = 0, qintptr second = 0) const
    {
        return function(document, message, first, second);
    }
};

Reader readerFor(QWidget *editor)
{
    const QMetaObject *base = editor->metaObject();
    while (base && std::strcmp(base->className(), "QsciScintillaBase") != 0)
        base = base->superClass();
    if (!base) return {};
    // qt_metacast adjusts the public base pointer; do not assume inheritance
    // offsets or reinterpret the opaque getCurEditView callback result.
    const void *instance = editor->qt_metacast("QsciScintillaBase");
    if (!instance) return {};
    PointerQuery pointerQuery = nullptr;
#if defined(Q_OS_WIN) && defined(_WIN64) && defined(_MSC_VER)
    HMODULE module = nullptr;
    // Resolve ONLY in the module that owns this object's base metaobject.
    // No LoadLibrary, filename search, bundled editor DLL or library replacement.
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(base), &module)) return {};
    if (reinterpret_cast<const void *>(GetProcAddress(module,
            "?staticMetaObject@QsciScintillaBase@@2UQMetaObject@@B")) != base) return {};
    // MSVC x64 uses the same register ABI for an explicit first 'this' pointer.
    // This public, nonvirtual method takes/returns pointer-width values.
    pointerQuery = reinterpret_cast<PointerQuery>(GetProcAddress(module,
            "?SendScintillaPtrResult@QsciScintillaBase@@QEBAPEAXI@Z"));
#elif defined(Q_OS_LINUX)
    // Also supports the exported native dependency in Linux integration tests.
    // A hidden/static host without these public symbols fails closed.
    if (dlsym(RTLD_DEFAULT, "_ZN17QsciScintillaBase16staticMetaObjectE") != base) return {};
    auto *symbol = dlsym(RTLD_DEFAULT, "_ZNK17QsciScintillaBase22SendScintillaPtrResultEj");
    Dl_info owner{}, methodOwner{};
    if (!symbol || !dladdr(base, &owner) || !dladdr(symbol, &methodOwner) ||
        owner.dli_fbase != methodOwner.dli_fbase) return {};
    pointerQuery = reinterpret_cast<PointerQuery>(symbol);
#endif
    if (!pointerQuery) return {};
    Reader reader;
    reader.function = reinterpret_cast<DirectFunction>(pointerQuery(instance, 2184)); // SCI_GETDIRECTFUNCTION
    reader.document = reinterpret_cast<qintptr>(pointerQuery(instance, 2185)); // SCI_GETDIRECTPOINTER
    if (!reader.function || !reader.document) return {};
    return reader;
}
}

DocumentSelection readDocumentSelection(QTabWidget *tabs)
{
    DocumentSelection result;
    result.error = QStringLiteral("请切换到普通文本文档后查看选区；计算器、二进制及分页大文件标签不支持。");
    if (!tabs || tabs->thread() != QThread::currentThread()) return result;
    QWidget *editor = tabs->currentWidget();
    if (!editor || !qobject_cast<QAbstractScrollArea *>(editor) ||
        !editor->inherits("QsciScintilla") || editor->property("calctabddNativeTab").toBool() ||
        editor->property("type").toInt() != 1) return result;
    result.editor = editor;
    result.sourceName = tabs->tabText(tabs->currentIndex());
    const Reader reader = readerFor(editor);
    if (!reader.function)
    {
        result.error = QStringLiteral("当前宿主编辑库未提供匹配的选区读取接口，无法查看选区。");
        return result;
    }
    // Keep rectangular, line and multiple selections distinct from a single
    // stream range. Never silently turn an enclosing range into selected text.
    if (reader.get(2570) != 1 || reader.get(2423) != 0 || // GETSELECTIONS / GETSELECTIONMODE
        reader.get(2581) != 0 || reader.get(2583) != 0) // virtual caret/anchor space
    {
        result.error = QStringLiteral("仅支持单个连续文本选区；请取消列选区、多选区或虚拟空格选择后重试。");
        return result;
    }
    const qintptr codePage = reader.get(2137); // GETCODEPAGE
    if (codePage != 65001 && codePage != 0)
    {
        result.error = QStringLiteral("当前文档的内部编码暂不支持选区读取。");
        return result;
    }
    const qintptr start = reader.get(2585); // GETSELECTIONNSTART, selection 0
    const qintptr end = reader.get(2587); // GETSELECTIONNEND
    const qintptr length = reader.get(2006); // GETLENGTH
    if (start < 0 || end < start || end > length ||
        end > std::numeric_limits<long>::max() || end - start >= std::numeric_limits<int>::max())
    {
        result.error = QStringLiteral("选区范围无效或超出读取接口的长度范围。");
        return result;
    }
    result.startByte = start;
    result.endByte = end;
    result.readOnly = reader.get(2140) != 0; // GETREADONLY
    result.error.clear();
    if (start == end)
    {
        result.status = DocumentSelection::Status::Empty;
        return result;
    }
    QByteArray bytes(static_cast<int>(end - start) + 1, '\0');
    TextRange range{{static_cast<long>(start), static_cast<long>(end)}, bytes.data()};
    const qintptr copied = reader.get(2162, 0, reinterpret_cast<qintptr>(&range)); // GETTEXTRANGE
    if (copied != end - start)
    {
        result.error = QStringLiteral("宿主返回的选区长度不一致，未显示选区内容。");
        return result;
    }
    bytes.chop(1); // Only the API's terminator; embedded NULs remain intact.
    result.text = codePage == 65001 ? QString::fromUtf8(bytes.constData(), bytes.size())
                                    : QString::fromLatin1(bytes.constData(), bytes.size());
    if (codePage == 65001 && result.text.toUtf8() != bytes)
    {
        result.text.clear();
        result.error = QStringLiteral("选区包含不完整或无效的 UTF-8 字符，未显示替换后的文本。");
        return result;
    }
    result.status = DocumentSelection::Status::Selected;
    return result;
}
