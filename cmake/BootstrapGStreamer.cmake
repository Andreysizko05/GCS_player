include_guard(GLOBAL)

option(GCS_FETCH_GSTREAMER "Automatically download the managed GStreamer SDK when it is missing." ON)
option(GCS_GSTREAMER_FORCE_DOWNLOAD "Refresh the managed GStreamer SDK; cannot be combined with external/system GStreamer." OFF)
option(GCS_ALLOW_EXTERNAL_GSTREAMER "Allow an explicitly provided GStreamer SDK instead of the managed SDK." OFF)
option(GCS_USE_SYSTEM_GSTREAMER "Allow GStreamer discovery from common system environment variables and PATH." OFF)
option(GCS_GSTREAMER_REQUIRE_CHECKSUM "Fail if an auto-downloaded GStreamer package has no pinned checksum." OFF)

set(GCS_GSTREAMER_VERSION "1.28.1" CACHE STRING
    "GStreamer SDK version expected for the selected Qt version."
)
set(GCS_GSTREAMER_INSTALL_ROOT "${PROJECT_SOURCE_DIR}/External/GStreamer" CACHE PATH
    "Root directory that stores managed GStreamer SDKs."
)
set(GCS_EXTERNAL_GSTREAMER_ROOT "" CACHE PATH
    "Explicit GStreamer SDK/runtime root used only when GCS_ALLOW_EXTERNAL_GSTREAMER=ON."
)
set(GCS_GSTREAMER_ROOT "" CACHE PATH "Resolved GStreamer SDK/runtime root.")
set(GCS_REQUIRED_GSTREAMER_PLUGINS
    "coreelements;app;udp;rtpmanager;rtp;mpegtsdemux;videoparsersbad;playback;videoconvertscale;libav"
    CACHE STRING "GStreamer plugin names required by the video receiver."
)
if(NOT "mpegtsdemux" IN_LIST GCS_REQUIRED_GSTREAMER_PLUGINS)
    list(APPEND GCS_REQUIRED_GSTREAMER_PLUGINS "mpegtsdemux")
    set(GCS_REQUIRED_GSTREAMER_PLUGINS
        "${GCS_REQUIRED_GSTREAMER_PLUGINS}"
        CACHE STRING "GStreamer plugin names required by the video receiver." FORCE
    )
endif()

set(_GCS_GSTREAMER_SHA256_WINDOWS_MSVC_X86_64_1_28_1
    "2ec50356d2d0937a9ead0f99d322f81d8413b9514c9d58ed41ca58fbcf25bfde"
)
set(_GCS_GSTREAMER_SHA256_WINDOWS_MSVC_ARM64_1_28_1
    "0a1938b7a8568ee5695c4c1755743cacc4a1643538cacdfc5be3c82426c0e193"
)
set(_GCS_GSTREAMER_SHA256_MACOS_1_28_1
    "02803f73435daabe8fb12b79c38c6775d0efb83af001474558ba25c4f874d305"
)
set(_GCS_GSTREAMER_SHA256_MACOS_DEVEL_1_28_1
    "df167b41559afbcd743276c6b068cba2ada8f5b69eb68095415a7a5a7515e52c"
)

function(gcs_gst_to_key input output_var)
    string(TOUPPER "${input}" _key)
    string(REPLACE "." "_" _key "${_key}")
    string(REPLACE "-" "_" _key "${_key}")
    set(${output_var} "${_key}" PARENT_SCOPE)
endfunction()

function(gcs_gst_expected_version_for_qt qt_version output_var)
    if(NOT qt_version)
        message(FATAL_ERROR "GCS_QT_VERSION must be set before resolving GStreamer compatibility.")
    endif()

    if(qt_version VERSION_GREATER_EQUAL "6.10.0" AND qt_version VERSION_LESS "6.11.0")
        set(${output_var} "1.28.1" PARENT_SCOPE)
    else()
        message(FATAL_ERROR
            "No verified GStreamer compatibility mapping is defined for Qt ${qt_version}. "
            "Add the Qt/GStreamer pair to cmake/BootstrapGStreamer.cmake before configuring the project."
        )
    endif()
endfunction()

function(gcs_gst_resolve_version_for_qt)
    gcs_gst_expected_version_for_qt("${GCS_QT_VERSION}" _expected_gst_version)

    if(GCS_GSTREAMER_VERSION AND NOT "${GCS_GSTREAMER_VERSION}" STREQUAL "${_expected_gst_version}")
        message(FATAL_ERROR
            "GStreamer version mismatch for Qt ${GCS_QT_VERSION}.\n"
            "Expected GStreamer: ${_expected_gst_version}\n"
            "Configured GStreamer: ${GCS_GSTREAMER_VERSION}"
        )
    endif()

    set(GCS_GSTREAMER_VERSION "${_expected_gst_version}" CACHE STRING
        "GStreamer SDK version expected for the selected Qt version." FORCE
    )
    set(GCS_QT_COMPATIBLE_GSTREAMER_VERSION "${_expected_gst_version}" CACHE INTERNAL
        "GStreamer version verified for the selected Qt version."
    )
endfunction()

function(gcs_gst_get_checksum platform version output_var)
    gcs_gst_to_key("${platform}" _platform_key)
    gcs_gst_to_key("${version}" _version_key)
    set(_var "_GCS_GSTREAMER_SHA256_${_platform_key}_${_version_key}")

    if(DEFINED ${_var})
        set(${output_var} "SHA256=${${_var}}" PARENT_SCOPE)
    elseif(GCS_GSTREAMER_REQUIRE_CHECKSUM)
        message(FATAL_ERROR
            "No pinned checksum is available for GStreamer ${version} ${platform}."
        )
    else()
        message(WARNING
            "No pinned checksum is available for GStreamer ${version} ${platform}; "
            "continuing without package verification."
        )
        set(${output_var} "" PARENT_SCOPE)
    endif()
