cmake_minimum_required(VERSION 3.21)

foreach(_required_var IN ITEMS
    GCS_BUNDLE_EXECUTABLE
    GCS_BUNDLE_DIR
    GCS_BUNDLE_ARCHIVE
    GCS_BUNDLE_QT_PLUGIN_DIR
    GCS_BUNDLE_GST_INSPECT
    GCS_BUNDLE_GST_PLUGIN_SCANNER_DIR
)
    if(NOT DEFINED ${_required_var} OR "${${_required_var}}" STREQUAL "")
        message(FATAL_ERROR "${_required_var} is required.")
    endif()
endforeach()

if(DEFINED GCS_BUNDLE_LIBRARY_DIRS)
    string(REPLACE "|" ";" GCS_BUNDLE_LIBRARY_DIRS "${GCS_BUNDLE_LIBRARY_DIRS}")
endif()
if(DEFINED GCS_BUNDLE_GSTREAMER_PLUGINS)
    string(REPLACE "|" ";" GCS_BUNDLE_GSTREAMER_PLUGINS "${GCS_BUNDLE_GSTREAMER_PLUGINS}")
endif()

set(_bundle_root "${GCS_BUNDLE_DIR}")
set(_bundle_bin_dir "${_bundle_root}/bin")
set(_bundle_lib_dir "${_bundle_root}/lib")
set(_bundle_qt_plugins_dir "${_bundle_root}/plugins")
set(_bundle_gst_plugins_dir "${_bundle_bin_dir}/gstreamer-1.0")
set(_bundle_gst_tools_dir "${_bundle_bin_dir}/gstreamer-tools/gstreamer-1.0")
set(_bundle_gio_modules_dir "${_bundle_bin_dir}/gio/modules")

file(REMOVE_RECURSE "${_bundle_root}")
file(MAKE_DIRECTORY
    "${_bundle_bin_dir}"
    "${_bundle_lib_dir}"
    "${_bundle_qt_plugins_dir}"
    "${_bundle_gst_plugins_dir}"
    "${_bundle_gst_tools_dir}"
)

file(COPY "${GCS_BUNDLE_EXECUTABLE}"
    DESTINATION "${_bundle_bin_dir}"
    FILE_PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)
get_filename_component(_source_executable_name "${GCS_BUNDLE_EXECUTABLE}" NAME)
set(_bundle_executable "${_bundle_bin_dir}/GCS_player.bin")
file(RENAME "${_bundle_bin_dir}/${_source_executable_name}" "${_bundle_executable}")

file(WRITE "${_bundle_bin_dir}/qt.conf" [=[
[Paths]
Prefix = ..
Plugins = plugins
Libraries = lib
]=])

