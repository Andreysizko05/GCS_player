cmake_minimum_required(VERSION 4.2) # VS2026 req

# conan-cmake latest install
if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan_provider.cmake")
    message(STATUS "Downloading conan_provider.cmake")
    file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/refs/heads/develop2/conan_provider.cmake"
         "${CMAKE_BINARY_DIR}/conan_provider.cmake"
         TLS_VERIFY ON)
endif()

# specify path from CMakeLists to conanfile
set(MY_CONAN_FILE "${CMAKE_SOURCE_DIR}/conan/conanfile.py")
set(CONAN_PROVIDER_FILE "${CMAKE_BINARY_DIR}/conan_provider.cmake")

file(READ "${CONAN_PROVIDER_FILE}" _PROV_CONTENT)
    string(REPLACE 
           "COMMAND \${CONAN_COMMAND} install \${CMAKE_SOURCE_DIR}" 
           "COMMAND \${CONAN_COMMAND} install \"\${MY_CONAN_FILE}\"" 
           _PROV_CONTENT "${_PROV_CONTENT}")

    string(REPLACE 
           "EXISTS \"\${CMAKE_SOURCE_DIR}/conanfile.py\"" 
           "EXISTS \"\${MY_CONAN_FILE}\"" 
           _PROV_CONTENT "${_PROV_CONTENT}")
           
    string(REPLACE 
           "READ \"\${CMAKE_SOURCE_DIR}/conanfile.py\"" 
           "READ \"\${MY_CONAN_FILE}\"" 
           _PROV_CONTENT "${_PROV_CONTENT}")

    file(WRITE "${CONAN_PROVIDER_FILE}" "${_PROV_CONTENT}")
    message(STATUS "Patched conan_provider.cmake to use ${MY_CONAN_FILE}")

# conan install
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_BINARY_DIR}/conan_provider.cmake")

project(PID_GUI VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(Qt5_USE_STATIC_LIBS ON)    # QTcreator
#set(Qt5_USE_STATIC_RUNTIME ON) # QTcreator

# Specify Build_type
set(CONAN_INSTALL_BUILD_CONFIGURATIONS "${CMAKE_BUILD_TYPE}" CACHE STRING "Conan configs" FORCE)
# Set to "Release" if not find
if(NOT CONAN_INSTALL_BUILD_CONFIGURATIONS)
    set(CONAN_INSTALL_BUILD_CONFIGURATIONS "Release")
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools SerialPort)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools SerialPort)

set(TS_FILES PID_GUI_uk_UA.ts)

set(PROJECT_SOURCES
        main.cpp
        MainWindow.cpp
        MainWindow.h
        MainWindow.ui
        ${TS_FILES}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(PID_GUI
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET PID_GUI APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    if(ANDROID)
        add_library(PID_GUI SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(PID_GUI
            ${PROJECT_SOURCES}
        )
    endif()

    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

target_link_libraries(PID_GUI PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

qt_import_plugins(KRAMM
    INCLUDE Qt6::QWindowsIntegrationPlugin
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.PID_GUI)
endif()
set_target_properties(PID_GUI PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS PID_GUI
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(PID_GUI)
endif()
