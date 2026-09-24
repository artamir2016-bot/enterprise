# =============================================================================
# OES Enterprise — frontend shared library
# All wxWidgets GUI code: forms, visual controls, charts, dialogs, editors,
# themes, and the visual host framework.
# =============================================================================

file(GLOB_RECURSE FRONTEND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# GLOB grabs every .cpp, but the desktop frontend.dll (frontend.vcxproj) builds
# a HAND-PICKED subset. The files below are NOT in that project, so building
# them here breaks the CMake build:
#   * web/ + wfrontend.cpp — web sources; they belong to wfrontend.dll and only
#     compile with OES_USE_WEB defined.
#   * the listed uikit units — unfinished, not part of the desktop build.
# Excluding them realigns the GLOB with the real .sln source set.
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "/web/")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "wfrontend\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "uikit/ctrl/statusBar\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "uikit/dialog\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "uikit/settings\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "uikit/theme/theme_win32\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "uikit/topLevel\\.cpp$")
list(FILTER FRONTEND_SOURCES EXCLUDE REGEX "uikit/frame\\.cpp$")

add_library(frontend SHARED ${FRONTEND_SOURCES})

# ---------------------------------------------------------------------------
# The FORKED widget layer compiles quiet — same rule as the wxWidgets submodule
# (see the root CMakeLists), applied per-file because these live inside our target.
#
# win/ctrls/{dataview,grid,charts,treelistctrl}, win/theme and uikit are vendored
# forks: wxUniversal, wxAUI dock art, wxCharts, wx's own dataview and grid. Upstream
# style is not ours to correct, and the volume hides our own: 150 of the 168 -Wreorder
# lines in the 2026-08-02 CI run came from win/ctrls/dataview alone.
#
# NOTE the limit of this, learned from a CI log rather than guessed: COMPILE_OPTIONS on
# a SOURCE file applies to that .cpp only. A vendored HEADER with inline definitions --
# dataview/datavgen.h is the loud one -- warns inside every OUR .cpp that includes it,
# where this flag is not in effect. Those headers carry `#pragma GCC system_header`
# instead; that is the only thing that silences a header at its own definition site.
#
# docs/wx-fork.md is the inventory of what is forked and why.
# ---------------------------------------------------------------------------
set(FRONTEND_VENDORED_SOURCES ${FRONTEND_SOURCES})
list(FILTER FRONTEND_VENDORED_SOURCES INCLUDE REGEX "/(uikit|win/ctrls|win/theme)/")