endfunction()

function(gcs_gst_download url destination_file expected_hash)
    if(EXISTS "${destination_file}")
        if(expected_hash)
            string(REGEX MATCH "^([^=]+)=(.+)$" _hash_match "${expected_hash}")
            if(NOT _hash_match)
                message(FATAL_ERROR "Invalid hash specification: ${expected_hash}")
            endif()
            file(${CMAKE_MATCH_1} "${destination_file}" _actual_hash)
            if(_actual_hash STREQUAL "${CMAKE_MATCH_2}")
                return()
            endif()

            message(STATUS "Cached GStreamer package failed checksum verification; re-downloading.")
            file(REMOVE "${destination_file}")
        else()
            message(STATUS "Using cached GStreamer package at ${destination_file}")
            return()
        endif()
    endif()

    get_filename_component(_destination_dir "${destination_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${_destination_dir}")
    set(_temporary_file "${destination_file}.tmp")
    file(REMOVE "${_temporary_file}")

    message(STATUS "Downloading GStreamer package: ${url}")
    file(DOWNLOAD "${url}" "${_temporary_file}"
        STATUS _download_status
        SHOW_PROGRESS
        TIMEOUT 900
        INACTIVITY_TIMEOUT 120
        TLS_VERIFY ON
    )
    list(GET _download_status 0 _download_code)
    if(NOT _download_code EQUAL 0)
        list(GET _download_status 1 _download_message)
        file(REMOVE "${_temporary_file}")
        message(FATAL_ERROR "Failed to download GStreamer package: ${_download_message}")
    endif()

    if(expected_hash)
        string(REGEX MATCH "^([^=]+)=(.+)$" _hash_match "${expected_hash}")
        file(${CMAKE_MATCH_1} "${_temporary_file}" _actual_hash)
        if(NOT _actual_hash STREQUAL "${CMAKE_MATCH_2}")
            file(REMOVE "${_temporary_file}")
            message(FATAL_ERROR
                "Checksum mismatch for ${destination_file}.\n"
                "Expected: ${CMAKE_MATCH_2}\n"
                "Actual:   ${_actual_hash}"
            )
        endif()
    endif()

    file(RENAME "${_temporary_file}" "${destination_file}")
endfunction()

function(gcs_gst_get_windows_arch output_arch output_checksum_platform)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(${output_arch} "arm64" PARENT_SCOPE)
        set(${output_checksum_platform} "WINDOWS_MSVC_ARM64" PARENT_SCOPE)
    else()
        set(${output_arch} "x86_64" PARENT_SCOPE)
        set(${output_checksum_platform} "WINDOWS_MSVC_X86_64" PARENT_SCOPE)
    endif()
endfunction()

function(gcs_gst_get_managed_root output_var)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        gcs_gst_get_windows_arch(_gst_arch _checksum_platform)
        set(_root "${GCS_GSTREAMER_INSTALL_ROOT}/windows-msvc-${_gst_arch}-${GCS_GSTREAMER_VERSION}/sdk")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(_root "${GCS_GSTREAMER_INSTALL_ROOT}/macos-${GCS_GSTREAMER_VERSION}/root")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _linux_arch)
        if(NOT _linux_arch)
            set(_linux_arch "unknown")
        endif()
        set(_root "${GCS_GSTREAMER_INSTALL_ROOT}/linux-${_linux_arch}-${GCS_GSTREAMER_VERSION}")
    else()
        set(_root "")
    endif()

    set(${output_var} "${_root}" PARENT_SCOPE)
endfunction()

function(gcs_gst_read_pc_version root_dir output_var)
    set(_pc_version "")
    gcs_gst_pkgconfig_dirs("${root_dir}" _pkgconfig_dirs)
    foreach(_pkgconfig_dir IN LISTS _pkgconfig_dirs)
        set(_pc_file "${_pkgconfig_dir}/gstreamer-1.0.pc")
        if(EXISTS "${_pc_file}")
            file(STRINGS "${_pc_file}" _version_lines REGEX "^Version:")
            foreach(_line IN LISTS _version_lines)
                if(_line MATCHES "^Version:[ \t]*([0-9]+\\.[0-9]+\\.[0-9]+)")
                    set(_pc_version "${CMAKE_MATCH_1}")
                    break()
                endif()
            endforeach()
        endif()

        if(_pc_version)
            break()
        endif()
    endforeach()

    set(${output_var} "${_pc_version}" PARENT_SCOPE)
endfunction()

