include_guard(GLOBAL)

option(GCS_FETCH_GSTREAMER "Automatically download a GStreamer SDK when GStreamer support is required." ON)
option(GCS_GSTREAMER_FORCE_DOWNLOAD "Ignore system GStreamer SDKs and use a project-local SDK." OFF)
option(GCS_GSTREAMER_REQUIRE_CHECKSUM "Fail if an auto-downloaded GStreamer package has no pinned checksum." OFF)

set(GCS_GSTREAMER_VERSION "1.28.1" CACHE STRING "GStreamer SDK version used for automatic downloads.")
set(GCS_GSTREAMER_INSTALL_ROOT "${PROJECT_SOURCE_DIR}/External/GStreamer" CACHE PATH
    "Root directory that stores auto-downloaded GStreamer SDKs."
)

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

function(gcs_gst_env_prepend env_name new_path)
    if(NOT new_path OR NOT EXISTS "${new_path}")
        return()
    endif()

    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(_separator ";")
    else()
        set(_separator ":")
    endif()

    set(_current "$ENV{${env_name}}")
    if(_current)
        set(ENV{${env_name}} "${new_path}${_separator}${_current}")
    else()
        set(ENV{${env_name}} "${new_path}")
    endif()
endfunction()

function(gcs_gst_to_key input output_var)
    string(TOUPPER "${input}" _key)
    string(REPLACE "." "_" _key "${_key}")
    string(REPLACE "-" "_" _key "${_key}")
    set(${output_var} "${_key}" PARENT_SCOPE)
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