if(FRONTEND_VENDORED_SOURCES)
    if(MSVC)
        set_source_files_properties(${FRONTEND_VENDORED_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
    else()
        set_source_files_properties(${FRONTEND_VENDORED_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    endif()
endif()

target_include_directories(frontend
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/engine/backend
        # win/theme/luna_dockart.cpp is a fork of wxAUI's dock art and reaches into wx's
        # GTK-private headers, which include <gtk/gtk.h> outright. wx links GTK privately,
        # so those paths never arrive through wx::core. Empty off Linux, hence unguarded.
        ${GTK3_INCLUDE_DIRS}
)

target_compile_definitions(frontend
    PRIVATE
        NOPCH
)

# Windows DLL export macro — FRONTEND_API resolves to dllexport only when
# FRONTEND_EXPORTS is defined (frontend.h); without it every FRONTEND_API class
# is dllimport and its wx event-table / class-info static members fail to define
# (C2491). Mirrors backend's BACKEND_EXPORTS.
if(WIN32)
    target_compile_definitions(frontend PRIVATE FRONTEND_EXPORTS)
endif()

target_link_libraries(frontend
    PRIVATE
        backend
        wx::core
        wx::aui
        wx::html
        wx::propgrid
        wx::adv
        wx::stc
        wx::xrc
        wx::qa
        wx::base
        wx::net
        wx::xml
)

set_target_properties(frontend PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "frontend"
)

# =============================================================================
# wfrontend + wenterprise-server — the WEB stack.
#
# Historically MSBuild-only (absent from CMake), which forced a separate native
# wxWidgets build just to link them. They are the SAME frontend sources compiled
# with OES_USE_WEB (rendering to HTML/JSON instead of wx widgets) plus the web/
# layer — so building them HERE reuses the CMake-built wx + backend, no native
# wx bootstrap. Windows-only in practice (winsock via cpp-httplib); gated so the
# Linux/macOS CI is untouched.
# =============================================================================
if(WIN32)
    set(WFRONTEND_SOURCES
        wfrontend.cpp
        web/webClient.cpp web/webSession.cpp web/webApplication.cpp web/webFrame.cpp
        web/webWindow.cpp web/webSizer.cpp web/webTimer.cpp web/webChildFrame.cpp web/webStubs.cpp
        visualView/visualHost.cpp visualView/visualHostEvent.cpp visualView/visualHostClient.cpp
        visualView/visualHostClientDocView.cpp visualView/controlEnum.cpp visualView/layerObject.cpp
        docView/docManager.cpp docView/docView.cpp
        visualView/layers/commandBar.cpp visualView/layers/commandReceiver.cpp
        visualView/ctrl/control.cpp visualView/ctrl/controlProperty.cpp visualView/ctrl/typeControl.cpp
        visualView/ctrl/window.cpp visualView/ctrl/frame.cpp visualView/ctrl/frameEvent.cpp
        visualView/ctrl/frameID.cpp visualView/ctrl/frameProperty.cpp
        visualView/ctrl/sizer.cpp visualView/ctrl/boxsizer.cpp visualView/ctrl/boxsizer_res.cpp
        visualView/ctrl/staticboxsizer.cpp visualView/ctrl/staticboxsizer_res.cpp
        visualView/ctrl/gridsizer.cpp visualView/ctrl/gridsizer_res.cpp
        visualView/ctrl/wrapsizer.cpp visualView/ctrl/wrapsizer_res.cpp visualView/ctrl/itemsizer.cpp
        visualView/ctrl/button.cpp visualView/ctrl/button_res.cpp visualView/ctrl/buttonEvent.cpp
        visualView/ctrl/statictext.cpp visualView/ctrl/statictextEvent.cpp visualView/ctrl/statictext_res.cpp
        visualView/ctrl/textctrl.cpp visualView/ctrl/textctrlEvent.cpp visualView/ctrl/textctrlProperty.cpp
        visualView/ctrl/textctrl_res.cpp
        visualView/ctrl/checkbox.cpp visualView/ctrl/checkboxEvent.cpp visualView/ctrl/checkboxProperty.cpp
        visualView/ctrl/checkbox_res.cpp
        visualView/ctrl/choice.cpp visualView/ctrl/choice_res.cpp
        visualView/ctrl/combobox.cpp visualView/ctrl/combobox_res.cpp
        visualView/ctrl/listbox.cpp visualView/ctrl/listbox_res.cpp
        visualView/ctrl/picture.cpp visualView/ctrl/picture_res.cpp
        visualView/ctrl/toolBar.cpp visualView/ctrl/toolBar_res.cpp
        visualView/ctrl/toolBarItem.cpp visualView/ctrl/toolBarItem_res.cpp
        visualView/ctrl/toolBarEvent.cpp visualView/ctrl/toolBarProperty.cpp visualView/ctrl/toolBarMenu.cpp
        visualView/ctrl/toolBarAction.cpp visualView/ctrl/toolBarItemAction.cpp
        visualView/ctrl/tableBox.cpp visualView/ctrl/tableBox_res.cpp
        visualView/ctrl/tableBoxColumn.cpp visualView/ctrl/tableBoxColumn_res.cpp
        visualView/ctrl/form.cpp visualView/ctrl/formAttribute.cpp visualView/ctrl/formCommand.cpp
        visualView/ctrl/formControl.cpp visualView/ctrl/formObject.cpp visualView/ctrl/formVisualHost.cpp
        visualView/ctrl/formAction.cpp visualView/ctrl/formFactory.cpp visualView/ctrl/formMem.cpp
        visualView/ctrl/form_res.cpp
    )

    add_library(wfrontend SHARED ${WFRONTEND_SOURCES})

    target_include_directories(wfrontend
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/src/engine
            ${CMAKE_SOURCE_DIR}/src/engine/backend
            ${CMAKE_SOURCE_DIR}/src/3rdparty/cpp-httplib
    )

    target_compile_definitions(wfrontend
        PRIVATE
            NOPCH
            OES_USE_WEB
            WFRONTEND_EXPORTS      # FRONTEND_API -> dllexport (frontend.h keys on either export macro)
    )

    target_link_libraries(wfrontend
        PRIVATE
            backend
            wx::core wx::aui wx::html wx::propgrid wx::adv wx::stc wx::xrc wx::qa
            wx::base wx::net wx::xml
    )

    set_target_properties(wfrontend PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME "wfrontend"
    )

    # The web server process — drives wfrontend over HTTP (cpp-httplib).
    add_executable(wenterprise-server ${CMAKE_SOURCE_DIR}/src/engine/wenterprise-server/main.cpp)

    target_include_directories(wenterprise-server
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/src/engine
            ${CMAKE_SOURCE_DIR}/src/3rdparty/cpp-httplib
    )

    target_link_libraries(wenterprise-server
        PRIVATE
            wfrontend
            backend
            wx::base wx::net
            ws2_32 crypt32
    )

    set_target_properties(wenterprise-server PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        OUTPUT_NAME "wenterprise-server"
    )
endif()
