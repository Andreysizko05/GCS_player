include_guard(GLOBAL)

option(GCS_FETCH_QT_WITH_AQT "Automatically install Qt with aqtinstall during configure." ON)
set(GCS_QT_VERSION "6.10.0" CACHE STRING "Qt version installed via aqtinstall.")
set(GCS_QT_INSTALL_ROOT "${PROJECT_SOURCE_DIR}/.qt-sdk" CACHE PATH "Root directory that stores aqt-installed Qt SDKs.")
set(GCS_QT_EXTRA_MODULES "qtmultimedia" CACHE STRING "Additional Qt modules installed with aqtinstall.")
set(GCS_QT_HOST "" CACHE STRING "Optional aqt host override, for example windows, mac, or linux.")
set(GCS_QT_ARCH "" CACHE STRING "Optional aqt desktop architecture override, for example clang_64 or win64_msvc2022_64.")
set(GCS_QT_ARCH_DIR "" CACHE STRING "Optional installed Qt directory name override, for example macos or msvc2022_64.")
set(GCS_AQT_COMMAND "" CACHE STRING "Optional custom command used to invoke aqtinstall.")
set(GCS_AQT_VENV_DIR "${CMAKE_BINARY_DIR}/.aqt-venv" CACHE PATH "Build-local virtual environment used when uv and aqt are unavailable.")

function(gcs_resolve_qt_layout out_host out_arch out_dir)
    if(GCS_QT_HOST)
        set(_host "${GCS_QT_HOST}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(_host "windows")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(_host "mac")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_host "linux")
    else()
        message(FATAL_ERROR
            "Automatic Qt bootstrap is not configured for ${CMAKE_SYSTEM_NAME}. "
            "Set GCS_FETCH_QT_WITH_AQT=OFF and provide Qt6_DIR manually."
        )
    endif()

    if(GCS_QT_ARCH)
        set(_arch "${GCS_QT_ARCH}")
    elseif(_host STREQUAL "windows")
        if(GCS_QT_VERSION VERSION_GREATER_EQUAL "6.8.0")
            set(_arch "win64_msvc2022_64")
        else()
            set(_arch "win64_msvc2019_64")
        endif()
    elseif(_host STREQUAL "mac")
        set(_arch "clang_64")
    elseif(_host STREQUAL "linux")
        if(GCS_QT_VERSION VERSION_GREATER_EQUAL "6.7.0")
            set(_arch "linux_gcc_64")
        else()
            set(_arch "gcc_64")
        endif()
    else()
        message(FATAL_ERROR
            "Automatic Qt bootstrap does not know which desktop architecture to use for "
            "the aqt host '${_host}'. Set GCS_QT_ARCH explicitly."
        )
    endif()

    if(GCS_QT_ARCH_DIR)
        set(_dir "${GCS_QT_ARCH_DIR}")
    elseif(_host STREQUAL "windows")
        if(GCS_QT_VERSION VERSION_GREATER_EQUAL "6.8.0")
            set(_dir "msvc2022_64")
        else()
            set(_dir "msvc2019_64")
        endif()
    elseif(_host STREQUAL "mac")
        if(GCS_QT_VERSION VERSION_GREATER_EQUAL "6.1.2")
            set(_dir "macos")
        else()
            set(_dir "clang_64")
        endif()
    elseif(_host STREQUAL "linux")
        set(_dir "gcc_64")
    else()
        set(_dir "${_arch}")
    endif()

    set(${out_host} "${_host}" PARENT_SCOPE)
    set(${out_arch} "${_arch}" PARENT_SCOPE)
    set(${out_dir} "${_dir}" PARENT_SCOPE)
endfunction()

function(gcs_resolve_aqt_command out_var)
    if(GCS_AQT_COMMAND)
        separate_arguments(_aqt_command NATIVE_COMMAND "${GCS_AQT_COMMAND}")
        set(${out_var} "${_aqt_command}" PARENT_SCOPE)
        return()
    endif()

    find_program(_uv_executable NAMES uv)
    if(_uv_executable)
        set(${out_var} "${_uv_executable};tool;run;--from;aqtinstall;aqt" PARENT_SCOPE)
        return()
    endif()

    find_program(_aqt_executable NAMES aqt)
    if(_aqt_executable)
        set(${out_var} "${_aqt_executable}" PARENT_SCOPE)
        return()
    endif()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    if(WIN32)
        set(_aqt_python "${GCS_AQT_VENV_DIR}/Scripts/python.exe")
    else()
        set(_aqt_python "${GCS_AQT_VENV_DIR}/bin/python")
    endif()

    if(NOT EXISTS "${_aqt_python}")
        message(STATUS "Creating a local virtual environment for aqtinstall at ${GCS_AQT_VENV_DIR}")
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -m venv "${GCS_AQT_VENV_DIR}"
            RESULT_VARIABLE _venv_result
            OUTPUT_VARIABLE _venv_stdout
            ERROR_VARIABLE _venv_stderr
        )
        if(NOT _venv_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to create the local aqtinstall virtual environment.\n"
                "${_venv_stdout}\n${_venv_stderr}"
            )
        endif()
    endif()

    execute_process(
        COMMAND "${_aqt_python}" -m aqt version
        RESULT_VARIABLE _aqt_check_result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT _aqt_check_result EQUAL 0)
        message(STATUS "Installing aqtinstall into ${GCS_AQT_VENV_DIR}")
        execute_process(
            COMMAND "${_aqt_python}" -m pip install --upgrade aqtinstall
            RESULT_VARIABLE _pip_result
            OUTPUT_VARIABLE _pip_stdout
            ERROR_VARIABLE _pip_stderr
        )
        if(NOT _pip_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to install aqtinstall into ${GCS_AQT_VENV_DIR}.\n"
                "${_pip_stdout}\n${_pip_stderr}"
            )
        endif()
    endif()

    set(${out_var} "${_aqt_python};-m;aqt" PARENT_SCOPE)