function(gcs_gst_pkgconfig_dirs root_dir output_var)
    set(_candidate_dirs
        "${root_dir}/lib/pkgconfig"
        "${root_dir}/lib/gstreamer-1.0/pkgconfig"
        "${root_dir}/lib64/pkgconfig"
        "${root_dir}/share/pkgconfig"
    )

    if(CMAKE_LIBRARY_ARCHITECTURE)
        list(APPEND _candidate_dirs "${root_dir}/lib/${CMAKE_LIBRARY_ARCHITECTURE}/pkgconfig")
    endif()

    list(APPEND _candidate_dirs
        "${root_dir}/lib/x86_64-linux-gnu/pkgconfig"
        "${root_dir}/lib/aarch64-linux-gnu/pkgconfig"
        "${root_dir}/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu/pkgconfig"
    )

    set(_existing_dirs)
    foreach(_dir IN LISTS _candidate_dirs)
        if(EXISTS "${_dir}")
            list(APPEND _existing_dirs "${_dir}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _existing_dirs)

    set(${output_var} "${_existing_dirs}" PARENT_SCOPE)
endfunction()

function(gcs_gst_plugin_dirs root_dir output_var)
    set(_candidate_dirs
        "${root_dir}/lib/gstreamer-1.0"
        "${root_dir}/lib64/gstreamer-1.0"
    )

    if(CMAKE_LIBRARY_ARCHITECTURE)
        list(APPEND _candidate_dirs "${root_dir}/lib/${CMAKE_LIBRARY_ARCHITECTURE}/gstreamer-1.0")
    endif()

    list(APPEND _candidate_dirs
        "${root_dir}/lib/x86_64-linux-gnu/gstreamer-1.0"
        "${root_dir}/lib/aarch64-linux-gnu/gstreamer-1.0"
        "${root_dir}/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu/gstreamer-1.0"
    )

    set(_existing_dirs)
    foreach(_dir IN LISTS _candidate_dirs)
        if(EXISTS "${_dir}")
            list(APPEND _existing_dirs "${_dir}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _existing_dirs)

    set(${output_var} "${_existing_dirs}" PARENT_SCOPE)
endfunction()

function(gcs_gst_join_paths output_var)
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(_separator ";")
    else()
        set(_separator ":")
    endif()

    set(_joined "")
    foreach(_path IN LISTS ARGN)
        if(NOT _path)
            continue()
        endif()

        if(_joined)
            string(APPEND _joined "${_separator}${_path}")
        else()
            set(_joined "${_path}")
        endif()
    endforeach()

    set(${output_var} "${_joined}" PARENT_SCOPE)
endfunction()

function(gcs_gst_root_complete root_dir output_var)
    set(_is_complete TRUE)

    if(NOT root_dir OR NOT EXISTS "${root_dir}")
        set(_is_complete FALSE)
    endif()

    if(_is_complete AND NOT EXISTS "${root_dir}/include/gstreamer-1.0")
        set(_is_complete FALSE)
    endif()

    if(_is_complete)
        gcs_gst_pkgconfig_dirs("${root_dir}" _pkgconfig_dirs)
        set(_has_gstreamer_pc FALSE)
        foreach(_pkgconfig_dir IN LISTS _pkgconfig_dirs)
            if(EXISTS "${_pkgconfig_dir}/gstreamer-1.0.pc")
                set(_has_gstreamer_pc TRUE)
                break()
            endif()
        endforeach()
        if(NOT _has_gstreamer_pc)
            set(_is_complete FALSE)
        endif()
    endif()

    if(_is_complete)
        gcs_gst_plugin_dirs("${root_dir}" _plugin_dirs)
        if(NOT _plugin_dirs)
            set(_is_complete FALSE)
        endif()
    endif()

    if(_is_complete AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if(NOT EXISTS "${root_dir}/bin/pkg-config.exe")
            set(_is_complete FALSE)
        endif()
    endif()

    if(_is_complete)
        if(WIN32)
            set(_gst_inspect_name "gst-inspect-1.0.exe")
        else()
            set(_gst_inspect_name "gst-inspect-1.0")
        endif()

        if(NOT EXISTS "${root_dir}/bin/${_gst_inspect_name}")
            set(_is_complete FALSE)
        endif()
    endif()

    set(${output_var} "${_is_complete}" PARENT_SCOPE)
endfunction()

function(gcs_gst_add_env_candidate candidate output_var)
    set(_candidates ${${output_var}})
    if(candidate)
        list(APPEND _candidates "${candidate}")
        list(REMOVE_DUPLICATES _candidates)
    endif()
    set(${output_var} "${_candidates}" PARENT_SCOPE)
endfunction()

function(gcs_gst_collect_system_candidates output_var)
    set(_candidates)

    foreach(_env_name IN ITEMS
        GSTREAMER_ROOT
        GSTREAMER_1_0_ROOT
        GSTREAMER_SDK_ROOT
        GSTREAMER_1_0_ROOT_X86_64
        GSTREAMER_1_0_ROOT_MSVC_X86_64
        GSTREAMER_1_0_ROOT_ARM64
        GSTREAMER_1_0_ROOT_MSVC_ARM64
    )
        if(DEFINED ENV{${_env_name}} AND NOT "$ENV{${_env_name}}" STREQUAL "")
            gcs_gst_add_env_candidate("$ENV{${_env_name}}" _candidates)
        endif()
    endforeach()

    find_program(_system_gst_inspect NAMES gst-inspect-1.0 gst-inspect-1.0.exe)
    if(_system_gst_inspect)
        get_filename_component(_system_gst_bin_dir "${_system_gst_inspect}" DIRECTORY)
        get_filename_component(_system_gst_root "${_system_gst_bin_dir}" DIRECTORY)
        gcs_gst_add_env_candidate("${_system_gst_root}" _candidates)
    endif()

    find_program(_system_pkg_config NAMES pkg-config pkgconf pkg-config.exe pkgconf.exe)
    if(_system_pkg_config)
        execute_process(
            COMMAND "${_system_pkg_config}" --variable=prefix gstreamer-1.0
            RESULT_VARIABLE _pkg_prefix_result
            OUTPUT_VARIABLE _pkg_prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_pkg_prefix_result EQUAL 0 AND _pkg_prefix)
            gcs_gst_add_env_candidate("${_pkg_prefix}" _candidates)
        endif()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            gcs_gst_add_env_candidate("C:/Program Files/gstreamer/1.0/msvc_arm64" _candidates)
            gcs_gst_add_env_candidate("C:/gstreamer/1.0/msvc_arm64" _candidates)
        else()
            gcs_gst_add_env_candidate("C:/Program Files/gstreamer/1.0/msvc_x86_64" _candidates)
            gcs_gst_add_env_candidate("C:/gstreamer/1.0/msvc_x86_64" _candidates)
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        gcs_gst_add_env_candidate("/Library/Frameworks/GStreamer.framework/Versions/1.0" _candidates)
        gcs_gst_add_env_candidate("/opt/homebrew/opt/gstreamer" _candidates)
        gcs_gst_add_env_candidate("/usr/local/opt/gstreamer" _candidates)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        gcs_gst_add_env_candidate("/usr/local" _candidates)
        gcs_gst_add_env_candidate("/usr" _candidates)
    endif()

    set(${output_var} "${_candidates}" PARENT_SCOPE)
endfunction()

function(gcs_gst_find_system_root output_var)
    gcs_gst_collect_system_candidates(_candidates)
    set(_wrong_version_messages)
    set(_incomplete_messages)

    foreach(_candidate IN LISTS _candidates)
        if(NOT _candidate OR NOT EXISTS "${_candidate}")
            continue()
        endif()

        get_filename_component(_candidate_root "${_candidate}" ABSOLUTE)
        gcs_gst_root_complete("${_candidate_root}" _candidate_complete)
        if(NOT _candidate_complete)
            list(APPEND _incomplete_messages "${_candidate_root}")
            continue()
        endif()

        gcs_gst_read_pc_version("${_candidate_root}" _candidate_version)
        if(NOT "${_candidate_version}" STREQUAL "${GCS_GSTREAMER_VERSION}")
            list(APPEND _wrong_version_messages "${_candidate_root}: ${_candidate_version}")
            continue()
        endif()

        set(${output_var} "${_candidate_root}" PARENT_SCOPE)
        return()
    endforeach()

    message(FATAL_ERROR
        "GCS_USE_SYSTEM_GSTREAMER=ON, but no complete system GStreamer "
        "${GCS_GSTREAMER_VERSION} SDK was found via GSTREAMER_* env vars, PATH, "
        "pkg-config, or standard OS install locations.\n"
        "Incomplete candidates: ${_incomplete_messages}\n"
        "Wrong-version candidates: ${_wrong_version_messages}"
    )
endfunction()

function(gcs_gst_find_windows_sdk_root extracted_dir output_var)
    if(EXISTS "${extracted_dir}/bin/pkg-config.exe")
        set(${output_var} "${extracted_dir}" PARENT_SCOPE)
        return()
    endif()

    file(GLOB_RECURSE _pkg_config_files "${extracted_dir}/pkg-config.exe")
    if(_pkg_config_files)
        list(GET _pkg_config_files 0 _first_pkg_config)
        get_filename_component(_bin_dir "${_first_pkg_config}" DIRECTORY)
        get_filename_component(_found_root "${_bin_dir}" DIRECTORY)
        set(${output_var} "${_found_root}" PARENT_SCOPE)
    else()
        set(${output_var} "" PARENT_SCOPE)
    endif()
endfunction()

function(gcs_gst_download_windows_sdk output_root_var)
    gcs_gst_get_windows_arch(_gst_arch _checksum_platform)

    if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR
            "Automatic GStreamer download on Windows requires a 64-bit MSVC generator."
        )
    endif()

    if(GCS_GSTREAMER_VERSION VERSION_LESS "1.28.0")
        message(FATAL_ERROR
            "Automatic Windows GStreamer download expects the unified 1.28+ installer. "
            "Set GCS_GSTREAMER_VERSION to 1.28.1 or provide GCS_EXTERNAL_GSTREAMER_ROOT manually."
        )
    endif()

    set(_cache_dir
        "${GCS_GSTREAMER_INSTALL_ROOT}/windows-msvc-${_gst_arch}-${GCS_GSTREAMER_VERSION}"
    )
    set(_sdk_dir "${_cache_dir}/sdk")
    set(_installer "${_cache_dir}/gstreamer-msvc-${_gst_arch}-${GCS_GSTREAMER_VERSION}.exe")
    set(_url
        "https://gstreamer.freedesktop.org/data/pkg/windows/${GCS_GSTREAMER_VERSION}/msvc/gstreamer-1.0-msvc-${_gst_arch}-${GCS_GSTREAMER_VERSION}.exe"
    )

    gcs_gst_get_checksum("${_checksum_platform}" "${GCS_GSTREAMER_VERSION}" _expected_hash)
    gcs_gst_download("${_url}" "${_installer}" "${_expected_hash}")

    gcs_gst_find_windows_sdk_root("${_sdk_dir}" _sdk_root)
    if(NOT _sdk_root OR GCS_GSTREAMER_FORCE_DOWNLOAD)
        file(REMOVE_RECURSE "${_sdk_dir}")
        file(MAKE_DIRECTORY "${_sdk_dir}")

        file(TO_NATIVE_PATH "${_installer}" _installer_native)
        file(TO_NATIVE_PATH "${_sdk_dir}" _sdk_dir_native)
        set(_installer_log "${_cache_dir}/installer-${_gst_arch}.log")
        file(TO_NATIVE_PATH "${_installer_log}" _installer_log_native)

        message(STATUS "Installing GStreamer ${GCS_GSTREAMER_VERSION} ${_gst_arch} SDK silently.")
        execute_process(
            COMMAND "${_installer_native}"
                /VERYSILENT /SUPPRESSMSGBOXES /SP- /NORESTART
                "/LOG=${_installer_log_native}"
                "/DIR=${_sdk_dir_native}"
            RESULT_VARIABLE _installer_result
            ERROR_VARIABLE _installer_stderr
            TIMEOUT 900
        )
        gcs_gst_find_windows_sdk_root("${_sdk_dir}" _sdk_root)
        gcs_gst_root_complete("${_sdk_root}" _sdk_complete_after_installer)
        if(NOT _installer_result EQUAL 0 AND NOT _sdk_complete_after_installer)
            file(REMOVE_RECURSE "${_sdk_dir}")
            message(FATAL_ERROR
                "GStreamer installer failed with exit code ${_installer_result}.\n"
                "stderr: ${_installer_stderr}\n"
                "Installer log: ${_installer_log}"
            )
        elseif(NOT _installer_result EQUAL 0)
            message(WARNING
                "GStreamer installer returned exit code ${_installer_result}, but the SDK "
                "contents verified successfully. Continuing.\n"
                "Installer log: ${_installer_log}"
            )
        endif()
    endif()

    gcs_gst_root_complete("${_sdk_root}" _sdk_complete)
    if(NOT _sdk_root OR NOT _sdk_complete)
        file(REMOVE_RECURSE "${_sdk_dir}")
        message(FATAL_ERROR
            "Downloaded GStreamer SDK is incomplete. Delete ${_cache_dir} and re-run CMake."
        )
    endif()

    set(${output_root_var} "${_sdk_root}" PARENT_SCOPE)
endfunction()

function(gcs_gst_validate_expanded_pkg expanded_dir label)
    file(GLOB _payloads "${expanded_dir}/*.pkg/Payload")
    if(NOT _payloads)
        file(REMOVE_RECURSE "${expanded_dir}")
        message(FATAL_ERROR
            "pkgutil expanded GStreamer ${label} package but no payloads were found in ${expanded_dir}."
        )
    endif()
endfunction()

function(gcs_gst_download_macos_sdk output_root_var)
    set(_cache_dir "${GCS_GSTREAMER_INSTALL_ROOT}/macos-${GCS_GSTREAMER_VERSION}")
    set(_runtime_pkg "${_cache_dir}/gstreamer.pkg")
    set(_devel_pkg "${_cache_dir}/gstreamer-devel.pkg")
    set(_expanded_runtime "${_cache_dir}/expanded-runtime")
    set(_expanded_devel "${_cache_dir}/expanded-devel")
    set(_sdk_root "${_cache_dir}/root")

    set(_runtime_url
        "https://gstreamer.freedesktop.org/data/pkg/macos/${GCS_GSTREAMER_VERSION}/gstreamer-1.0-${GCS_GSTREAMER_VERSION}-universal.pkg"
    )
    set(_devel_url
        "https://gstreamer.freedesktop.org/data/pkg/macos/${GCS_GSTREAMER_VERSION}/gstreamer-1.0-devel-${GCS_GSTREAMER_VERSION}-universal.pkg"
    )

    gcs_gst_get_checksum("MACOS" "${GCS_GSTREAMER_VERSION}" _runtime_hash)
    gcs_gst_get_checksum("MACOS_DEVEL" "${GCS_GSTREAMER_VERSION}" _devel_hash)
    gcs_gst_download("${_runtime_url}" "${_runtime_pkg}" "${_runtime_hash}")
    gcs_gst_download("${_devel_url}" "${_devel_pkg}" "${_devel_hash}")

    gcs_gst_root_complete("${_sdk_root}" _sdk_complete)
    if(_sdk_complete AND EXISTS "${_sdk_root}/.merge_complete" AND NOT GCS_GSTREAMER_FORCE_DOWNLOAD)
        set(${output_root_var} "${_sdk_root}" PARENT_SCOPE)
        return()
    endif()

    file(REMOVE_RECURSE "${_sdk_root}" "${_expanded_runtime}" "${_expanded_devel}")

    message(STATUS "Expanding GStreamer macOS runtime package.")
    execute_process(
        COMMAND pkgutil --expand-full "${_runtime_pkg}" "${_expanded_runtime}"
        RESULT_VARIABLE _pkgutil_result
    )
    if(NOT _pkgutil_result EQUAL 0)
        message(FATAL_ERROR "pkgutil failed to expand GStreamer runtime package.")
    endif()
    gcs_gst_validate_expanded_pkg("${_expanded_runtime}" "runtime")

    message(STATUS "Expanding GStreamer macOS development package.")
    execute_process(
        COMMAND pkgutil --expand-full "${_devel_pkg}" "${_expanded_devel}"
        RESULT_VARIABLE _pkgutil_result
    )
    if(NOT _pkgutil_result EQUAL 0)
        message(FATAL_ERROR "pkgutil failed to expand GStreamer development package.")
    endif()
    gcs_gst_validate_expanded_pkg("${_expanded_devel}" "development")

    file(MAKE_DIRECTORY "${_sdk_root}")
    foreach(_expanded_dir IN ITEMS "${_expanded_runtime}" "${_expanded_devel}")
        file(GLOB _sub_pkg_dirs "${_expanded_dir}/*.pkg")
        foreach(_pkg_dir IN LISTS _sub_pkg_dirs)
            if(EXISTS "${_pkg_dir}/Payload")
                file(GLOB _payload_entries "${_pkg_dir}/Payload/*")
                foreach(_entry IN LISTS _payload_entries)
                    get_filename_component(_entry_name "${_entry}" NAME)
                    if(_entry_name STREQUAL "Headers")
                        continue()
                    endif()
                    file(COPY "${_entry}" DESTINATION "${_sdk_root}")
                endforeach()
            endif()
        endforeach()
    endforeach()
    file(TOUCH "${_sdk_root}/.merge_complete")

    gcs_gst_root_complete("${_sdk_root}" _sdk_complete)
    if(NOT _sdk_complete)
        file(REMOVE_RECURSE "${_sdk_root}")
        message(FATAL_ERROR "Downloaded macOS GStreamer SDK is incomplete.")
    endif()

    set(${output_root_var} "${_sdk_root}" PARENT_SCOPE)
endfunction()

function(gcs_gst_apply_root root_dir)
    if(NOT root_dir OR NOT EXISTS "${root_dir}")
        message(FATAL_ERROR "GStreamer root does not exist: ${root_dir}")
    endif()

    get_filename_component(_root_dir "${root_dir}" ABSOLUTE)
    gcs_gst_root_complete("${_root_dir}" _root_complete)
    if(NOT _root_complete)
        message(FATAL_ERROR
            "GStreamer root is incomplete: ${_root_dir}\n"
            "Expected include/gstreamer-1.0, a gstreamer-1.0.pc file, and plugin directories."
        )
    endif()

    set(GCS_GSTREAMER_ROOT "${_root_dir}" CACHE PATH "Resolved GStreamer SDK/runtime root." FORCE)

    gcs_gst_pkgconfig_dirs("${_root_dir}" _pkgconfig_dirs)
    gcs_gst_join_paths(_pkg_config_libdir ${_pkgconfig_dirs})
    set(ENV{PKG_CONFIG_LIBDIR} "${_pkg_config_libdir}")
    set(ENV{PKG_CONFIG_PATH} "")
    set(ENV{PKG_CONFIG_DONT_DEFINE_PREFIX} "1")

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(_pkg_config_exe "${_root_dir}/bin/pkg-config.exe")
        if(EXISTS "${_pkg_config_exe}")
            set(ENV{PKG_CONFIG} "${_pkg_config_exe}")
            set(PKG_CONFIG_EXECUTABLE "${_pkg_config_exe}" CACHE FILEPATH "pkg-config executable" FORCE)
            set(_pkg_config_args
                "--dont-define-prefix"
                "--define-variable=prefix=${_root_dir}"
                "--define-variable=libdir=${_root_dir}/lib"
                "--define-variable=includedir=${_root_dir}/include"
            )
            set(PKG_CONFIG_ARGN "${_pkg_config_args}" CACHE STRING "Extra arguments for pkg-config" FORCE)
        endif()
    elseif(EXISTS "${_root_dir}/bin/pkg-config")
        set(ENV{PKG_CONFIG} "${_root_dir}/bin/pkg-config")
        set(PKG_CONFIG_EXECUTABLE "${_root_dir}/bin/pkg-config" CACHE FILEPATH "pkg-config executable" FORCE)
    endif()
endfunction()

function(gcs_gst_path_must_be_under_root path root_dir label)
    if(NOT path OR NOT EXISTS "${path}")
        return()
    endif()

    file(REAL_PATH "${path}" _path_real)
    file(REAL_PATH "${root_dir}" _root_real)
    file(TO_CMAKE_PATH "${_path_real}" _path_real)
    file(TO_CMAKE_PATH "${_root_real}" _root_real)

    string(APPEND _path_real "/")
    string(APPEND _root_real "/")
    string(FIND "${_path_real}" "${_root_real}" _root_pos)
    if(NOT _root_pos EQUAL 0)
        message(FATAL_ERROR
            "${label} resolves outside the selected GStreamer root.\n"
            "GStreamer root: ${root_dir}\n"
            "${label}: ${path}"
        )
    endif()
endfunction()

function(gcs_verify_gstreamer_pkg_config_paths)
    if(NOT GCS_GSTREAMER_ROOT)
        message(FATAL_ERROR "GCS_GSTREAMER_ROOT is empty; refusing to query system GStreamer.")
    endif()

    pkg_get_variable(_gst_prefix gstreamer-1.0 prefix)
    pkg_get_variable(_gst_pluginsdir gstreamer-1.0 pluginsdir)
    pkg_get_variable(_gst_pluginscannerdir gstreamer-1.0 pluginscannerdir)
    pkg_get_variable(_gst_toolsdir gstreamer-1.0 toolsdir)
    pkg_get_variable(_gst_giomoduledir gio-2.0 giomoduledir)

    if(_gst_prefix)
        gcs_gst_path_must_be_under_root("${_gst_prefix}" "${GCS_GSTREAMER_ROOT}" "GStreamer prefix")
    endif()
    if(_gst_pluginsdir)
        gcs_gst_path_must_be_under_root("${_gst_pluginsdir}" "${GCS_GSTREAMER_ROOT}" "GStreamer pluginsdir")
    endif()
    if(_gst_pluginscannerdir)
        gcs_gst_path_must_be_under_root("${_gst_pluginscannerdir}" "${GCS_GSTREAMER_ROOT}" "GStreamer pluginscannerdir")
    endif()
    if(_gst_toolsdir)
        gcs_gst_path_must_be_under_root("${_gst_toolsdir}" "${GCS_GSTREAMER_ROOT}" "GStreamer toolsdir")
    endif()
    if(_gst_giomoduledir)
        gcs_gst_path_must_be_under_root("${_gst_giomoduledir}" "${GCS_GSTREAMER_ROOT}" "GIO module dir")
    endif()
endfunction()

function(gcs_verify_gstreamer_plugins)
    if(NOT GCS_GSTREAMER_ROOT)
        message(FATAL_ERROR "Cannot verify GStreamer plugins without GCS_GSTREAMER_ROOT.")
    endif()

    pkg_get_variable(_gst_pluginsdir gstreamer-1.0 pluginsdir)
    pkg_get_variable(_gst_pluginscannerdir gstreamer-1.0 pluginscannerdir)

    find_program(_gst_inspect
        NAMES gst-inspect-1.0 gst-inspect-1.0.exe
        HINTS "${GCS_GSTREAMER_ROOT}/bin"
        NO_DEFAULT_PATH
    )
    if(NOT _gst_inspect)
        message(FATAL_ERROR
            "gst-inspect-1.0 was not found under ${GCS_GSTREAMER_ROOT}/bin. "
            "Cannot verify GStreamer plugin versions."
        )
    endif()

    if(WIN32)
        set(_scanner_name "gst-plugin-scanner.exe")
    else()
        set(_scanner_name "gst-plugin-scanner")
    endif()

    set(_inspect_env
        "GST_PLUGIN_PATH=${_gst_pluginsdir}"
        "GST_PLUGIN_PATH_1_0=${_gst_pluginsdir}"
        "GST_PLUGIN_SYSTEM_PATH=${_gst_pluginsdir}"
        "GST_PLUGIN_SYSTEM_PATH_1_0=${_gst_pluginsdir}"
        "GST_REGISTRY=${CMAKE_BINARY_DIR}/gstreamer-registry-${GCS_GSTREAMER_VERSION}.bin"
        "GST_REGISTRY_FORK=no"
        "GST_REGISTRY_REUSE_PLUGIN_SCANNER=no"
    )

    if(_gst_pluginscannerdir AND EXISTS "${_gst_pluginscannerdir}/${_scanner_name}")
        list(APPEND _inspect_env
            "GST_PLUGIN_SCANNER=${_gst_pluginscannerdir}/${_scanner_name}"
            "GST_PLUGIN_SCANNER_1_0=${_gst_pluginscannerdir}/${_scanner_name}"
        )
    endif()

    list(LENGTH GCS_REQUIRED_GSTREAMER_PLUGINS _required_plugin_count)
    message(STATUS
        "Checking ${_required_plugin_count} required GStreamer plugins with ${_gst_inspect}. "
        "The first configure in a new build directory can take a while while gst-inspect "
        "creates ${CMAKE_BINARY_DIR}/gstreamer-registry-${GCS_GSTREAMER_VERSION}.bin."
    )

    set(_plugin_index 0)
    foreach(_plugin IN LISTS GCS_REQUIRED_GSTREAMER_PLUGINS)
        math(EXPR _plugin_index "${_plugin_index} + 1")
        message(STATUS
            "Checking GStreamer plugin ${_plugin_index}/${_required_plugin_count}: ${_plugin}"
        )
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env ${_inspect_env}
                "${_gst_inspect}" "--plugin" "${_plugin}"
            RESULT_VARIABLE _inspect_result
            OUTPUT_VARIABLE _inspect_stdout
            ERROR_VARIABLE _inspect_stderr
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE
            TIMEOUT 120
        )
        if(NOT _inspect_result EQUAL 0)
            if(_inspect_result MATCHES "timeout")
                message(FATAL_ERROR
                    "Timed out while inspecting GStreamer plugin '${_plugin}'.\n"
                    "Registry file: ${CMAKE_BINARY_DIR}/gstreamer-registry-${GCS_GSTREAMER_VERSION}.bin\n"
                    "${_inspect_stdout}\n${_inspect_stderr}"
                )
            endif()
            message(FATAL_ERROR
                "Required GStreamer plugin '${_plugin}' was not found in ${_gst_pluginsdir}.\n"
                "${_inspect_stdout}\n${_inspect_stderr}"
            )
        endif()

        if(NOT _inspect_stdout MATCHES "Version[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+)")
            message(FATAL_ERROR
                "Could not read the version for GStreamer plugin '${_plugin}'.\n"
                "${_inspect_stdout}"
            )
        endif()

        set(_plugin_version "${CMAKE_MATCH_1}")
        if(NOT "${_plugin_version}" STREQUAL "${GCS_GSTREAMER_VERSION}")
            message(FATAL_ERROR
                "GStreamer plugin '${_plugin}' has an incompatible version.\n"
                "Expected: ${GCS_GSTREAMER_VERSION} for Qt ${GCS_QT_VERSION}\n"
                "Found:    ${_plugin_version}\n"
                "Plugin source: ${_gst_pluginsdir}"
            )
        endif()
    endforeach()

    message(STATUS
        "Verified GStreamer ${GCS_GSTREAMER_VERSION} plugins for Qt ${GCS_QT_VERSION}: "
        "${GCS_REQUIRED_GSTREAMER_PLUGINS}"
    )
endfunction()

function(gcs_bootstrap_gstreamer)
    gcs_gst_resolve_version_for_qt()

    if(GCS_GSTREAMER_FORCE_DOWNLOAD AND (GCS_ALLOW_EXTERNAL_GSTREAMER OR GCS_USE_SYSTEM_GSTREAMER))
        message(FATAL_ERROR
            "GCS_GSTREAMER_FORCE_DOWNLOAD refreshes only the managed GStreamer SDK. "
            "Do not combine it with GCS_ALLOW_EXTERNAL_GSTREAMER or GCS_USE_SYSTEM_GSTREAMER."
        )
    endif()

    if(GCS_ALLOW_EXTERNAL_GSTREAMER AND GCS_USE_SYSTEM_GSTREAMER)
        message(FATAL_ERROR
            "Choose one GStreamer source: explicit GCS_EXTERNAL_GSTREAMER_ROOT/GCS_GSTREAMER_ROOT "
            "or GCS_USE_SYSTEM_GSTREAMER."
        )
    endif()

    if(GCS_ALLOW_EXTERNAL_GSTREAMER)
        if(GCS_EXTERNAL_GSTREAMER_ROOT)
            set(_external_root "${GCS_EXTERNAL_GSTREAMER_ROOT}")
        elseif(GCS_GSTREAMER_ROOT)
            set(_external_root "${GCS_GSTREAMER_ROOT}")
        else()
            message(FATAL_ERROR
                "GCS_ALLOW_EXTERNAL_GSTREAMER=ON requires GCS_EXTERNAL_GSTREAMER_ROOT "
                "or GCS_GSTREAMER_ROOT to be set."
            )
        endif()

        gcs_gst_apply_root("${_external_root}")
        set(GCS_GSTREAMER_SOURCE "external" CACHE INTERNAL "Resolved GStreamer SDK source.")
        message(STATUS
            "Using explicitly provided GStreamer ${GCS_GSTREAMER_VERSION} from ${GCS_GSTREAMER_ROOT}"
        )
        return()
    endif()

    if(GCS_USE_SYSTEM_GSTREAMER)
        gcs_gst_find_system_root(_system_root)
        gcs_gst_apply_root("${_system_root}")
        set(GCS_GSTREAMER_SOURCE "system" CACHE INTERNAL "Resolved GStreamer SDK source.")
        message(STATUS
            "Using system GStreamer ${GCS_GSTREAMER_VERSION} from ${GCS_GSTREAMER_ROOT}"
        )
        return()
    endif()

    if(GCS_EXTERNAL_GSTREAMER_ROOT)
        message(FATAL_ERROR
            "GCS_EXTERNAL_GSTREAMER_ROOT was provided, but external GStreamer SDKs are disabled. "
            "Set GCS_ALLOW_EXTERNAL_GSTREAMER=ON to use that path."
        )
    endif()

    gcs_gst_get_managed_root(_managed_root)
    if(NOT _managed_root)
        message(FATAL_ERROR "Managed GStreamer SDKs are not configured for ${CMAKE_SYSTEM_NAME}.")
    endif()

    if(GCS_GSTREAMER_ROOT AND NOT "${GCS_GSTREAMER_ROOT}" STREQUAL "${_managed_root}")
        message(STATUS
            "Ignoring cached GCS_GSTREAMER_ROOT=${GCS_GSTREAMER_ROOT} because "
            "GCS_ALLOW_EXTERNAL_GSTREAMER=OFF; managed root is ${_managed_root}."
        )
        set(GCS_GSTREAMER_ROOT "" CACHE PATH "Resolved GStreamer SDK/runtime root." FORCE)
    endif()

    gcs_gst_root_complete("${_managed_root}" _managed_complete)
    if(_managed_complete AND NOT GCS_GSTREAMER_FORCE_DOWNLOAD)
        gcs_gst_apply_root("${_managed_root}")
        set(GCS_GSTREAMER_SOURCE "managed" CACHE INTERNAL "Resolved GStreamer SDK source.")
        message(STATUS "Using managed GStreamer ${GCS_GSTREAMER_VERSION} from ${GCS_GSTREAMER_ROOT}")
        return()
    endif()

    if(GCS_FETCH_GSTREAMER)
        if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            gcs_gst_download_windows_sdk(_downloaded_root)
            gcs_gst_apply_root("${_downloaded_root}")
            set(GCS_GSTREAMER_SOURCE "managed" CACHE INTERNAL "Resolved GStreamer SDK source.")
            message(STATUS "Using managed GStreamer ${GCS_GSTREAMER_VERSION} from ${GCS_GSTREAMER_ROOT}")
            return()
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            gcs_gst_download_macos_sdk(_downloaded_root)
            gcs_gst_apply_root("${_downloaded_root}")
            set(GCS_GSTREAMER_SOURCE "managed" CACHE INTERNAL "Resolved GStreamer SDK source.")
            message(STATUS "Using managed GStreamer ${GCS_GSTREAMER_VERSION} from ${GCS_GSTREAMER_ROOT}")
            return()
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            message(FATAL_ERROR
                "Automatic GStreamer SDK download is not available on Linux. "
                "Place a complete GStreamer ${GCS_GSTREAMER_VERSION} SDK at ${_managed_root}, "
                "set GCS_ALLOW_EXTERNAL_GSTREAMER=ON and GCS_EXTERNAL_GSTREAMER_ROOT=<path>, "
                "or set GCS_USE_SYSTEM_GSTREAMER=ON to use a complete matching system SDK."
            )
        endif()
    endif()

    set(GCS_GSTREAMER_ROOT "" CACHE PATH "Resolved GStreamer SDK/runtime root." FORCE)
    message(FATAL_ERROR
        "GStreamer is required, but the managed GStreamer ${GCS_GSTREAMER_VERSION} SDK "
        "was not found at ${_managed_root}. Enable GCS_FETCH_GSTREAMER, provide "
        "GCS_EXTERNAL_GSTREAMER_ROOT with GCS_ALLOW_EXTERNAL_GSTREAMER=ON, or set "
        "GCS_USE_SYSTEM_GSTREAMER=ON to use a complete matching system SDK."
    )
endfunction()
