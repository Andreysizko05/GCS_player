cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

project(GCS_player VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)

set(GCS_CORE_INC_DIR "${PROJECT_SOURCE_DIR}/Core/Inc")
set(GCS_CORE_SRC_DIR "${PROJECT_SOURCE_DIR}/Core/Src")
set(GCS_VIDEO_MODULE_INC_DIR "${PROJECT_SOURCE_DIR}/Modules/Video/Inc")
set(GCS_VIDEO_MODULE_SRC_DIR "${PROJECT_SOURCE_DIR}/Modules/Video/Src")

include(cmake/BootstrapQt.cmake)
include(cmake/BootstrapGStreamer.cmake)

gcs_bootstrap_gstreamer()

if(GCS_GSTREAMER_ROOT)
    # FindPkgConfig may resolve -lfoo entries with find_library(), so make the
    # selected SDK root the first place CMake searches for GStreamer dylibs.
    list(PREPEND CMAKE_PREFIX_PATH "${GCS_GSTREAMER_ROOT}")
    if(EXISTS "${GCS_GSTREAMER_ROOT}/lib")
        list(PREPEND CMAKE_LIBRARY_PATH "${GCS_GSTREAMER_ROOT}/lib")
    endif()
    if(EXISTS "${GCS_GSTREAMER_ROOT}/include")
        list(PREPEND CMAKE_INCLUDE_PATH "${GCS_GSTREAMER_ROOT}/include")
    endif()
endif()

find_package(Qt6 ${GCS_MINIMUM_QT_VERSION} REQUIRED COMPONENTS Widgets LinguistTools Multimedia MultimediaWidgets)
find_package(PkgConfig REQUIRED)

if(NOT GCS_GSTREAMER_ROOT)
    message(FATAL_ERROR
        "GStreamer is required, but no managed, external, or system GStreamer root was selected."
    )
endif()

get_cmake_property(_gcs_cache_vars CACHE_VARIABLES)
foreach(_gcs_cache_var IN LISTS _gcs_cache_vars)
    if(_gcs_cache_var MATCHES "^pkgcfg_lib_GSTREAMER_"
        OR _gcs_cache_var MATCHES "^GSTREAMER_"
        OR _gcs_cache_var STREQUAL "__pkg_config_arguments_GSTREAMER"
        OR _gcs_cache_var STREQUAL "__pkg_config_checked_GSTREAMER")
        unset(${_gcs_cache_var} CACHE)
    endif()
endforeach()

if(GCS_GSTREAMER_RESOLVED_VERSION)
    set(_gcs_required_gstreamer_version "${GCS_GSTREAMER_RESOLVED_VERSION}")
else()
    set(_gcs_required_gstreamer_version "${GCS_GSTREAMER_VERSION}")
endif()

pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
    "gstreamer-1.0 = ${_gcs_required_gstreamer_version}"
    "gstreamer-app-1.0 = ${_gcs_required_gstreamer_version}"
    "gstreamer-video-1.0 = ${_gcs_required_gstreamer_version}"
)
gcs_verify_gstreamer_pkg_config_paths()
gcs_verify_gstreamer_plugins()
message(STATUS
    "GStreamer is required and enabled with ${_gcs_required_gstreamer_version} "
    "from ${GCS_GSTREAMER_ROOT}"
)

set(GCS_PLAYER_SOURCES
    "${GCS_CORE_SRC_DIR}/main.cpp"
    "${GCS_CORE_SRC_DIR}/MainWindow.cpp"
    "${GCS_CORE_INC_DIR}/MainWindow.h"
    "${GCS_CORE_SRC_DIR}/MainWindow.ui"
    "${GCS_VIDEO_MODULE_SRC_DIR}/GstVideoReceiver.cpp"
    "${GCS_VIDEO_MODULE_INC_DIR}/GstVideoReceiver.h"
    "${GCS_VIDEO_MODULE_SRC_DIR}/UsbCameraManager.cpp"
    "${GCS_VIDEO_MODULE_INC_DIR}/UsbCameraManager.h"
    "${GCS_VIDEO_MODULE_SRC_DIR}/VideoSettingsConfig.cpp"
    "${GCS_VIDEO_MODULE_INC_DIR}/VideoSettingsConfig.h"
)

qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES uk_UA)

qt_add_executable(GCS_player
    MANUAL_FINALIZATION
    ${GCS_PLAYER_SOURCES}
)

qt_add_translations(GCS_player
    TS_FILES "${GCS_CORE_SRC_DIR}/GCS_player_uk_UA.ts"
    RESOURCE_PREFIX /i18n
)

target_include_directories(GCS_player PRIVATE
    "${GCS_CORE_INC_DIR}"
    "${GCS_VIDEO_MODULE_INC_DIR}"
)

target_link_libraries(GCS_player PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    PkgConfig::GSTREAMER
)

if(WIN32)
    target_link_libraries(GCS_player PRIVATE
        strmiids
        ole32
        oleaut32
    )
endif()

if(NOT WIN32 AND GSTREAMER_LIBRARY_DIRS)
    set(_gcs_append_gstreamer_rpath TRUE)
    # Homebrew pkg-config modules already pass -Wl,-rpath for their lib dirs.
    if(APPLE AND GCS_GSTREAMER_SOURCE MATCHES "^homebrew")
        set(_gcs_append_gstreamer_rpath FALSE)
    endif()
    if(_gcs_append_gstreamer_rpath)
        set_property(TARGET GCS_player APPEND PROPERTY BUILD_RPATH "${GSTREAMER_LIBRARY_DIRS}")
        set_property(TARGET GCS_player APPEND PROPERTY INSTALL_RPATH "${GSTREAMER_LIBRARY_DIRS}")
    endif()
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

