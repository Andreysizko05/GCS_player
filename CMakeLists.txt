cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

project(GCS_player VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)
set(GCS_GSTREAMER_MODE "AUTO" CACHE STRING "Controls video receiver support: AUTO, ON, or OFF.")
set_property(CACHE GCS_GSTREAMER_MODE PROPERTY STRINGS AUTO ON OFF)
set(GCS_GSTREAMER_ROOT "" CACHE PATH
    "Optional GStreamer SDK/runtime root used to seed PATH and PKG_CONFIG_PATH."
)

include(cmake/BootstrapQt.cmake)
include(cmake/BootstrapGStreamer.cmake)

function(gcs_prepend_env_path env_name new_path)
    if(NOT new_path OR NOT EXISTS "${new_path}")
        return()
    endif()

    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(_path_separator ";")
    else()
        set(_path_separator ":")
    endif()

    set(_current_value "$ENV{${env_name}}")
    if(_current_value)
        set(ENV{${env_name}} "${new_path}${_path_separator}${_current_value}")
    else()
        set(ENV{${env_name}} "${new_path}")
    endif()
endfunction()

string(TOUPPER "${GCS_GSTREAMER_MODE}" GCS_GSTREAMER_MODE)

if(NOT GCS_GSTREAMER_MODE STREQUAL "OFF")
    gcs_bootstrap_gstreamer()
endif()

if(GCS_GSTREAMER_ROOT)
    gcs_prepend_env_path(PATH "${GCS_GSTREAMER_ROOT}/bin")
    gcs_prepend_env_path(PKG_CONFIG_PATH "${GCS_GSTREAMER_ROOT}/lib/pkgconfig")
    gcs_prepend_env_path(PKG_CONFIG_PATH "${GCS_GSTREAMER_ROOT}/lib64/pkgconfig")
    gcs_prepend_env_path(PKG_CONFIG_PATH "${GCS_GSTREAMER_ROOT}/lib/x86_64-linux-gnu/pkgconfig")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(PkgConfig QUIET)

set(GCS_ENABLE_GSTREAMER OFF)

if(GCS_GSTREAMER_MODE STREQUAL "OFF")
    message(STATUS "GStreamer receiver support is disabled by configuration")
elseif(PkgConfig_FOUND)
    pkg_check_modules(GSTREAMER IMPORTED_TARGET QUIET
        gstreamer-1.0
        gstreamer-app-1.0
        gstreamer-video-1.0
    )
    if(TARGET PkgConfig::GSTREAMER)
        set(GCS_ENABLE_GSTREAMER ON)
        message(STATUS "GStreamer receiver support is enabled")
    elseif(GCS_GSTREAMER_MODE STREQUAL "ON")
        message(FATAL_ERROR
            "GStreamer receiver support was requested, but the required development files "
            "were not found via pkg-config."
        )
    else()
        message(STATUS "GStreamer development files were not found, building without video receiver")
    endif()
elseif(GCS_GSTREAMER_MODE STREQUAL "ON")
    message(FATAL_ERROR "GStreamer receiver support was requested, but pkg-config is unavailable.")
else()
    message(STATUS "pkg-config is unavailable, building without video receiver")
endif()

if(GCS_ENABLE_GSTREAMER)
    find_package(Qt6 REQUIRED COMPONENTS Multimedia MultimediaWidgets)
endif()

set(GCS_PLAYER_SOURCES
    main.cpp
    MainWindow.cpp
    MainWindow.h
    MainWindow.ui
)

if(GCS_ENABLE_GSTREAMER)
    list(APPEND GCS_PLAYER_SOURCES
        GstVideoReceiver.cpp
        GstVideoReceiver.h
    )
endif()

qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES uk_UA)

qt_add_executable(GCS_player
    MANUAL_FINALIZATION
    ${GCS_PLAYER_SOURCES}
)

qt_add_translations(GCS_player
    TS_FILES GCS_player_uk_UA.ts
    RESOURCE_PREFIX /i18n
)

target_link_libraries(GCS_player PRIVATE Qt6::Widgets)

if(GCS_ENABLE_GSTREAMER)
    target_link_libraries(GCS_player PRIVATE
        Qt6::Multimedia
        Qt6::MultimediaWidgets
        PkgConfig::GSTREAMER
    )
    target_compile_definitions(GCS_player PRIVATE GCS_ENABLE_GSTREAMER=1)
endif()

set_target_properties(GCS_player PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER ua.kpi.pid-gui
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)

install(TARGETS GCS_player
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

function(gcs_stage_directory_for_target target source_dir destination_subdir comment_text)
    if(NOT source_dir OR NOT EXISTS "${source_dir}")
        return()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_FILE_DIR:${target}>/${destination_subdir}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${source_dir}"
            "$<TARGET_FILE_DIR:${target}>/${destination_subdir}"
        COMMENT "${comment_text}"
    )
endfunction()

function(gcs_install_directory_if_exists source_dir destination_subdir)
    if(NOT source_dir OR NOT EXISTS "${source_dir}")
        return()
    endif()

    install(DIRECTORY "${source_dir}/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/${destination_subdir}"
    )
endfunction()