function(gcs_gst_windows_sdk_complete root_dir output_var)
    set(_required_paths
        "bin/pkg-config.exe"
        "include/gstreamer-1.0"
        "lib/gstreamer-1.0"
        "lib/pkgconfig/gstreamer-1.0.pc"
    )

    set(_is_complete TRUE)
    foreach(_path IN LISTS _required_paths)
        if(NOT EXISTS "${root_dir}/${_path}")
            set(_is_complete FALSE)
            break()
        endif()
    endforeach()

    set(${output_var} "${_is_complete}" PARENT_SCOPE)
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
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(_gst_arch "arm64")
        set(_checksum_platform "WINDOWS_MSVC_ARM64")
    else()
        set(_gst_arch "x86_64")
        set(_checksum_platform "WINDOWS_MSVC_X86_64")
    endif()

    if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR
            "Automatic GStreamer download on Windows requires a 64-bit MSVC generator."
        )
    endif()

    if(GCS_GSTREAMER_VERSION VERSION_LESS "1.28.0")
        message(FATAL_ERROR
            "Automatic Windows GStreamer download expects the unified 1.28+ installer. "
            "Set GCS_GSTREAMER_VERSION to 1.28.1 or provide GCS_GSTREAMER_ROOT manually."
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
    if(NOT _sdk_root)
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
        if(NOT _installer_result EQUAL 0)
            file(REMOVE_RECURSE "${_sdk_dir}")
            message(FATAL_ERROR
                "GStreamer installer failed with exit code ${_installer_result}.\n"
                "stderr: ${_installer_stderr}\n"
                "Installer log: ${_installer_log}"
            )
        endif()

        gcs_gst_find_windows_sdk_root("${_sdk_dir}" _sdk_root)
    endif()

    gcs_gst_windows_sdk_complete("${_sdk_root}" _sdk_complete)
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

    if(EXISTS "${_sdk_root}/.merge_complete"
       AND EXISTS "${_sdk_root}/lib/gstreamer-1.0"
       AND EXISTS "${_sdk_root}/include/gstreamer-1.0"
       AND EXISTS "${_sdk_root}/lib/pkgconfig/gstreamer-1.0.pc")
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

    if(NOT EXISTS "${_sdk_root}/lib/gstreamer-1.0"
       OR NOT EXISTS "${_sdk_root}/include/gstreamer-1.0"
       OR NOT EXISTS "${_sdk_root}/lib/pkgconfig/gstreamer-1.0.pc")
        file(REMOVE_RECURSE "${_sdk_root}")
        message(FATAL_ERROR "Downloaded macOS GStreamer SDK is incomplete.")
    endif()

    set(${output_root_var} "${_sdk_root}" PARENT_SCOPE)
endfunction()

function(gcs_gst_apply_root root_dir)
    if(NOT root_dir OR NOT EXISTS "${root_dir}")
        return()
    endif()

    set(GCS_GSTREAMER_ROOT "${root_dir}" CACHE PATH "Resolved GStreamer SDK/runtime root." FORCE)
    gcs_gst_env_prepend(PATH "${root_dir}/bin")

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(_pkg_config_exe "${root_dir}/bin/pkg-config.exe")
        if(EXISTS "${_pkg_config_exe}")
            set(ENV{PKG_CONFIG} "${_pkg_config_exe}")
            set(PKG_CONFIG_EXECUTABLE "${_pkg_config_exe}" CACHE FILEPATH "pkg-config executable" FORCE)
            set(_pkg_config_args
                "--dont-define-prefix"
                "--define-variable=prefix=${root_dir}"
                "--define-variable=libdir=${root_dir}/lib"
                "--define-variable=includedir=${root_dir}/include"
            )
            set(PKG_CONFIG_ARGN "${_pkg_config_args}" CACHE STRING "Extra arguments for pkg-config" FORCE)
        endif()
        set(ENV{PKG_CONFIG_DONT_DEFINE_PREFIX} "1")
        set(ENV{PKG_CONFIG_LIBDIR} "${root_dir}/lib/pkgconfig;${root_dir}/lib/gstreamer-1.0/pkgconfig")
        set(ENV{PKG_CONFIG_PATH} "")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        if(EXISTS "${root_dir}/bin/pkg-config")
            set(ENV{PKG_CONFIG} "${root_dir}/bin/pkg-config")
            set(PKG_CONFIG_EXECUTABLE "${root_dir}/bin/pkg-config" CACHE FILEPATH "pkg-config executable" FORCE)
        endif()
        set(ENV{PKG_CONFIG_LIBDIR} "${root_dir}/lib/pkgconfig:${root_dir}/lib/gstreamer-1.0/pkgconfig")
    else()
        gcs_gst_env_prepend(PKG_CONFIG_PATH "${root_dir}/lib/pkgconfig")
        gcs_gst_env_prepend(PKG_CONFIG_PATH "${root_dir}/lib64/pkgconfig")
        gcs_gst_env_prepend(PKG_CONFIG_PATH "${root_dir}/lib/x86_64-linux-gnu/pkgconfig")
        gcs_gst_env_prepend(PKG_CONFIG_PATH "${root_dir}/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu/pkgconfig")
    endif()
endfunction()

function(gcs_bootstrap_gstreamer)
    if(GCS_GSTREAMER_MODE STREQUAL "OFF")
        return()
    endif()

    set(_is_required FALSE)
    if(GCS_GSTREAMER_MODE STREQUAL "ON")
        set(_is_required TRUE)
    endif()

    if(GCS_GSTREAMER_ROOT AND EXISTS "${GCS_GSTREAMER_ROOT}" AND NOT GCS_GSTREAMER_FORCE_DOWNLOAD)
        gcs_gst_apply_root("${GCS_GSTREAMER_ROOT}")
        return()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if(NOT GCS_GSTREAMER_FORCE_DOWNLOAD)
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
                set(_candidates
                    "$ENV{GSTREAMER_1_0_ROOT_ARM64}"
                    "$ENV{GSTREAMER_1_0_ROOT_MSVC_ARM64}"
                    "C:/Program Files/gstreamer/1.0/msvc_arm64"
                    "C:/gstreamer/1.0/msvc_arm64"
                )
            else()
                set(_candidates
                    "$ENV{GSTREAMER_1_0_ROOT_X86_64}"
                    "$ENV{GSTREAMER_1_0_ROOT_MSVC_X86_64}"
                    "C:/Program Files/gstreamer/1.0/msvc_x86_64"
                    "C:/gstreamer/1.0/msvc_x86_64"
                )
            endif()

            foreach(_candidate IN LISTS _candidates)
                if(_candidate AND EXISTS "${_candidate}")
                    gcs_gst_windows_sdk_complete("${_candidate}" _candidate_complete)
                    if(_candidate_complete)
                        gcs_gst_apply_root("${_candidate}")
                        return()
                    elseif(_is_required)
                        message(STATUS
                            "Existing GStreamer at ${_candidate} is incomplete; downloading a project-local SDK."
                        )
                    endif()
                endif()
            endforeach()
        endif()

        if(GCS_FETCH_GSTREAMER AND _is_required)
            gcs_gst_download_windows_sdk(_downloaded_root)
            gcs_gst_apply_root("${_downloaded_root}")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        if(NOT GCS_GSTREAMER_FORCE_DOWNLOAD)
            foreach(_candidate IN ITEMS
                "/Library/Frameworks/GStreamer.framework/Versions/1.0"
                "/opt/homebrew/opt/gstreamer"
                "/usr/local/opt/gstreamer"
            )
                if(EXISTS "${_candidate}/lib/pkgconfig/gstreamer-1.0.pc")
                    gcs_gst_apply_root("${_candidate}")
                    return()
                endif()
            endforeach()
        endif()

        if(GCS_FETCH_GSTREAMER AND _is_required)
            gcs_gst_download_macos_sdk(_downloaded_root)
            gcs_gst_apply_root("${_downloaded_root}")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if(NOT GCS_GSTREAMER_ROOT)
            set(GCS_GSTREAMER_ROOT "/usr" CACHE PATH "Resolved GStreamer SDK/runtime root." FORCE)
        endif()
        gcs_gst_apply_root("${GCS_GSTREAMER_ROOT}")
    endif()
endfunction()