file(WRITE "${_bundle_bin_dir}/GCS_player" [=[#!/usr/bin/env sh
set -eu

SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SELF_DIR/.." && pwd)

export LD_LIBRARY_PATH="$ROOT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$ROOT_DIR/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$ROOT_DIR/plugins/platforms"

export GST_PLUGIN_PATH="$SELF_DIR/gstreamer-1.0"
export GST_PLUGIN_PATH_1_0="$SELF_DIR/gstreamer-1.0"
export GST_PLUGIN_SYSTEM_PATH="$SELF_DIR/gstreamer-1.0"
export GST_PLUGIN_SYSTEM_PATH_1_0="$SELF_DIR/gstreamer-1.0"
export GST_PLUGIN_SCANNER="$SELF_DIR/gstreamer-tools/gstreamer-1.0/gst-plugin-scanner"
export GST_PLUGIN_SCANNER_1_0="$GST_PLUGIN_SCANNER"
export GIO_EXTRA_MODULES="$SELF_DIR/gio/modules"
export GST_REGISTRY_FORK=no
export GST_REGISTRY_REUSE_PLUGIN_SCANNER=no

exec "$SELF_DIR/GCS_player.bin" "$@"
]=])
file(CHMOD "${_bundle_bin_dir}/GCS_player"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

file(WRITE "${_bundle_root}/GCS_player" [=[#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$ROOT_DIR/bin/GCS_player" "$@"
]=])
file(CHMOD "${_bundle_root}/GCS_player"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

file(WRITE "${_bundle_root}/README.txt" [=[
GCS_player portable Linux bundle.

Run:
  ./GCS_player

The launcher pins Qt and GStreamer to the local lib/plugins folders before
starting bin/GCS_player.bin.
]=])

function(gcs_bundle_copy_qt_plugins plugin_subdir)
    set(_plugin_destination_dir "${_bundle_qt_plugins_dir}/${plugin_subdir}")
    foreach(_plugin_pattern IN LISTS ARGN)
        file(GLOB _plugin_files
            LIST_DIRECTORIES false
            "${GCS_BUNDLE_QT_PLUGIN_DIR}/${plugin_subdir}/${_plugin_pattern}"
        )
        if(_plugin_files)
            file(MAKE_DIRECTORY "${_plugin_destination_dir}")
            foreach(_plugin_file IN LISTS _plugin_files)
                file(COPY "${_plugin_file}" DESTINATION "${_plugin_destination_dir}")
            endforeach()
        endif()
    endforeach()
endfunction()

gcs_bundle_copy_qt_plugins(platforms
    "libqxcb.so"
    "libqwayland.so"
    "libqeglfs.so"
    "libqlinuxfb.so"
    "libqoffscreen.so"
    "libqminimal.so"
    "libqminimalegl.so"
)
gcs_bundle_copy_qt_plugins(xcbglintegrations "*.so")
gcs_bundle_copy_qt_plugins(egldeviceintegrations "*.so")
gcs_bundle_copy_qt_plugins(generic "*.so")
gcs_bundle_copy_qt_plugins(multimedia "*.so")
gcs_bundle_copy_qt_plugins(imageformats
    "libqgif.so"
    "libqico.so"
    "libqjpeg.so"
    "libqsvg.so"
    "libqtga.so"
    "libqtiff.so"
    "libqwbmp.so"
    "libqwebp.so"
)
gcs_bundle_copy_qt_plugins(iconengines "libqsvgicon.so")
gcs_bundle_copy_qt_plugins(platformthemes
    "libqgtk3.so"
    "libqxdgdesktopportal.so"
)
gcs_bundle_copy_qt_plugins(tls "*.so")
gcs_bundle_copy_qt_plugins(wayland-decoration-client "*.so")
gcs_bundle_copy_qt_plugins(wayland-graphics-integration-client "*.so")
gcs_bundle_copy_qt_plugins(wayland-shell-integration "*.so")

file(GLOB_RECURSE _qt_plugin_files
    LIST_DIRECTORIES false
    "${_bundle_qt_plugins_dir}/*.so"
    "${_bundle_qt_plugins_dir}/*.so.*"
)

set(_gst_plugin_files)
foreach(_gst_plugin IN LISTS GCS_BUNDLE_GSTREAMER_PLUGINS)
    execute_process(
        COMMAND "${GCS_BUNDLE_GST_INSPECT}" --plugin "${_gst_plugin}"
        RESULT_VARIABLE _gst_inspect_result
        OUTPUT_VARIABLE _gst_inspect_output
        ERROR_VARIABLE _gst_inspect_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _gst_inspect_result EQUAL 0)
        message(FATAL_ERROR
            "Could not inspect GStreamer plugin '${_gst_plugin}'.\n"
            "${_gst_inspect_output}\n${_gst_inspect_error}"
        )
    endif()

    string(REGEX MATCH "Filename[ \t]+([^ \r\n]+)" _gst_plugin_filename_match "${_gst_inspect_output}")
    if(NOT CMAKE_MATCH_1 OR NOT EXISTS "${CMAKE_MATCH_1}")
        message(FATAL_ERROR "Could not resolve the file for GStreamer plugin '${_gst_plugin}'.")
    endif()

    get_filename_component(_gst_plugin_filename "${CMAKE_MATCH_1}" NAME)
    file(COPY "${CMAKE_MATCH_1}" DESTINATION "${_bundle_gst_plugins_dir}")
    list(APPEND _gst_plugin_files "${_bundle_gst_plugins_dir}/${_gst_plugin_filename}")
endforeach()

set(_scanner_name "gst-plugin-scanner")
if(EXISTS "${GCS_BUNDLE_GST_PLUGIN_SCANNER_DIR}/${_scanner_name}")
    file(COPY "${GCS_BUNDLE_GST_PLUGIN_SCANNER_DIR}/${_scanner_name}"
        DESTINATION "${_bundle_gst_tools_dir}"
        FILE_PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE
    )
endif()

set(_gio_module_files)
if(DEFINED GCS_BUNDLE_GIO_MODULE_DIR AND EXISTS "${GCS_BUNDLE_GIO_MODULE_DIR}")
    file(MAKE_DIRECTORY "${_bundle_gio_modules_dir}")
    file(GLOB _source_gio_modules
        LIST_DIRECTORIES false
        "${GCS_BUNDLE_GIO_MODULE_DIR}/*.so"
        "${GCS_BUNDLE_GIO_MODULE_DIR}/*.so.*"
    )
    foreach(_gio_module IN LISTS _source_gio_modules)
        get_filename_component(_gio_module_name "${_gio_module}" NAME)
        file(COPY "${_gio_module}" DESTINATION "${_bundle_gio_modules_dir}")
        list(APPEND _gio_module_files "${_bundle_gio_modules_dir}/${_gio_module_name}")
    endforeach()
endif()

set(_runtime_libraries ${_qt_plugin_files} ${_gst_plugin_files} ${_gio_module_files})
set(_dependency_args EXECUTABLES "${_bundle_executable}")
if(_runtime_libraries)
    list(APPEND _dependency_args LIBRARIES ${_runtime_libraries})
endif()
if(GCS_BUNDLE_LIBRARY_DIRS)
    list(APPEND _dependency_args DIRECTORIES ${GCS_BUNDLE_LIBRARY_DIRS})
endif()

file(GET_RUNTIME_DEPENDENCIES
    ${_dependency_args}
    RESOLVED_DEPENDENCIES_VAR _resolved_dependencies
    UNRESOLVED_DEPENDENCIES_VAR _unresolved_dependencies
    POST_EXCLUDE_REGEXES
        [[.*/ld-linux[^/]*\.so.*]]
        [[.*/libc\.so(\..*)?$]]
        [[.*/libpthread\.so(\..*)?$]]
        [[.*/libdl\.so(\..*)?$]]
        [[.*/librt\.so(\..*)?$]]
)

if(_unresolved_dependencies)
    list(JOIN _unresolved_dependencies "\n  " _unresolved_text)
    message(FATAL_ERROR "Unresolved portable bundle dependencies:\n  ${_unresolved_text}")
endif()

function(gcs_bundle_copy_library library_path destination_dir manifest_var)
    if(NOT EXISTS "${library_path}")
        return()
    endif()

    get_filename_component(_dependency_name "${library_path}" NAME)
    file(REAL_PATH "${library_path}" _dependency_real_path)
    get_filename_component(_dependency_real_name "${_dependency_real_path}" NAME)

    file(COPY "${_dependency_real_path}" DESTINATION "${destination_dir}")
    if(NOT _dependency_name STREQUAL _dependency_real_name)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E create_symlink
                "${_dependency_real_name}"
                "${destination_dir}/${_dependency_name}"
            RESULT_VARIABLE _symlink_result
        )
        if(NOT _symlink_result EQUAL 0)
            message(FATAL_ERROR "Could not create symlink for ${_dependency_name}.")
        endif()
    endif()

    set(${manifest_var} "${${manifest_var}};${library_path}" PARENT_SCOPE)
endfunction()

set(_copied_dependencies)
list(SORT _resolved_dependencies)
foreach(_dependency IN LISTS _resolved_dependencies)
    gcs_bundle_copy_library("${_dependency}" "${_bundle_lib_dir}" _copied_dependencies)
endforeach()

if(CMAKE_HOST_UNIX)
    file(RPATH_SET FILE "${_bundle_executable}" NEW_RPATH "$ORIGIN/../lib")
endif()

list(SORT _copied_dependencies)
string(REPLACE ";" "\n" _manifest_text "${_copied_dependencies}")
file(WRITE "${_bundle_root}/runtime-manifest.txt" "${_manifest_text}\n")

get_filename_component(_archive_parent "${GCS_BUNDLE_ARCHIVE}" DIRECTORY)
file(MAKE_DIRECTORY "${_archive_parent}")
file(REMOVE "${GCS_BUNDLE_ARCHIVE}")

get_filename_component(_bundle_parent "${_bundle_root}" DIRECTORY)
get_filename_component(_bundle_name "${_bundle_root}" NAME)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar czf "${GCS_BUNDLE_ARCHIVE}" "${_bundle_name}"
    WORKING_DIRECTORY "${_bundle_parent}"
    RESULT_VARIABLE _archive_result
)
if(NOT _archive_result EQUAL 0)
    message(FATAL_ERROR "Could not create portable bundle archive: ${GCS_BUNDLE_ARCHIVE}")
endif()

message(STATUS "Created portable Linux bundle: ${GCS_BUNDLE_ARCHIVE}")
