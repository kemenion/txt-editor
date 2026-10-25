cmake_minimum_required(VERSION 3.21)
project(txt_editor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# The wx libraries are linked statically; no run-time search path is needed and
# an absolute build-machine RPATH must never leak into packaged binaries.
set(CMAKE_SKIP_RPATH ON)

include(GNUInstallDirs)

option(TXT_EDITOR_BUILD_TESTS "Build the core unit tests" ON)
option(TXT_EDITOR_NATIVE_OPTIMIZATION "Build with -march=native (local benchmarks only)" OFF)

set(wxWidgets_USE_STATIC ON)
find_package(wxWidgets REQUIRED COMPONENTS core base stc)
include(${wxWidgets_USE_FILE})

# wx-config --static emits absolute paths to *all* of its dependencies.  For
# libraries from the local GTK sysroot this would force static versions (and
# their full transitive closure) into the link.  Keep the wx libraries static,
# but let the toolkit dependencies link dynamically.
set(TXT_WX_TARGETS "")
foreach(_wx_lib IN LISTS wxWidgets_LIBRARIES)
    set(_wx_lib_is_local FALSE)
    foreach(_wx_dir IN LISTS wxWidgets_LIBRARY_DIRS)
        if(_wx_lib MATCHES "^${_wx_dir}/")
            set(_wx_lib_is_local TRUE)
        endif()
    endforeach()
    if(NOT _wx_lib_is_local AND _wx_lib MATCHES "/lib([^/]+)\\.a$")
        list(APPEND TXT_WX_TARGETS "-l${CMAKE_MATCH_1}")
    else()
        list(APPEND TXT_WX_TARGETS "${_wx_lib}")
    endif()
endforeach()


add_library(txt_core STATIC
    src/core/Diff.cpp
    src/core/Document.cpp
    src/core/Encoding.cpp
    src/core/Language.cpp
    src/core/Session.cpp
)
target_include_directories(txt_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(txt_editor_lib STATIC
    src/app/Profile.cpp
    src/editor/EditorWidget.cpp
    src/ui/CompareFrame.cpp
    src/ui/DiffPane.cpp
    src/ui/FindPanel.cpp
    src/ui/MainFrame.cpp
    src/ui/SaveChangesDialog.cpp
    src/ui/TabBook.cpp
)
target_include_directories(txt_editor_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(txt_editor_lib PUBLIC txt_core ${TXT_WX_TARGETS})

add_executable(txt-editor
    src/app/App.cpp
    src/app/main.cpp
)
target_link_libraries(txt-editor PRIVATE txt_editor_lib)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target txt_core txt_editor_lib txt-editor)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

if(TXT_EDITOR_NATIVE_OPTIMIZATION)
    foreach(target txt_core txt_editor_lib txt-editor)
        target_compile_options(${target} PRIVATE -march=native)
    endforeach()
endif()

install(TARGETS txt-editor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES packaging/txt-editor.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES packaging/txt-editor.svg
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
        RENAME txt-editor.svg)

if(TXT_EDITOR_BUILD_TESTS)
    enable_testing()
    add_executable(txt_core_tests tests/test_core.cpp)
    target_link_libraries(txt_core_tests PRIVATE txt_core)
    add_test(NAME core COMMAND txt_core_tests)
endif()
