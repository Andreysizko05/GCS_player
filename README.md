Qt is bootstrapped with `aqtinstall` during CMake configure on macOS, Linux, and Windows.

How Qt is fetched:

1. CMake loads [cmake/BootstrapQt.cmake](cmake/BootstrapQt.cmake).
2. If `Qt6_DIR` is already provided, that Qt is used as-is.
3. Otherwise the bootstrap tries, in order:
   - `uv tool run --from aqtinstall aqt ...`
   - a preinstalled `aqt` executable
   - a build-local Python virtual environment with `python -m aqt`
4. `aqtinstall` downloads the official prebuilt Qt packages into `.qt-sdk/<version>/<platform-dir>`.
5. `CMAKE_PREFIX_PATH` and `Qt6_DIR` are pointed at that SDK, then normal `find_package(Qt6 ...)` continues.

Why `uv` is used if available:

- `uv` is not a replacement for `aqtinstall`.
- `aqtinstall` is still the tool that talks to the Qt package repository and downloads Qt.
- `uv` is only the launcher/package manager for the Python tool itself.
- `uv tool run --from aqtinstall aqt ...` runs `aqt` in an isolated ephemeral environment, so we avoid polluting a global Python or relying on `pip install --user`.
- If `uv` is missing, the fallback still works by creating `build/<preset>/.aqt-venv` and installing `aqtinstall` there.

Why `aqtinstall` is used for Qt:

- It installs official prebuilt Qt SDKs non-interactively.
- It works well in CI and CMake presets.
- It keeps the Qt layout predictable across all three desktop OSes.
- It avoids rebuilding Qt from source through `vcpkg`, which is much slower and more fragile.

How GStreamer is connected:

- GStreamer is optional and is controlled by `GCS_GSTREAMER_MODE`.
- `AUTO`: if `pkg-config` can find `gstreamer-1.0`, `gstreamer-app-1.0`, and `gstreamer-video-1.0`, video support is enabled. Otherwise the app builds without video.
- `ON`: configuration fails if those development packages are missing. The `*-gstreamer` presets use this mode.
- `OFF`: video support is disabled even if GStreamer is installed.

How GStreamer is found:

- CMake asks `pkg-config` for compiler/linker flags and creates `PkgConfig::GSTREAMER`.
- If you have a non-standard GStreamer installation, set `GCS_GSTREAMER_ROOT` to its prefix. CMake will prepend `<root>/bin` to `PATH` and common `pkgconfig` directories to `PKG_CONFIG_PATH`.
- The `*-gstreamer` presets set `GCS_GSTREAMER_FORCE_DOWNLOAD=ON`, so they use a project-local SDK when supported instead of relying on a partial system install.
- At runtime, [GstVideoReceiver.cpp](GstVideoReceiver.cpp) looks for sibling folders like `gstreamer-1.0`, `gio/modules`, `gstreamer-runtime`, and `gstreamer-tools`, then exports `GST_PLUGIN_PATH`, `GIO_EXTRA_MODULES`, `GST_PLUGIN_SCANNER`, and `PATH` before calling `gst_init_check()`.

How GStreamer files are staged:

- Qt itself is downloaded automatically.
- GStreamer is also bootstrapped by [cmake/BootstrapGStreamer.cmake](cmake/BootstrapGStreamer.cmake) when `GCS_GSTREAMER_MODE=ON` and `GCS_FETCH_GSTREAMER=ON`.
- On Windows, CMake downloads the official GStreamer MSVC SDK installer into `.gstreamer-sdk` and installs it silently into a project-local prefix.
- On macOS, CMake downloads the official runtime and development `.pkg` files and merges them into `.gstreamer-sdk`.
- On Linux, the project follows QGroundControl's default desktop approach and uses the system GStreamer packages under `/usr`; install the distro development packages if `pkg-config` cannot find them.
- When GStreamer is enabled, CMake queries `pkg-config` for `pluginsdir`, `pluginscannerdir`, `giomoduledir`, and related paths.
- On Windows, those directories are copied next to the built app, because local `.exe` execution usually needs nearby DLLs, plugins, and the plugin scanner.
- On macOS and Linux, the default expectation is a system-installed GStreamer runtime. The app will still use local sibling folders if they exist, but the build does not force-copy the whole GStreamer tree there.

Per-OS quick start:

macOS:

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
./build/macos-debug/GCS_player.app/Contents/MacOS/GCS_player
```

Linux:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
./build/linux-debug/GCS_player
```

Windows:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
.\build\windows-debug\Debug\GCS_player.exe
```

The default Windows preset targets `Visual Studio 2026`. Qt is still fetched from the official `aqtinstall` desktop package `win64_msvc2022_64`, which is the current prebuilt MSVC Qt package used by this project.
