
# RedPandaIDE #

add_executable(RedPandaIDE WIN32)
# "Dev by Sufyan" — the shipped executable is named DevBySufyan
# (the CMake target keeps its upstream name).
set_target_properties(RedPandaIDE PROPERTIES OUTPUT_NAME "DevBySufyan")
# rooms/mainwindow_rooms.cpp includes ui_mainwindow.h from a subdirectory;
# AUTOUIC needs the main source dir in its .ui search paths.
set_target_properties(RedPandaIDE PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_qt_plain_cpp(RedPandaIDE
    src/rooms/mainwindow_rooms
    src/rooms/dbsfeatures
    src/rooms/dbstheme
    src/autolinkmanager
    src/colorscheme
    src/customfileiconprovider
    src/projectoptions
    src/settings
    src/syntaxermanager
    src/systemconsts
    src/utils
    src/visithistorymanager
    # compiler
    src/compiler/compilerinfo
    # debugger
    src/debugger/dapprotocol
    src/debugger/gdbmiresultparser
    # parser
    src/parser/cppparser
    src/parser/cpppreprocessor
    src/parser/cpptokenizer
    src/parser/parserutils
    src/parser/statementmodel
    # problems
    src/problems/competitivecompenionhandler
    src/problems/freeprojectsetformat
    src/problems/ojproblemset
    src/problems/problemcasevalidator
    # settings
    src/settings/basesettings
    src/settings/codecompletionsettings
    src/settings/codeformattersettings
    src/settings/compilersetsettings
    src/settings/compilesettings
    src/settings/debuggersettings
    src/settings/dirsettings
    src/settings/editorsettings
    src/settings/environmentsettings
    src/settings/executorsettings
    src/settings/languagesettings
    src/settings/uisettings
    # utils
    src/utils/escape
    src/utils/file
    src/utils/font
    src/utils/os
    src/utils/parsearg
    src/utils/parser
    src/utils/parsemacros
    src/utils/pe
    src/utils/terminal
    src/utils/ui
    )

target_moc_classes(RedPandaIDE
    src/rooms/roomclient
    src/rooms/dbsupdater
    src/rooms/dbsfx
    src/rooms/roomspanel
    src/rooms/roomchatwindow
    src/rooms/welcomepage
    src/caretlist
    src/codesnippetsmanager
    src/cpprefacter
    src/editor
    src/editormanager
    src/iconsmanager
    src/main
    src/project
    src/projecttemplate
    src/shortcutmanager
    src/symbolusagemanager
    src/thememanager
    src/todoparser
    src/toolsmanager
    # compiler
    src/compiler/compiler
    src/compiler/compilermanager
    src/compiler/executablerunner
    src/compiler/filecompiler
    src/compiler/nasmfilecompiler
    src/compiler/ojproblemcasesrunner
    src/compiler/projectcompiler
    src/compiler/runner
    src/compiler/stdincompiler
    # debugger
    src/debugger/dapdebugger
    src/debugger/debugger
    src/debugger/debuggermodels
    src/debugger/gdbmidebugger
    # parser
    src/parser/cppparser
    src/parser/statementmodel
    # problems
    src/problems/competitivecompenionhandler
    # reformatter
    src/reformatter/basereformatter
    src/reformatter/astyleformatter
    # settings dialog
    src/settingsdialog/settingswidget
    # widgets
    src/widgets/bookmarkmodel
    src/widgets/classbrowser
    src/widgets/codecompletionlistview
    src/widgets/codecompletionpopup
    src/widgets/coloredit
    src/widgets/compileargumentswidget
    src/widgets/customdisablediconengine
    src/widgets/customfilesystemmodel
    src/widgets/darkfusionstyle
    src/widgets/editorstabwidget
    src/widgets/filenameeditdelegate
    src/widgets/functiontooltipwidget
    src/widgets/headercompletionpopup
    src/widgets/issuestable
    src/widgets/labelwithmenu
    src/widgets/lightfusionstyle
    src/widgets/linenumbertexteditor
    src/widgets/macroinfomodel
    src/widgets/ojproblemsetmodel
    src/widgets/qconsole
    src/widgets/qpatchedcombobox
    src/widgets/searchresultview
    src/widgets/shortcutinputedit
    src/widgets/shrinkabletabwidget)

target_ui_classes(RedPandaIDE
    src/mainwindow
    # settings dialog
    src/settingsdialog/compilerautolinkwidget
    src/settingsdialog/compilergaswidget
    src/settingsdialog/compilernasmwidget
    src/settingsdialog/compilersetdirectorieswidget
    src/settingsdialog/compilersetoptionwidget
    src/settingsdialog/debuggeneralwidget
    src/settingsdialog/editorautosavewidget
    src/settingsdialog/editorclipboardwidget
    src/settingsdialog/editorcodecompletionwidget
    src/settingsdialog/editorcolorschemewidget
    src/settingsdialog/editorcustomctypekeywords
    src/settingsdialog/editorfontwidget
    src/settingsdialog/editorgeneralwidget
    src/settingsdialog/editormiscwidget
    src/settingsdialog/editorsnippetwidget
    src/settingsdialog/editorsymbolcompletionwidget
    src/settingsdialog/editorsyntaxcheckwidget
    src/settingsdialog/editortooltipswidget
    src/settingsdialog/environmentappearancewidget
    src/settingsdialog/environmentfolderswidget
    src/settingsdialog/environmentperformancewidget
    src/settingsdialog/environmentprogramswidget
    src/settingsdialog/environmentshortcutwidget
    src/settingsdialog/executorgeneralwidget
    src/settingsdialog/executorproblemsetwidget
    src/settingsdialog/formattergeneralwidget
    src/settingsdialog/formatterpathwidget
    src/settingsdialog/languageasmgenerationwidget
    src/settingsdialog/projectcompileparamaterswidget
    src/settingsdialog/projectcompilerwidget
    src/settingsdialog/projectdirectorieswidget
    src/settingsdialog/projectdllhostwidget
    src/settingsdialog/projectfileswidget
    src/settingsdialog/projectgeneralwidget
    src/settingsdialog/projectmakefilewidget
    src/settingsdialog/projectoutputwidget
    src/settingsdialog/projectprecompilewidget
    src/settingsdialog/settingsdialog
    src/settingsdialog/toolsgeneralwidget
    # widgets
    src/widgets/aboutdialog
    src/widgets/choosethemedialog
    src/widgets/cpudialog
    src/widgets/custommakefileinfodialog
    src/widgets/editorfontdialog
    src/widgets/filepropertiesdialog
    src/widgets/infomessagebox
    src/widgets/newclassdialog
    src/widgets/newheaderdialog
    src/widgets/newprojectdialog
    src/widgets/newprojectunitdialog
    src/widgets/newtemplatedialog
    src/widgets/ojproblempropertywidget
    src/widgets/projectalreadyopendialog
    src/widgets/searchdialog
    src/widgets/searchinfiledialog
    src/widgets/signalmessagedialog)

target_compile_definitions(RedPandaIDE PRIVATE
    ${GLOBAL_COMPILE_DEFINITIONS}
    APP_NAME=\"${APP_NAME}\"
    LIBEXECDIR=\"${CMAKE_INSTALL_LIBEXECDIR}\"
    REDPANDA_CPP_VERSION=\"${REDPANDA_VERSION}\"
    ARCH_X86_64=1
    ARCH_X86=1)

if(APP_VERSION_PRE_RELEASE)
    target_compile_definitions(RedPandaIDE PRIVATE
        APP_VERSION_SUFFIX=\"${APP_VERSION_PRE_RELEASE}\")
endif()

target_include_directories(RedPandaIDE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(RedPandaIDE PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Network
    Qt::PrintSupport
    Qt::Svg
    Qt::WebSockets
    Qt::Widgets
    Qt::Xml
    qsynedit
    redpanda_qt_utils)

#target_link_options(RedPandaIDE PRIVATE
#    $<$<CONFIG:Release>:-s -static>
#    $<$<CONFIG:MinSizeRel>:-s -static>
#)

# Dev by Sufyan WASM port (Part 2): the console test executables below
# (test-editor, test-cppparser) link as plain add_executable targets and
# miss Qt's WASM Embind glue (undefined _emval_* symbols). They are dev
# tests, not part of the shipped IDE, so skip them for Emscripten only.
if(NOT EMSCRIPTEN)

#####################
# test-editor       #
#####################

add_executable(test-editor test/test-editor-main.cpp)

target_qt_plain_cpp(test-editor
    #settings
    src/settings/basesettings
    src/settings/dirsettings
    src/settings/editorsettings
    src/settings/codecompletionsettings

    src/parser/cppparser
    src/parser/cpppreprocessor
    src/parser/cpptokenizer
    src/parser/parserutils
    src/parser/statementmodel

    # utils
    src/utils/escape
    src/utils/file
    src/utils/font
    src/utils/os
    src/utils/parsearg
    src/utils/parsemacros
    src/utils/ui

    src/colorscheme
    src/syntaxermanager
    src/iconsmanager
    src/systemconsts
    src/compiler/compilerinfo
    src/reformatter/basereformatter
    )

target_moc_classes(test-editor
    src/editor

    src/widgets/bookmarkmodel
    src/widgets/codecompletionlistview
    src/widgets/codecompletionpopup
    src/widgets/customdisablediconengine
    src/widgets/functiontooltipwidget
    src/widgets/headercompletionpopup

    src/symbolusagemanager
    src/codesnippetsmanager
    #test
    test/test_editor_base
    test/test_editor_symbol_completion
)
target_include_directories(test-editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(test-editor PRIVATE
        Qt::Core
        Qt::Gui
        Qt::PrintSupport
        Qt::Svg
        Qt::Test
        Qt::Widgets
        Qt::Xml
        qsynedit
        redpanda_qt_utils)

if (QT_FEATURE_static)
    qt_import_plugins(test-editor
        INCLUDE Qt::QOffscreenIntegrationPlugin)
endif()

add_custom_command(
    TARGET test-editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<TARGET_PROPERTY:test-editor,SOURCE_DIR>/test/resources"
        "$<TARGET_PROPERTY:test-editor,BINARY_DIR>/resources")

target_compile_definitions(test-editor PRIVATE
    ${GLOBAL_COMPILE_DEFINITIONS}
    APP_NAME=\"test-editor\"
    LIBEXECDIR=\"${CMAKE_INSTALL_LIBEXECDIR}\"
    REDPANDA_CPP_VERSION=\"${REDPANDA_VERSION}\"
    ARCH_X86_64=1
    ARCH_X86=1)

add_test(
    NAME test-editor
    COMMAND test-editor)

add_dependencies(all-test-targets test-editor)

#####################
# test-cppparser    #
#####################

add_executable(test-cppparser test/test-cppparser-main.cpp)

target_qt_plain_cpp(test-cppparser
    src/parser/cppparser
    src/parser/cpppreprocessor
    src/parser/cpptokenizer
    src/parser/parserutils
    src/parser/statementmodel
    )

target_moc_classes(test-cppparser
    #test
    test/test_cppparser
    test/test_cpppreprocessor
    test/test_cpptokenizer
)
target_include_directories(test-cppparser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(test-cppparser PRIVATE
        Qt::Core
        Qt::Test
        qsynedit
        redpanda_qt_utils)

if (QT_FEATURE_static)
    qt_import_plugins(test-cppparser
        INCLUDE Qt::QOffscreenIntegrationPlugin)
endif()

add_custom_command(
    TARGET test-cppparser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<TARGET_PROPERTY:test-editor,SOURCE_DIR>/test/resources"
        "$<TARGET_PROPERTY:test-editor,BINARY_DIR>/resources")

target_compile_definitions(test-cppparser PRIVATE
    ${GLOBAL_COMPILE_DEFINITIONS}
    APP_NAME=\"test-cppparser\"
    LIBEXECDIR=\"${CMAKE_INSTALL_LIBEXECDIR}\"
    REDPANDA_CPP_VERSION=\"${REDPANDA_VERSION}\"
    ARCH_X86_64=1
    ARCH_X86=1)

add_test(
    NAME test-cppparser
    COMMAND test-cppparser)

add_dependencies(all-test-targets test-cppparser)

endif() # NOT EMSCRIPTEN (test executables)

#####################
# Platform-specific #
#####################

if(WIN32)
    target_ui_classes(RedPandaIDE
        src/settingsdialog/environmentfileassociationwidget
        src/settingsdialog/projectversioninfowidget)

    target_link_libraries(RedPandaIDE PRIVATE
        advapi32
        dbghelp
        psapi
        shlwapi
        user32)
endif()

if(MACOS)
    target_link_libraries(RedPandaIDE PRIVATE Qt::GuiPrivate)
endif()

if(${FILESYSTEM_LAYOUT} STREQUAL "bundle")
    set_target_properties(RedPandaIDE PROPERTIES
        MACOSX_BUNDLE ON)
    set(MACOSX_BUNDLE_ICON_FILE RedPandaIDE.icns)
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/platform/macos/RedPandaIDE.icns
        PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
    target_sources(RedPandaIDE PRIVATE
        ${CMAKE_SOURCE_DIR}/platform/macos/RedPandaIDE.icns)
endif()

if(LINUX AND QT_FEATURE_static)
    qt_import_plugins(RedPandaIDE
        INCLUDE
            Qt::QComposePlatformInputContextPlugin
            Qt::QFcitx5PlatformInputContextPlugin
            Qt::QIbusPlatformInputContextPlugin)
endif()

if(${FILESYSTEM_LAYOUT} STREQUAL "hierarchy")
    install(
        TARGETS RedPandaIDE
        DESTINATION ${CMAKE_INSTALL_BINDIR})
elseif(${FILESYSTEM_LAYOUT} STREQUAL "flat")
    install(
        TARGETS RedPandaIDE
        DESTINATION .)
elseif(${FILESYSTEM_LAYOUT} STREQUAL "bundle")
    install(
        TARGETS RedPandaIDE
        BUNDLE DESTINATION .)
else()
    message(FATAL_ERROR "Unknown FILESYSTEM_LAYOUT: ${FILESYSTEM_LAYOUT}")
endif()

#########
# Paths #
#########

set (MAIN_PROGRAM_PATH_DEFS
    FILESYSTEM_LAYOUT=FILESYSTEM_LAYOUT_${FILESYSTEM_LAYOUT}
    FILESYSTEM_LAYOUT_hierarchy=1
    FILESYSTEM_LAYOUT_flat=2
    FILESYSTEM_LAYOUT_bundle=3
    MAKE_INTERFACE=MAKE_INTERFACE_${MAKE_INTERFACE}
    MAKE_INTERFACE_unix=1
    MAKE_INTERFACE_mingw32=2
    PORTABLE_CONFIG=PORTABLE_CONFIG_${PORTABLE_CONFIG}
    PORTABLE_CONFIG_oui=1
    PORTABLE_CONFIG_non=2
    PORTABLE_CONFIG_registry=3)

target_compile_definitions(RedPandaIDE PRIVATE ${MAIN_PROGRAM_PATH_DEFS})
if(NOT EMSCRIPTEN) # test-editor is not built for Emscripten (see above)
    target_compile_definitions(test-editor PRIVATE ${MAIN_PROGRAM_PATH_DEFS})
endif()

###################
# Feature options #
###################

if(LUA_ADDON)
    target_qt_plain_cpp(RedPandaIDE
        src/addon/luaapi
        src/addon/luaexecutor
        src/addon/luaruntime)

    target_compile_definitions(RedPandaIDE PRIVATE ENABLE_LUA_ADDON)

    target_link_libraries(RedPandaIDE PRIVATE lua)
endif()

if(EMSCRIPTEN)
    # Dev by Sufyan WASM port (Part 2):
    # - src/wasmshim/qprocess_wasm: minimal QProcess stand-in. Qt for
    #   WebAssembly is built with QT_FEATURE_process off, so the class
    #   does not exist there; the shim lets the process-using sources
    #   compile unchanged (its start() simply always fails — the real
    #   work is rerouted to the online backend below, and the debugger
    #   is disabled in the web UI).
    # - src/compiler/onlinebackend: synchronous POST to the /api/run
    #   endpoint (same contract as the web client) + the Runner that
    #   surfaces program output in the IDE's own output panel.
    target_moc_classes(RedPandaIDE
        src/wasmshim/qprocess_wasm
        src/compiler/onlinebackend
        # Part 9: terminal-style window for online Run output.
        src/wasmshim/dbsterminal)

    # Dev by Sufyan WASM port (Part 6): single-threaded variant for
    # browsers where SharedArrayBuffer is unavailable (no cross-origin
    # isolation), built with the wasm_singlethread Qt kit. Under
    # DBS_WASM_ST the online backend uses ASYNCHRONOUS emscripten_fetch
    # from the main thread (a synchronous fetch is only legal on a
    # pthread worker, which this kit does not have) and the
    # compiler/runner completion sequencing keys off the app-level
    # finished signals instead of QThread::finished. The multithread
    # build (option OFF, the default) is behavior-identical to Part 2.
    option(DBS_WASM_SINGLETHREAD "Dev by Sufyan WASM: single-threaded variant (no SharedArrayBuffer/pthreads; async online backend)" OFF)
    if(DBS_WASM_SINGLETHREAD)
        target_compile_definitions(RedPandaIDE PRIVATE DBS_WASM_ST=1)
    endif()

    # The IDE target is created with plain add_executable() upstream,
    # so it misses the finalizer qt_add_executable() would have run:
    # embind linkage (without it libQt6Core's _emval_* symbols stay
    # undefined), the JS export name, and generation of the
    # .html/qtloader.js shell. Run Qt's own WASM finalizer for it.
    _qt_internal_finalize_wasm_app(RedPandaIDE)

    # The IDE's static data alone needs ~24 MB, over Emscripten's
    # 16 MB default initial memory; in the multithreaded
    # (shared-memory) configuration wasm-ld then refuses the link.
    # Start at 64 MB — ALLOW_MEMORY_GROWTH (set by Qt's platform
    # properties) lets it grow from there.
    target_link_options(RedPandaIDE PRIVATE "SHELL:-s INITIAL_MEMORY=67108864")

    # Dev by Sufyan WASM port (Part 9 Stage 2): link Emscripten's
    # IDBFS so FS.filesystems.IDBFS exists at runtime — the browser
    # launcher mounts it over the app home (/home/web_user) for
    # real file persistence across reloads. Without this flag the
    # filesystem object silently contains MEMFS only, every syncfs
    # "succeeds" against no persistent mount, and nothing survives
    # a reload (the Stage 1 persistence failure).
    target_link_options(RedPandaIDE PRIVATE "-lidbfs.js")
endif()

if(SDCC)
    target_moc_classes(RedPandaIDE
        src/compiler/sdccfilecompiler
        src/compiler/sdccprojectcompiler)

    target_compile_definitions(RedPandaIDE PRIVATE ENABLE_SDCC)
endif()

if(VCS)
    target_qt_plain_cpp(RedPandaIDE
        src/settings/vcssettings)
    target_moc_classes(RedPandaIDE
        src/vcs/gitmanager
        src/vcs/gitrepository
        src/vcs/gitutils)

    target_ui_classes(RedPandaIDE
        src/settingsdialog/toolsgitwidget
        src/vcs/gitbranchdialog
        src/vcs/gitfetchdialog
        src/vcs/gitlogdialog
        src/vcs/gitmergedialog
        src/vcs/gitpulldialog
        src/vcs/gitpushdialog
        src/vcs/gitremotedialog
        src/vcs/gitresetdialog
        src/vcs/gituserconfigdialog)

    target_compile_definitions(RedPandaIDE PRIVATE ENABLE_VCS)
endif()

if(WINDOWS_PREFER_OPENCONSOLE)
    target_compile_definitions(RedPandaIDE PRIVATE WINDOWS_PREFER_OPENCONSOLE)
endif()

#############
# Resources #
#############

target_sources(RedPandaIDE PRIVATE
    fonts.qrc
    codes.qrc
    defaultconfigs.qrc
    icons.qrc
    projecttemplates.qrc)

if(EMSCRIPTEN)
    # Part 7 (root cause of the WASM startup abort): the Qt WASM
    # kits are built with QT_FEATURE_zstd off, so a zstd-compressed
    # resource can never be decompressed at runtime — QFile on any
    # ":/…" entry fails, and the first startup read that throws on
    # failure (the terminal list in EnvironmentSettings) killed the
    # app after the download, before any window appeared. rcc
    # compresses with zstd by default, so store the app's resources
    # UNCOMPRESSED in web builds; the qResourceFeatureZstd()==0 shim
    # in the QProcess shim file is then truthful. (Translations
    # embedded by target_embed_translations below may stay
    # compressed/unreadable — worst case the web UI is English.)
    set_target_properties(RedPandaIDE PROPERTIES AUTORCC_OPTIONS "--no-compress")
endif()

# translation

target_embed_translations(RedPandaIDE
    TS_FILES
        translations/RedPandaIDE_pt_BR.ts
        translations/RedPandaIDE_ru_RU.ts
        translations/RedPandaIDE_zh_CN.ts
        translations/RedPandaIDE_zh_TW.ts)

if(QT_FEATURE_static)
    set(qtbase_translation_filenames
        qtbase_pt_BR.qm
        qtbase_ru_RU.qm
        qtbase_zh_CN.qm
        qtbase_zh_TW.qm)

    set(qtbase_translation_files)

    foreach(filename IN LISTS qtbase_translation_filenames)
        if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
            set(absolute_filename ${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}/${filename})
        else()
            # there is no `QT5_INSTALL_TRANSLATIONS`. hard-code default path
            set(absolute_filename ${QT5_INSTALL_PREFIX}/translations/${filename})
        endif()
        if(EXISTS ${absolute_filename})
            list(APPEND qtbase_translation_files ${absolute_filename})
        endif()
    endforeach()

    if(qtbase_translation_files)
        message(STATUS "Found Qt base translation files: ${qtbase_translation_files}")
        target_embed_qtbase_translations(RedPandaIDE
            QM_FILES ${qtbase_translation_files})
    else()
        message(WARNING "Qt base translation files not found")
    endif()
endif()

# appereance

file(GLOB_RECURSE resource_iconset_files
    "resources/iconsets/*.json"
    "resources/iconsets/*.svg")
target_embed_resources(RedPandaIDE
    RESOURCE_NAME iconsets
    FILES ${resource_iconset_files})

file(GLOB resource_theme_files
    "resources/themes/*.json"
    "resources/themes/*.lua"
    "resources/themes/*.png")
target_embed_resources(RedPandaIDE
    RESOURCE_NAME theme
    FILES ${resource_theme_files})

file(GLOB resource_colorscheme_files
    "resources/colorschemes/*.scheme")
target_embed_resources(RedPandaIDE
    RESOURCE_NAME colorschemes
    FILES ${resource_colorscheme_files})

# app icon

if(WIN32)
    target_sources(RedPandaIDE PRIVATE
        resources/windows_icon.rc)
elseif(MACOS)
    set(app_icon ../platform/macos/RedPandaIDE.icns)
    set_source_files_properties(${app_icon}
        PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(RedPandaIDE PRIVATE
        ${app_icon})
endif()