if(PkgConfig_FOUND)
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
            "GStreamer development files were found under ${GCS_GSTREAMER_ROOT}. "
            "On ${CMAKE_SYSTEM_NAME}, stale bundled GStreamer folders are removed so the "
            "resolved SDK/runtime root is used consistently."
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
        if(GCS_GST_RUNTIME_DIR AND EXISTS "${GCS_GST_RUNTIME_DIR}")
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

if(NOT WIN32)
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

if(WIN32 OR APPLE)
    qt_generate_deploy_app_script(
        TARGET GCS_player
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
endif()

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

set(CPACK_PACKAGE_NAME "gcs-player")
set(CPACK_PACKAGE_VENDOR "KPI")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt/GStreamer desktop video receiver")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "GCS_player maintainers")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "GCS_player")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")

string(TOLOWER "${CMAKE_SYSTEM_NAME}" GCS_CPACK_SYSTEM_NAME)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" GCS_CPACK_ARCH_NAME)
if(NOT GCS_CPACK_ARCH_NAME)
    set(GCS_CPACK_ARCH_NAME "unknown")
endif()
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${GCS_CPACK_SYSTEM_NAME}-${GCS_CPACK_ARCH_NAME}"
)

if(UNIX AND NOT APPLE)
    if(QT6_INSTALL_PREFIX AND QT6_INSTALL_PLUGINS)
        set(GCS_QT_PLUGIN_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
    else()
        set(GCS_QT_PLUGIN_DIR "${GCS_QT_ROOT_DIR}/plugins")
    endif()

    set(GCS_BUNDLE_LIBRARY_DIRS
        "${GSTREAMER_LIBRARY_DIRS}"
        "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}"
        "${GCS_GSTREAMER_ROOT}/lib"
    )
    list(REMOVE_DUPLICATES GCS_BUNDLE_LIBRARY_DIRS)
    string(REPLACE ";" "|" GCS_BUNDLE_LIBRARY_DIRS_ARG "${GCS_BUNDLE_LIBRARY_DIRS}")
    string(REPLACE ";" "|" GCS_BUNDLE_GSTREAMER_PLUGINS_ARG "${GCS_REQUIRED_GSTREAMER_PLUGINS}")

    set(GCS_LINUX_PORTABLE_BUNDLE_NAME "${CPACK_PACKAGE_FILE_NAME}-portable")
    set(GCS_LINUX_PORTABLE_BUNDLE_DIR "${CMAKE_BINARY_DIR}/${GCS_LINUX_PORTABLE_BUNDLE_NAME}")
    set(GCS_LINUX_PORTABLE_BUNDLE_ARCHIVE
        "${CMAKE_BINARY_DIR}/${GCS_LINUX_PORTABLE_BUNDLE_NAME}.tar.gz"
    )

    add_custom_target(linux-portable-bundle
        COMMAND "${CMAKE_COMMAND}"
            "-DGCS_BUNDLE_EXECUTABLE=$<TARGET_FILE:GCS_player>"
            "-DGCS_BUNDLE_DIR=${GCS_LINUX_PORTABLE_BUNDLE_DIR}"
            "-DGCS_BUNDLE_ARCHIVE=${GCS_LINUX_PORTABLE_BUNDLE_ARCHIVE}"
            "-DGCS_BUNDLE_QT_PLUGIN_DIR=${GCS_QT_PLUGIN_DIR}"
            "-DGCS_BUNDLE_GST_INSPECT=${GCS_GST_PREFIX}/bin/gst-inspect-1.0"
            "-DGCS_BUNDLE_GST_PLUGIN_SCANNER_DIR=${GCS_GST_PLUGIN_SCANNER_DIR}"
            "-DGCS_BUNDLE_GIO_MODULE_DIR=${GCS_GST_GIO_MODULE_DIR}"
            "-DGCS_BUNDLE_LIBRARY_DIRS=${GCS_BUNDLE_LIBRARY_DIRS_ARG}"
            "-DGCS_BUNDLE_GSTREAMER_PLUGINS=${GCS_BUNDLE_GSTREAMER_PLUGINS_ARG}"
            -P "${PROJECT_SOURCE_DIR}/cmake/BundleLinuxRuntime.cmake"
        DEPENDS GCS_player
        BYPRODUCTS
            "${GCS_LINUX_PORTABLE_BUNDLE_ARCHIVE}"
        COMMENT "Creating portable Linux runtime bundle"
        VERBATIM
    )
endif()

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "GCS_player maintainers")
    set(CPACK_DEBIAN_PACKAGE_SECTION "video")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    if(GCS_CPACK_ARCH_NAME MATCHES "^(x86_64|amd64)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    elseif(GCS_CPACK_ARCH_NAME MATCHES "^(aarch64|arm64)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    endif()
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "qt6-qpa-plugins, qt6-wayland, gstreamer1.0-tools, gstreamer1.0-plugins-base, gstreamer1.0-plugins-good, gstreamer1.0-plugins-bad, gstreamer1.0-libav"
    )
endif()

include(CPack)
