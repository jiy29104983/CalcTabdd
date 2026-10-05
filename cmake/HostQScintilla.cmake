set(qsci_root "${CALCTABDD_HOST_SOURCE_DIR}/src/qscint")
if(NOT EXISTS "${qsci_root}/src/Qsci/qsciscintilla.h")
    message(FATAL_ERROR "notepad-- QScintilla source not found")
endif()
if(CALCTABDD_QSCINTILLA_LIBRARY)
    add_library(host_qscintilla STATIC IMPORTED)
    set_target_properties(host_qscintilla PROPERTIES IMPORTED_LOCATION "${CALCTABDD_QSCINTILLA_LIBRARY}")
    set(scope INTERFACE)
else()
    find_package(Qt5 5.15 REQUIRED COMPONENTS PrintSupport)
    # Use exactly the source/header list shipped by the pinned host, avoiding
    # inactive lexers (e.g. LPeg) and duplicate LexHex implementations.
    file(READ "${qsci_root}/src/qscintilla.pro" qsci_project)
    string(REGEX REPLACE "#[^\n]*" "" qsci_project "${qsci_project}")
    string(REGEX MATCHALL "[A-Za-z0-9_./-]+\\.(cpp|h)" qsci_relative "${qsci_project}")
    set(qsci_sources)
    foreach(source IN LISTS qsci_relative)
        list(APPEND qsci_sources "${qsci_root}/src/${source}")
    endforeach()
    list(REMOVE_DUPLICATES qsci_sources)
    add_library(host_qscintilla STATIC ${qsci_sources})
    target_compile_definitions(host_qscintilla PRIVATE SCINTILLA_QT SCI_LEXER INCLUDE_DEPRECATED_FEATURES QT_NO_DEBUG_OUTPUT)
    target_include_directories(host_qscintilla PRIVATE
        "${qsci_root}/scintilla/include" "${qsci_root}/scintilla/lexlib"
        "${qsci_root}/scintilla/src" "${qsci_root}/scintilla/boostregex")
    if(MSVC)
        target_compile_options(host_qscintilla PRIVATE /utf-8 /W0)
        target_compile_definitions(host_qscintilla PRIVATE NOMINMAX)
    else()
        # The pinned XML highlighter uses intptr_t without including cstdint.
        # Supply the header for the test dependency without editing host sources.
        target_compile_options(host_qscintilla PRIVATE -w -include cstdint)
    endif()
    target_link_libraries(host_qscintilla PUBLIC Qt5::PrintSupport)
    set(scope PUBLIC)
endif()
# The pinned host hardcodes QSCINTILLA_DLL in qsciglobal.h despite shipping
# a static-library project. Generate a test-only header overlay; never edit
# the shared host checkout. Consumers must use the same static declarations.
file(READ "${qsci_root}/src/Qsci/qsciglobal.h" qsci_global)
string(REGEX REPLACE "#define[ \t]+QSCINTILLA_DLL[ \t\r]*\n" "" qsci_global "${qsci_global}")
set(qsci_overlay "${CMAKE_CURRENT_BINARY_DIR}/host_qscintilla_headers")
file(MAKE_DIRECTORY "${qsci_overlay}/Qsci")
file(WRITE "${qsci_overlay}/Qsci/qsciglobal.h" "${qsci_global}")
target_include_directories(host_qscintilla BEFORE ${scope} "${qsci_overlay}" "${qsci_root}/src")
target_link_libraries(host_qscintilla ${scope} Qt5::Widgets)
