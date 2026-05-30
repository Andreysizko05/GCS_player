include_guard(GLOBAL)

option(GCS_FETCH_QT_WITH_AQT "Automatically install Qt with aqtinstall during configure." ON)
option(GCS_QT_FORCE_DOWNLOAD "Refresh the managed Qt SDK with aqtinstall; cannot be combined with external/system Qt." OFF)
option(GCS_ALLOW_EXTERNAL_QT "Allow an explicitly provided Qt SDK instead of the aqt-managed SDK." OFF)
option(GCS_USE_SYSTEM_QT "Allow Qt discovery from common system environment variables and PATH." OFF)
option(GCS_PREFER_SYSTEM_QT "Prefer a complete system Qt SDK before falling back to the aqt-managed SDK." ON)
set(GCS_MINIMUM_QT_VERSION "6.2.0" CACHE STRING
    "Minimum acceptable Qt version for auto-discovered or explicitly provided SDKs."
)
set(GCS_QT_VERSION "6.10.0" CACHE STRING "Qt version installed via aqtinstall.")
set(GCS_QT_INSTALL_ROOT "${PROJECT_SOURCE_DIR}/External/Qt" CACHE PATH "Root directory that stores aqt-installed Qt SDKs.")
set(GCS_EXTERNAL_QT_ROOT "" CACHE PATH "Explicit Qt SDK root used only when GCS_ALLOW_EXTERNAL_QT=ON.")
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
            "Use GCS_ALLOW_EXTERNAL_QT=ON or GCS_USE_SYSTEM_QT=ON with a verified Qt ${GCS_QT_VERSION} SDK."
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