if(GCS_ENABLE_GSTREAMER AND PkgConfig_FOUND)
    pkg_get_variable(GCS_GST_PLUGIN_DIR gstreamer-1.0 pluginsdir)
    pkg_get_variable(GCS_GST_TOOLS_DIR gstreamer-1.0 toolsdir)
    pkg_get_variable(GCS_GST_PLUGIN_SCANNER_DIR gstreamer-1.0 pluginscannerdir)
    pkg_get_variable(GCS_GST_PREFIX gstreamer-1.0 prefix)
    pkg_get_variable(GCS_GST_GIO_MODULE_DIR gio-2.0 giomoduledir)

    if(WIN32)
        set(GCS_GST_RUNTIME_DIR "${GCS_GST_PREFIX}/bin")
        gcs_stage_directory_for_target(
            GCS_player
            "${GCS_GST_PLUGIN_DIR}"
            "gstreamer-1.0"
            "Copying GStreamer plugin directory for local execution"
        )
        gcs_install_directory_if_exists("${GCS_GST_PLUGIN_DIR}" "gstreamer-1.0")

        gcs_stage_directory_for_target(
            GCS_player
            "${GCS_GST_GIO_MODULE_DIR}"
            "gio/modules"
            "Copying GIO modules for GStreamer"
        )
        gcs_install_directory_if_exists("${GCS_GST_GIO_MODULE_DIR}" "gio/modules")

        gcs_stage_directory_for_target(
            GCS_player
            "${GCS_GST_TOOLS_DIR}"
            "gstreamer-tools"
            "Copying GStreamer helper tools"
        )
        gcs_install_directory_if_exists("${GCS_GST_TOOLS_DIR}" "gstreamer-tools")

        gcs_stage_directory_for_target(
            GCS_player
            "${GCS_GST_PLUGIN_SCANNER_DIR}"
            "gstreamer-tools/gstreamer-1.0"
            "Copying the GStreamer plugin scanner"
        )
        gcs_install_directory_if_exists("${GCS_GST_PLUGIN_SCANNER_DIR}" "gstreamer-tools/gstreamer-1.0")

        gcs_stage_directory_for_target(
            GCS_player
            "${GCS_GST_RUNTIME_DIR}"
            "gstreamer-runtime"
            "Copying the GStreamer runtime directory"
        )
        gcs_install_directory_if_exists("${GCS_GST_RUNTIME_DIR}" "gstreamer-runtime")
    else()
        message(STATUS
            "GStreamer development files were found. On ${CMAKE_SYSTEM_NAME}, the build "
            "uses the system GStreamer runtime instead of copying it next to the app."
        )
        add_custom_command(TARGET GCS_player POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf
                "$<TARGET_FILE_DIR:GCS_player>/gstreamer-1.0"
                "$<TARGET_FILE_DIR:GCS_player>/gstreamer-runtime"
                "$<TARGET_FILE_DIR:GCS_player>/gstreamer-tools"
                "$<TARGET_FILE_DIR:GCS_player>/gio"
            COMMENT "Removing stale bundled GStreamer directories for system-runtime execution"
        )
    endif()
endif()

if(WIN32)
    get_filename_component(GCS_QT_INSTALL_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    find_program(GCS_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${GCS_QT_INSTALL_PREFIX}/bin"
    )
    if(GCS_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET GCS_player POST_BUILD
            COMMAND "${GCS_WINDEPLOYQT_EXECUTABLE}"
                --force
                --no-translations
                "$<TARGET_FILE:GCS_player>"
            COMMENT "Deploying the Qt runtime for local Windows execution"
        )
        add_custom_command(TARGET GCS_player POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_SOURCE_DIR}/qt.conf"
                "$<TARGET_FILE_DIR:GCS_player>/qt.conf"
            COMMAND ${CMAKE_COMMAND} -E rm -rf
                "$<TARGET_FILE_DIR:GCS_player>/plugins"
            COMMENT "Normalizing Qt plugin lookup for local Windows execution"
        )
        if(GCS_ENABLE_GSTREAMER AND GCS_GST_RUNTIME_DIR AND EXISTS "${GCS_GST_RUNTIME_DIR}")
            add_custom_command(TARGET GCS_player POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${GCS_GST_RUNTIME_DIR}"
                    "$<TARGET_FILE_DIR:GCS_player>"
                COMMENT "Overlaying matching GStreamer runtime DLLs next to the executable"
            )
        endif()
    else()
        message(WARNING
            "windeployqt was not found under ${GCS_QT_INSTALL_PREFIX}/bin. "
            "The built executable may require PATH adjustments on Windows."
        )
    endif()
endif()

if(GCS_ENABLE_GSTREAMER AND NOT WIN32)
    if(APPLE)
        install(CODE "
            file(REMOVE_RECURSE
                \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/GCS_player.app/Contents/MacOS/gstreamer-1.0\"
                \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/GCS_player.app/Contents/MacOS/gstreamer-runtime\"
                \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/GCS_player.app/Contents/MacOS/gstreamer-tools\"
                \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/GCS_player.app/Contents/MacOS/gio\"
            )
        ")
    else()
        install(CODE "
            file(REMOVE_RECURSE
                \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/gstreamer-1.0\"
                \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/gstreamer-runtime\"
                \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/gstreamer-tools\"
                \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/gio\"
            )
        ")
    endif()
endif()

qt_generate_deploy_app_script(
    TARGET GCS_player
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

include(CTest)

if(BUILD_TESTING)
    add_test(
        NAME GCS_player_smoke
        COMMAND $<TARGET_FILE:GCS_player> --smoke-test --smoke-test-ms 250
    )
    set_tests_properties(GCS_player_smoke PROPERTIES
        WORKING_DIRECTORY $<TARGET_FILE_DIR:GCS_player>
    )

    if(WIN32)
        set_tests_properties(GCS_player_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:GCS_player>"
        )
    elseif(UNIX)
        set_tests_properties(GCS_player_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endif()
endif()

qt_finalize_executable(GCS_player)