endfunction()

if(NOT GCS_FETCH_QT_WITH_AQT)
    message(STATUS "Automatic Qt bootstrap is disabled (GCS_FETCH_QT_WITH_AQT=OFF)")
    return()
endif()

if(DEFINED Qt6_DIR AND EXISTS "${Qt6_DIR}/Qt6Config.cmake")
    get_filename_component(_qt_root_from_dir "${Qt6_DIR}/../../.." ABSOLUTE)
    list(PREPEND CMAKE_PREFIX_PATH "${_qt_root_from_dir}")
    message(STATUS "Using Qt from Qt6_DIR: ${Qt6_DIR}")
    return()
endif()

gcs_resolve_qt_layout(_gcs_qt_host _gcs_qt_arch _gcs_qt_dir_name)
set(_gcs_qt_root_dir "${GCS_QT_INSTALL_ROOT}/${GCS_QT_VERSION}/${_gcs_qt_dir_name}")
set(_gcs_qt_cmake_dir "${_gcs_qt_root_dir}/lib/cmake/Qt6")

if(NOT EXISTS "${_gcs_qt_cmake_dir}/Qt6Config.cmake")
    gcs_resolve_aqt_command(_gcs_aqt_command)

    set(_gcs_qt_modules_arg "${GCS_QT_EXTRA_MODULES}")
    string(REPLACE ";" " " _gcs_qt_modules_arg "${_gcs_qt_modules_arg}")
    separate_arguments(_gcs_qt_modules NATIVE_COMMAND "${_gcs_qt_modules_arg}")

    set(_gcs_aqt_install_args
        install-qt
        "${_gcs_qt_host}"
        desktop
        "${GCS_QT_VERSION}"
        "${_gcs_qt_arch}"
        -O "${GCS_QT_INSTALL_ROOT}"
    )
    if(_gcs_qt_modules)
        list(APPEND _gcs_aqt_install_args -m ${_gcs_qt_modules})
    endif()

    message(STATUS
        "Installing Qt ${GCS_QT_VERSION} for ${CMAKE_SYSTEM_NAME} via aqtinstall "
        "into ${_gcs_qt_root_dir}"
    )
    execute_process(
        COMMAND ${_gcs_aqt_command} ${_gcs_aqt_install_args}
        RESULT_VARIABLE _gcs_aqt_result
        OUTPUT_VARIABLE _gcs_aqt_stdout
        ERROR_VARIABLE _gcs_aqt_stderr
    )
    if(NOT _gcs_aqt_result EQUAL 0)
        message(FATAL_ERROR
            "Qt bootstrap via aqtinstall failed.\n"
            "Command: ${_gcs_aqt_command};${_gcs_aqt_install_args}\n"
            "${_gcs_aqt_stdout}\n${_gcs_aqt_stderr}"
        )
    endif()
endif()

if(NOT EXISTS "${_gcs_qt_cmake_dir}/Qt6Config.cmake")
    message(FATAL_ERROR
        "Qt was expected at ${_gcs_qt_cmake_dir}, but Qt6Config.cmake was not found."
    )
endif()

list(PREPEND CMAKE_PREFIX_PATH "${_gcs_qt_root_dir}")
set(Qt6_DIR "${_gcs_qt_cmake_dir}" CACHE PATH "Path to Qt6Config.cmake" FORCE)
set(GCS_QT_ROOT_DIR "${_gcs_qt_root_dir}" CACHE INTERNAL "Resolved Qt installation root.")

message(STATUS "Using Qt from ${_gcs_qt_root_dir}")