function(gcs_get_qt_config_dir_from_root root_dir out_var)
    if(NOT root_dir)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    get_filename_component(_root_dir "${root_dir}" ABSOLUTE)
    set(_qt_config_dir "${_root_dir}/lib/cmake/Qt6")
    if(EXISTS "${_qt_config_dir}/Qt6Config.cmake")
        set(${out_var} "${_qt_config_dir}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

function(gcs_pin_qt_package_dirs qt_root)
    if(NOT qt_root)
        return()
    endif()

    get_filename_component(_qt_root "${qt_root}" ABSOLUTE)
    file(GLOB _qt_package_dirs LIST_DIRECTORIES true "${_qt_root}/lib/cmake/Qt6*")
    foreach(_qt_package_dir IN LISTS _qt_package_dirs)
        get_filename_component(_qt_package_name "${_qt_package_dir}" NAME)
        if(EXISTS "${_qt_package_dir}/${_qt_package_name}Config.cmake")
            set(
                "${_qt_package_name}_DIR"
                "${_qt_package_dir}"
                CACHE PATH "Path to ${_qt_package_name}Config.cmake"
                FORCE
            )
        endif()
    endforeach()
endfunction()

function(gcs_get_qt_root_from_config_dir qt_config_dir out_var)
    get_filename_component(_qt_root "${qt_config_dir}/../../.." ABSOLUTE)
    set(${out_var} "${_qt_root}" PARENT_SCOPE)
endfunction()

function(gcs_read_qt_config_version qt_config_dir out_var)
    set(_qt_version "")
    foreach(_version_file IN ITEMS
        "${qt_config_dir}/Qt6ConfigVersionImpl.cmake"
        "${qt_config_dir}/Qt6ConfigVersion.cmake"
        "${qt_config_dir}/../Qt6Core/Qt6CoreConfigVersionImpl.cmake"
        "${qt_config_dir}/../Qt6Core/Qt6CoreConfigVersion.cmake"
    )
        if(EXISTS "${_version_file}")
            file(STRINGS "${_version_file}" _version_lines REGEX "PACKAGE_VERSION")
            foreach(_line IN LISTS _version_lines)
                if(_line MATCHES "PACKAGE_VERSION[ \t]+\"([0-9]+\\.[0-9]+\\.[0-9]+)\"")
                    set(_qt_version "${CMAKE_MATCH_1}")
                    break()
                endif()
            endforeach()
        endif()

        if(_qt_version)
            break()
        endif()
    endforeach()

    set(${out_var} "${_qt_version}" PARENT_SCOPE)
endfunction()

function(gcs_require_qt_version qt_config_dir expected_version)
    gcs_read_qt_config_version("${qt_config_dir}" _actual_qt_version)
    if(NOT _actual_qt_version)
        message(FATAL_ERROR
            "Could not read the Qt version from ${qt_config_dir}. "
            "Use an aqt-managed Qt SDK or provide a complete Qt SDK root."
        )
    endif()

    if(NOT _actual_qt_version STREQUAL expected_version)
        message(FATAL_ERROR
            "Qt version mismatch.\n"
            "Expected: ${expected_version}\n"
            "Found:    ${_actual_qt_version}\n"
            "Qt path:  ${qt_config_dir}"
        )
    endif()

    set(GCS_QT_RESOLVED_VERSION "${_actual_qt_version}" CACHE INTERNAL "Resolved Qt version." FORCE)
endfunction()

function(gcs_check_qt_module_configs qt_root out_ok out_missing_modules)
    set(_required_modules Widgets LinguistTools Multimedia MultimediaWidgets)
    set(_missing_modules)

    foreach(_module IN LISTS _required_modules)
        set(_module_config "${qt_root}/lib/cmake/Qt6${_module}/Qt6${_module}Config.cmake")
        if(NOT EXISTS "${_module_config}")
            list(APPEND _missing_modules "Qt6${_module}")
        endif()
    endforeach()

    if(_missing_modules)
        set(${out_ok} FALSE PARENT_SCOPE)
        set(${out_missing_modules} "${_missing_modules}" PARENT_SCOPE)
    else()
        set(${out_ok} TRUE PARENT_SCOPE)
        set(${out_missing_modules} "" PARENT_SCOPE)
    endif()
endfunction()

function(gcs_require_qt_module_configs qt_root)
    gcs_check_qt_module_configs("${qt_root}" _modules_ok _missing_modules)
    if(NOT _modules_ok)
        list(JOIN _missing_modules ", " _missing_modules_text)
        message(FATAL_ERROR
            "Qt at ${qt_root} is incomplete: missing ${_missing_modules_text}."
        )
    endif()
endfunction()

function(gcs_validate_qt_candidate qt_root qt_config_dir minimum_version out_ok out_version out_reason)
    set(_candidate_ok FALSE)
    set(_candidate_version "")
    set(_candidate_reason "")

    if(NOT qt_root OR NOT qt_config_dir OR NOT EXISTS "${qt_config_dir}/Qt6Config.cmake")
        set(_candidate_reason "Qt6Config.cmake was not found")
    else()
        gcs_read_qt_config_version("${qt_config_dir}" _candidate_version)
        if(NOT _candidate_version)
            set(_candidate_reason "could not read the Qt version from ${qt_config_dir}")
        elseif(_candidate_version VERSION_LESS "${minimum_version}")
            set(_candidate_reason
                "Qt ${_candidate_version} is below the minimum supported version ${minimum_version}"
            )
        else()
            gcs_check_qt_module_configs("${qt_root}" _modules_ok _missing_modules)
            if(NOT _modules_ok)
                list(JOIN _missing_modules ", " _missing_modules_text)
                set(_candidate_reason "missing required modules: ${_missing_modules_text}")
            else()
                set(_candidate_ok TRUE)
            endif()
        endif()
    endif()

    set(${out_ok} "${_candidate_ok}" PARENT_SCOPE)
    set(${out_version} "${_candidate_version}" PARENT_SCOPE)
    set(${out_reason} "${_candidate_reason}" PARENT_SCOPE)
endfunction()

function(gcs_qt_path_must_be_under_root path root_dir label)
    get_filename_component(_path_abs "${path}" ABSOLUTE)
    get_filename_component(_root_abs "${root_dir}" ABSOLUTE)
    file(TO_CMAKE_PATH "${_path_abs}" _path_abs)
    file(TO_CMAKE_PATH "${_root_abs}" _root_abs)

    string(APPEND _path_abs "/")
    string(APPEND _root_abs "/")
    string(FIND "${_path_abs}" "${_root_abs}" _root_pos)
    if(NOT _root_pos EQUAL 0)
        message(FATAL_ERROR
            "${label} must stay under ${root_dir}.\n"
            "${label}: ${path}"
        )
    endif()
endfunction()

function(gcs_try_qt_root_candidate candidate out_root out_config_dir)
    set(${out_root} "" PARENT_SCOPE)
    set(${out_config_dir} "" PARENT_SCOPE)

    if(NOT candidate)
        return()
    endif()

    get_filename_component(_candidate "${candidate}" ABSOLUTE)
    if(EXISTS "${_candidate}/Qt6Config.cmake")
        gcs_get_qt_root_from_config_dir("${_candidate}" _qt_root)
        set(${out_root} "${_qt_root}" PARENT_SCOPE)
        set(${out_config_dir} "${_candidate}" PARENT_SCOPE)
        return()
    endif()

    gcs_get_qt_config_dir_from_root("${_candidate}" _qt_config_dir)
    if(_qt_config_dir)
        set(${out_root} "${_candidate}" PARENT_SCOPE)
        set(${out_config_dir} "${_qt_config_dir}" PARENT_SCOPE)
    endif()
endfunction()

function(gcs_find_system_qt out_root out_config_dir out_version out_summary)
    set(_candidate_roots)
    set(_candidate_config_dirs)
    set(_rejection_messages)

    if(DEFINED Qt6_DIR AND Qt6_DIR)
        list(APPEND _candidate_config_dirs "${Qt6_DIR}")
    endif()

    foreach(_env_name IN ITEMS Qt6_DIR QT_DIR Qt_DIR)
        if(DEFINED ENV{${_env_name}} AND NOT "$ENV{${_env_name}}" STREQUAL "")
            list(APPEND _candidate_config_dirs "$ENV{${_env_name}}")
        endif()
    endforeach()

    foreach(_env_name IN ITEMS QTDIR Qt6_ROOT QT6_ROOT Qt_ROOT QT_ROOT Qt6_ROOT_DIR)
        if(DEFINED ENV{${_env_name}} AND NOT "$ENV{${_env_name}}" STREQUAL "")
            list(APPEND _candidate_roots "$ENV{${_env_name}}")
        endif()
    endforeach()

    if(DEFINED ENV{CMAKE_PREFIX_PATH} AND NOT "$ENV{CMAKE_PREFIX_PATH}" STREQUAL "")
        cmake_path(CONVERT "$ENV{CMAKE_PREFIX_PATH}" TO_CMAKE_PATH_LIST _prefix_path_candidates)
        list(APPEND _candidate_roots ${_prefix_path_candidates})
    endif()

    find_program(_qt_pkg_config NAMES pkg-config pkgconf pkg-config.exe pkgconf.exe)
    if(_qt_pkg_config)
        execute_process(
            COMMAND "${_qt_pkg_config}" --exists
                Qt6Core
                Qt6Widgets
                Qt6Multimedia
                Qt6MultimediaWidgets
            RESULT_VARIABLE _qt_pkg_exists_result
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(_qt_pkg_exists_result EQUAL 0)
            execute_process(
                COMMAND "${_qt_pkg_config}" --variable=prefix Qt6Core
                RESULT_VARIABLE _qt_pkg_prefix_result
                OUTPUT_VARIABLE _qt_pkg_prefix
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(_qt_pkg_prefix_result EQUAL 0 AND _qt_pkg_prefix)
                list(APPEND _candidate_roots "${_qt_pkg_prefix}")
            endif()
        endif()
    endif()

    find_program(_qt_qmake NAMES qmake6 qmake)
    if(_qt_qmake)
        execute_process(
            COMMAND "${_qt_qmake}" -query QT_INSTALL_PREFIX
            RESULT_VARIABLE _qmake_result
            OUTPUT_VARIABLE _qmake_prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_qmake_result EQUAL 0 AND _qmake_prefix)
            list(APPEND _candidate_roots "${_qmake_prefix}")
        endif()
    endif()

    find_program(_qt_paths NAMES qtpaths6 qtpaths)
    if(_qt_paths)
        execute_process(
            COMMAND "${_qt_paths}" --install-prefix
            RESULT_VARIABLE _qtpaths_result
            OUTPUT_VARIABLE _qtpaths_prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_qtpaths_result EQUAL 0 AND _qtpaths_prefix)
            list(APPEND _candidate_roots "${_qtpaths_prefix}")
        endif()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        file(GLOB _windows_qt_roots LIST_DIRECTORIES TRUE
            "C:/Qt/*/msvc*"
            "C:/Qt/*/clang_64"
        )
        list(APPEND _candidate_roots ${_windows_qt_roots})
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        list(APPEND _candidate_roots
            "/opt/homebrew/opt/qt"
            "/usr/local/opt/qt"
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND _candidate_roots
            "/opt/qt6"
            "/usr/local/opt/qt6"
            "/usr/lib/qt6"
        )
    endif()

    list(REMOVE_DUPLICATES _candidate_config_dirs)
    list(REMOVE_DUPLICATES _candidate_roots)

    foreach(_candidate IN LISTS _candidate_config_dirs _candidate_roots)
        gcs_try_qt_root_candidate("${_candidate}" _qt_root _qt_config_dir)
        if(_qt_root AND _qt_config_dir)
            gcs_validate_qt_candidate(
                "${_qt_root}"
                "${_qt_config_dir}"
                "${GCS_MINIMUM_QT_VERSION}"
                _candidate_ok
                _candidate_version
                _candidate_reason
            )
            if(_candidate_ok)
                set(${out_root} "${_qt_root}" PARENT_SCOPE)
                set(${out_config_dir} "${_qt_config_dir}" PARENT_SCOPE)
                set(${out_version} "${_candidate_version}" PARENT_SCOPE)
                set(${out_summary} "" PARENT_SCOPE)
                return()
            endif()

            if(_candidate_reason)
                list(APPEND _rejection_messages "${_qt_root}: ${_candidate_reason}")
            endif()
        endif()
    endforeach()

    if(_rejection_messages)
        list(JOIN _rejection_messages "\n  " _summary_text)
    else()
        set(_summary_text "")
    endif()

    set(${out_root} "" PARENT_SCOPE)
    set(${out_config_dir} "" PARENT_SCOPE)
    set(${out_version} "" PARENT_SCOPE)
    set(${out_summary} "${_summary_text}" PARENT_SCOPE)
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

if(GCS_QT_FORCE_DOWNLOAD AND (GCS_ALLOW_EXTERNAL_QT OR GCS_USE_SYSTEM_QT))
    message(FATAL_ERROR
        "GCS_QT_FORCE_DOWNLOAD refreshes only the managed aqt SDK. "
        "Do not combine it with GCS_ALLOW_EXTERNAL_QT or GCS_USE_SYSTEM_QT."
    )
endif()

if(GCS_ALLOW_EXTERNAL_QT AND GCS_USE_SYSTEM_QT)
    message(FATAL_ERROR
        "Choose one Qt source: explicit GCS_EXTERNAL_QT_ROOT/Qt6_DIR or GCS_USE_SYSTEM_QT."
    )
endif()

if(GCS_QT_VERSION VERSION_LESS "${GCS_MINIMUM_QT_VERSION}")
    message(FATAL_ERROR
        "GCS_QT_VERSION (${GCS_QT_VERSION}) is below the minimum supported Qt version "
        "${GCS_MINIMUM_QT_VERSION}."
    )
endif()

if(GCS_ALLOW_EXTERNAL_QT)
    if(GCS_EXTERNAL_QT_ROOT)
        gcs_get_qt_config_dir_from_root("${GCS_EXTERNAL_QT_ROOT}" _gcs_qt_cmake_dir)
        if(NOT _gcs_qt_cmake_dir)
            message(FATAL_ERROR
                "GCS_EXTERNAL_QT_ROOT is set to ${GCS_EXTERNAL_QT_ROOT}, "
                "but lib/cmake/Qt6/Qt6Config.cmake was not found there."
            )
        endif()
        get_filename_component(_gcs_qt_root_dir "${GCS_EXTERNAL_QT_ROOT}" ABSOLUTE)
    elseif(DEFINED Qt6_DIR AND EXISTS "${Qt6_DIR}/Qt6Config.cmake")
        get_filename_component(_gcs_qt_cmake_dir "${Qt6_DIR}" ABSOLUTE)
        gcs_get_qt_root_from_config_dir("${_gcs_qt_cmake_dir}" _gcs_qt_root_dir)
    else()
        message(FATAL_ERROR
            "GCS_ALLOW_EXTERNAL_QT=ON requires either GCS_EXTERNAL_QT_ROOT "
            "or a valid Qt6_DIR."
        )
    endif()

    gcs_validate_qt_candidate(
        "${_gcs_qt_root_dir}"
        "${_gcs_qt_cmake_dir}"
        "${GCS_MINIMUM_QT_VERSION}"
        _gcs_qt_ok
        _gcs_qt_version
        _gcs_qt_reason
    )
    if(NOT _gcs_qt_ok)
        message(FATAL_ERROR
            "The explicitly provided Qt SDK at ${_gcs_qt_root_dir} is not usable: "
            "${_gcs_qt_reason}"
        )
    endif()

    list(PREPEND CMAKE_PREFIX_PATH "${_gcs_qt_root_dir}")
    set(Qt6_DIR "${_gcs_qt_cmake_dir}" CACHE PATH "Path to Qt6Config.cmake" FORCE)
    gcs_pin_qt_package_dirs("${_gcs_qt_root_dir}")
    set(GCS_QT_ROOT_DIR "${_gcs_qt_root_dir}" CACHE INTERNAL "Resolved Qt installation root.")
    set(GCS_QT_RESOLVED_VERSION "${_gcs_qt_version}" CACHE INTERNAL "Resolved Qt version." FORCE)
    set(GCS_QT_SOURCE "external" CACHE INTERNAL "Resolved Qt SDK source.")
    message(STATUS "Using explicitly provided Qt ${GCS_QT_RESOLVED_VERSION} from ${_gcs_qt_root_dir}")
    return()
endif()

if(GCS_USE_SYSTEM_QT)
    gcs_find_system_qt(_gcs_qt_root_dir _gcs_qt_cmake_dir _gcs_qt_version _gcs_qt_summary)
    if(NOT _gcs_qt_root_dir OR NOT _gcs_qt_cmake_dir)
        if(_gcs_qt_summary)
            set(_gcs_qt_rejections_text "\nRejected candidates:\n  ${_gcs_qt_summary}")
        else()
            set(_gcs_qt_rejections_text "")
        endif()
        message(FATAL_ERROR
            "GCS_USE_SYSTEM_QT=ON, but no complete system Qt >= ${GCS_MINIMUM_QT_VERSION} "
            "SDK was found via Qt6_DIR, QTDIR/QT_DIR, CMAKE_PREFIX_PATH, qmake, qtpaths, "
            "or common install locations.${_gcs_qt_rejections_text}"
        )
    endif()

    list(PREPEND CMAKE_PREFIX_PATH "${_gcs_qt_root_dir}")
    set(Qt6_DIR "${_gcs_qt_cmake_dir}" CACHE PATH "Path to Qt6Config.cmake" FORCE)
    gcs_pin_qt_package_dirs("${_gcs_qt_root_dir}")
    set(GCS_QT_ROOT_DIR "${_gcs_qt_root_dir}" CACHE INTERNAL "Resolved Qt installation root.")
    set(GCS_QT_RESOLVED_VERSION "${_gcs_qt_version}" CACHE INTERNAL "Resolved Qt version." FORCE)
    set(GCS_QT_SOURCE "system" CACHE INTERNAL "Resolved Qt SDK source.")
    message(STATUS "Using system Qt ${GCS_QT_RESOLVED_VERSION} from ${_gcs_qt_root_dir}")
    return()
endif()

if(GCS_EXTERNAL_QT_ROOT)
    message(FATAL_ERROR
        "GCS_EXTERNAL_QT_ROOT was provided, but external Qt SDKs are disabled. "
        "Set GCS_ALLOW_EXTERNAL_QT=ON to use that path."
    )
endif()

gcs_resolve_qt_layout(_gcs_qt_host _gcs_qt_arch _gcs_qt_dir_name)
set(_gcs_qt_root_dir "${GCS_QT_INSTALL_ROOT}/${GCS_QT_VERSION}/${_gcs_qt_dir_name}")
set(_gcs_qt_cmake_dir "${_gcs_qt_root_dir}/lib/cmake/Qt6")

if(DEFINED Qt6_DIR AND Qt6_DIR AND NOT "${Qt6_DIR}" STREQUAL "${_gcs_qt_cmake_dir}")
    message(STATUS
        "Ignoring Qt6_DIR=${Qt6_DIR} because GCS_ALLOW_EXTERNAL_QT=OFF; "
        "using the aqt-managed Qt at ${_gcs_qt_root_dir}."
    )
endif()

if(GCS_QT_FORCE_DOWNLOAD AND EXISTS "${_gcs_qt_root_dir}")
    gcs_qt_path_must_be_under_root("${_gcs_qt_root_dir}" "${GCS_QT_INSTALL_ROOT}" "Managed Qt SDK root")
    message(STATUS "Refreshing managed Qt ${GCS_QT_VERSION} at ${_gcs_qt_root_dir}")
    file(REMOVE_RECURSE "${_gcs_qt_root_dir}")
endif()

if(NOT GCS_QT_FORCE_DOWNLOAD AND GCS_PREFER_SYSTEM_QT)
    gcs_find_system_qt(_gcs_auto_qt_root _gcs_auto_qt_cmake_dir _gcs_auto_qt_version _gcs_auto_qt_summary)
    if(_gcs_auto_qt_root AND "${_gcs_auto_qt_root}" STREQUAL "${_gcs_qt_root_dir}")
        set(_gcs_auto_qt_root "")
        set(_gcs_auto_qt_cmake_dir "")
        set(_gcs_auto_qt_version "")
    endif()
    if(_gcs_auto_qt_root AND _gcs_auto_qt_cmake_dir)
        list(PREPEND CMAKE_PREFIX_PATH "${_gcs_auto_qt_root}")
        set(Qt6_DIR "${_gcs_auto_qt_cmake_dir}" CACHE PATH "Path to Qt6Config.cmake" FORCE)
        gcs_pin_qt_package_dirs("${_gcs_auto_qt_root}")
        set(GCS_QT_ROOT_DIR "${_gcs_auto_qt_root}" CACHE INTERNAL "Resolved Qt installation root.")
        set(GCS_QT_RESOLVED_VERSION "${_gcs_auto_qt_version}" CACHE INTERNAL "Resolved Qt version." FORCE)
        set(GCS_QT_SOURCE "system-auto" CACHE INTERNAL "Resolved Qt SDK source.")
        message(STATUS "Using auto-discovered system Qt ${GCS_QT_RESOLVED_VERSION} from ${_gcs_auto_qt_root}")
        return()
    endif()

    if(_gcs_auto_qt_summary)
        message(STATUS
            "No usable system Qt >= ${GCS_MINIMUM_QT_VERSION} was auto-discovered; "
            "falling back to managed Qt ${GCS_QT_VERSION}.\n"
            "Rejected candidates:\n  ${_gcs_auto_qt_summary}"
        )
    else()
        message(STATUS
            "No system Qt was auto-discovered by standard names or paths; "
            "falling back to managed Qt ${GCS_QT_VERSION}."
        )
    endif()
elseif(NOT GCS_QT_FORCE_DOWNLOAD)
    message(STATUS
        "System Qt auto-discovery is disabled; using the aqt-managed Qt "
        "${GCS_QT_VERSION} at ${_gcs_qt_root_dir}."
    )
endif()

if(NOT EXISTS "${_gcs_qt_cmake_dir}/Qt6Config.cmake")
    if(NOT GCS_FETCH_QT_WITH_AQT)
        message(FATAL_ERROR
            "The managed Qt SDK was expected at ${_gcs_qt_cmake_dir}, but it is missing "
            "and automatic Qt bootstrap is disabled (GCS_FETCH_QT_WITH_AQT=OFF)."
        )
    endif()

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

gcs_require_qt_version("${_gcs_qt_cmake_dir}" "${GCS_QT_VERSION}")
gcs_require_qt_module_configs("${_gcs_qt_root_dir}")
list(PREPEND CMAKE_PREFIX_PATH "${_gcs_qt_root_dir}")
set(Qt6_DIR "${_gcs_qt_cmake_dir}" CACHE PATH "Path to Qt6Config.cmake" FORCE)
gcs_pin_qt_package_dirs("${_gcs_qt_root_dir}")
set(GCS_QT_ROOT_DIR "${_gcs_qt_root_dir}" CACHE INTERNAL "Resolved Qt installation root.")
set(GCS_QT_SOURCE "aqt" CACHE INTERNAL "Resolved Qt SDK source.")

message(STATUS "Using aqt-managed Qt ${GCS_QT_RESOLVED_VERSION} from ${_gcs_qt_root_dir}")
