cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

project(GCS_player VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(PkgConfig QUIET)

set(GCS_ENABLE_GSTREAMER OFF)
if(PkgConfig_FOUND)
    pkg_check_modules(GSTREAMER IMPORTED_TARGET QUIET
        gstreamer-1.0
        gstreamer-app-1.0
        gstreamer-video-1.0
    )
    if(TARGET PkgConfig::GSTREAMER)
        set(GCS_ENABLE_GSTREAMER ON)
        message(STATUS "GStreamer receiver support is enabled")
    else()
        message(STATUS "GStreamer development files were not found, building without video receiver")
    endif()
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
    RUNTIME_DEPENDENCY_SET GCS_player_runtime_deps
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(GCS_ENABLE_GSTREAMER AND DEFINED VCPKG_TARGET_TRIPLET)
    set(GCS_VCPKG_TRIPLET_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
    set(GCS_GST_GIO_MODULE_DIR "${GCS_VCPKG_TRIPLET_DIR}/lib/gio/modules")
    set(GCS_GST_RUNTIME_DIR "${GCS_VCPKG_TRIPLET_DIR}/bin")
    set(GCS_GST_TOOL_DIR "${GCS_VCPKG_TRIPLET_DIR}/tools/gstreamer")

    if(WIN32)
        set(GCS_GST_PLUGIN_DIR "${GCS_VCPKG_TRIPLET_DIR}/plugins/gstreamer")
    else()
        set(GCS_GST_PLUGIN_DIR "${GCS_VCPKG_TRIPLET_DIR}/lib/gstreamer-1.0")
    endif()

    if(EXISTS "${GCS_GST_PLUGIN_DIR}")
        add_custom_command(TARGET GCS_player POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${GCS_GST_PLUGIN_DIR}"
                "$<TARGET_FILE_DIR:GCS_player>/gstreamer-1.0"
            COMMENT "Copying GStreamer plugin directory for local execution"
        )
        install(DIRECTORY "${GCS_GST_PLUGIN_DIR}/"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/gstreamer-1.0"
        )
    endif()

    if(EXISTS "${GCS_GST_GIO_MODULE_DIR}")
        add_custom_command(TARGET GCS_player POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${GCS_GST_GIO_MODULE_DIR}"
                "$<TARGET_FILE_DIR:GCS_player>/gio/modules"
            COMMENT "Copying GIO modules for GStreamer"
        )
        install(DIRECTORY "${GCS_GST_GIO_MODULE_DIR}/"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/gio/modules"
        )
    endif()

    if(EXISTS "${GCS_GST_RUNTIME_DIR}")
        add_custom_command(TARGET GCS_player POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${GCS_GST_RUNTIME_DIR}"
                "$<TARGET_FILE_DIR:GCS_player>/gstreamer-runtime"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${GCS_GST_RUNTIME_DIR}"
                "$<TARGET_FILE_DIR:GCS_player>/gstreamer-tools"
            COMMENT "Copying GStreamer release runtime for plugin dependencies"
        )
        install(DIRECTORY "${GCS_GST_RUNTIME_DIR}/"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/gstreamer-runtime"
        )
    endif()

    if(EXISTS "${GCS_GST_TOOL_DIR}")
        add_custom_command(TARGET GCS_player POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${GCS_GST_TOOL_DIR}"
                "$<TARGET_FILE_DIR:GCS_player>/gstreamer-tools"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${GCS_GST_RUNTIME_DIR}"
                "$<TARGET_FILE_DIR:GCS_player>/gstreamer-tools"
            COMMENT "Copying GStreamer helper tools"
        )
        install(DIRECTORY "${GCS_GST_TOOL_DIR}/"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/gstreamer-tools"
        )
    endif()
endif()

if(WIN32)
    set(QT_CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/qt.conf")
    set(QT_PLUGINS_DIR_RELEASE "${Qt6_DIR}/../../Qt6/plugins")
    set(QT_PLUGINS_DIR_DEBUG "${Qt6_DIR}/../../debug/Qt6/plugins")

    add_custom_command(TARGET GCS_player POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT_CONF_FILE}"
            "$<TARGET_FILE_DIR:GCS_player>/qt.conf"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:GCS_player>/plugins/platforms"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:GCS_player>/plugins/styles"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${QT_PLUGINS_DIR_DEBUG}/platforms/qwindowsd.dll,${QT_PLUGINS_DIR_RELEASE}/platforms/qwindows.dll>"
            "$<IF:$<CONFIG:Debug>,$<TARGET_FILE_DIR:GCS_player>/plugins/platforms/qwindowsd.dll,$<TARGET_FILE_DIR:GCS_player>/plugins/platforms/qwindows.dll>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${QT_PLUGINS_DIR_DEBUG}/styles/qmodernwindowsstyled.dll,${QT_PLUGINS_DIR_RELEASE}/styles/qmodernwindowsstyle.dll>"
            "$<IF:$<CONFIG:Debug>,$<TARGET_FILE_DIR:GCS_player>/plugins/styles/qmodernwindowsstyled.dll,$<TARGET_FILE_DIR:GCS_player>/plugins/styles/qmodernwindowsstyle.dll>"
        COMMENT "Copying Qt runtime plugins for local Windows execution"
    )

    install(FILES "${QT_CONF_FILE}"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES
            "${QT_PLUGINS_DIR_DEBUG}/platforms/qwindowsd.dll"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins/platforms"
        CONFIGURATIONS Debug
    )
    install(FILES
            "${QT_PLUGINS_DIR_RELEASE}/platforms/qwindows.dll"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins/platforms"
        CONFIGURATIONS Release RelWithDebInfo MinSizeRel
    )
    install(FILES
            "${QT_PLUGINS_DIR_DEBUG}/styles/qmodernwindowsstyled.dll"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins/styles"
        CONFIGURATIONS Debug
    )
    install(FILES
            "${QT_PLUGINS_DIR_RELEASE}/styles/qmodernwindowsstyle.dll"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins/styles"
        CONFIGURATIONS Release RelWithDebInfo MinSizeRel
    )
    install(RUNTIME_DEPENDENCY_SET GCS_player_runtime_deps
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        PRE_EXCLUDE_REGEXES "api-ms-" "ext-ms-" "[Hh][Vv][Ss][Ii]FileTrust\\.dll"
        POST_EXCLUDE_REGEXES ".*/system32/.*\\.dll"
    )
else()
    qt_generate_deploy_app_script(
        TARGET GCS_player
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
endif()

qt_finalize_executable(GCS_player)
