Qt is bootstrapped with `aqtinstall` during CMake configure on macOS, Linux, and Windows.
By default, CMake first tries to use a Qt SDK that is discoverable by standard names and paths.
If no usable SDK is found, or if the discovered SDK is too old or incomplete, CMake falls back
to the project-managed Qt SDK under `External/Qt`.

How Qt is fetched:

1. CMake loads [cmake/BootstrapQt.cmake](cmake/BootstrapQt.cmake).
2. Unless an explicit or system-only Qt mode is enabled, CMake first searches `Qt6_DIR`, `QTDIR`/`QT_DIR`, `CMAKE_PREFIX_PATH`, `qmake`, `qtpaths`, and common install locations.
3. A discovered SDK is accepted only if it is complete and its version is at least `GCS_MINIMUM_QT_VERSION` (default `6.2.0`).
4. If no acceptable system Qt SDK is found, CMake resolves the expected managed SDK path: `External/Qt/<version>/<platform-dir>`.
5. If that managed SDK is missing and `GCS_FETCH_QT_WITH_AQT=ON`, the bootstrap tries, in order:
   - `uv tool run --from aqtinstall aqt ...`
   - a preinstalled `aqt` executable
   - a build-local Python virtual environment with `python -m aqt`
6. `aqtinstall` downloads the official prebuilt Qt packages into `External/Qt/<version>/<platform-dir>`.
7. `CMAKE_PREFIX_PATH` and `Qt6_DIR` are forced to the selected SDK, then normal `find_package(Qt6 ...)` continues.

Qt source options:

- Auto mode: default. CMake prefers a usable system Qt SDK and falls back to the managed SDK if discovery fails.
- Managed SDK: use `GCS_QT_FORCE_DOWNLOAD=ON` to delete and refresh the managed SDK under `GCS_QT_INSTALL_ROOT`, even if a system SDK is available.
- Explicit SDK path: set `GCS_ALLOW_EXTERNAL_QT=ON` and `GCS_EXTERNAL_QT_ROOT=<path>`.
- System-only SDK: set `GCS_USE_SYSTEM_QT=ON` to fail unless a usable system Qt SDK is found.

Using an external Qt SDK:

```bash
cmake --preset windows-debug ^
  -DGCS_ALLOW_EXTERNAL_QT=ON ^
  -DGCS_EXTERNAL_QT_ROOT=C:/Qt/6.10.0/msvc2022_64 ^
  -DGCS_QT_VERSION=6.10.0
```

`GCS_EXTERNAL_QT_ROOT` is ignored unless `GCS_ALLOW_EXTERNAL_QT=ON`. The external Qt SDK must be complete and at least `GCS_MINIMUM_QT_VERSION`.
`GCS_QT_FORCE_DOWNLOAD` cannot be combined with external or system Qt modes.

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

Project layout:

- `Core/Inc` and `Core/Src` contain the application shell and main window.
- `Modules/Video/Inc` and `Modules/Video/Src` contain the GStreamer video receiver module.
- `External` is reserved for third-party git submodules and bootstrapped SDKs.

How GStreamer is connected:

- GStreamer is required. The project does not support configuring, building, or running without it.
- The normal presets configure video support directly; there are no separate video-enabled preset variants.
- If no complete compatible GStreamer SDK is selected, CMake configuration fails.

How GStreamer is found:

- CMake first selects the managed download version from the Qt compatibility table in [cmake/BootstrapGStreamer.cmake](cmake/BootstrapGStreamer.cmake). Currently managed Qt `6.10.x` downloads managed GStreamer `1.28.1`.
- In the default auto mode, CMake searches `GSTREAMER_*` roots, `gst-inspect-1.0` in `PATH`, `pkg-config`, and standard OS install locations before it falls back to the managed SDK.
- A discovered SDK is accepted only if it is complete, its version is at least `GCS_MINIMUM_GSTREAMER_VERSION` (default `1.20.0`), `pkg-config` can resolve the required modules from that SDK, and all required plugins pass `gst-inspect-1.0`.
- CMake asks `pkg-config` for exact `gstreamer-1.0`, `gstreamer-app-1.0`, and `gstreamer-video-1.0` versions that match the selected SDK and creates `PkgConfig::GSTREAMER`.
- `PKG_CONFIG_LIBDIR` is restricted to the selected SDK root, so an unrelated system `pkg-config` database is not searched.
- To use an already installed GStreamer SDK by path, set both `GCS_ALLOW_EXTERNAL_GSTREAMER=ON` and `GCS_EXTERNAL_GSTREAMER_ROOT=<path>`.
- To require a system GStreamer SDK instead of falling back to the managed one, set `GCS_USE_SYSTEM_GSTREAMER=ON`.
- At runtime, [GstVideoReceiver.cpp](Modules/Video/Src/GstVideoReceiver.cpp) looks for sibling folders like `gstreamer-1.0`, `gio/modules`, `gstreamer-runtime`, and `gstreamer-tools`, then sets process-local `GST_PLUGIN_PATH`, `GIO_EXTRA_MODULES`, `GST_PLUGIN_SCANNER`, and `PATH` before calling `gst_init_check()`. CMake does not write those paths into the user or system environment.

GStreamer source options:

- Auto mode: default on Windows and macOS. CMake prefers a usable system GStreamer SDK and falls back to the managed SDK if discovery fails.
- Linux presets use system GStreamer by default (`GCS_USE_SYSTEM_GSTREAMER=ON`) because there is no official managed Linux SDK download path.
- Linux presets use the aqt-managed Qt SDK by default (`GCS_PREFER_SYSTEM_QT=OFF`); Qt does not need to be installed through the distro package manager.
- Managed SDK: use `GCS_GSTREAMER_FORCE_DOWNLOAD=ON` to refresh only the managed SDK under `GCS_GSTREAMER_INSTALL_ROOT`, even if a system SDK is available.
- Explicit SDK path: set `GCS_ALLOW_EXTERNAL_GSTREAMER=ON` and `GCS_EXTERNAL_GSTREAMER_ROOT=<path>`.
- System-only SDK: set `GCS_USE_SYSTEM_GSTREAMER=ON`.
- The force-download option cannot be combined with explicit or system GStreamer modes.
- Required plugins are always verified during configure with `gst-inspect-1.0`.
- CMake does not add the managed SDK `bin` directory to the user or system `PATH`.

Using an external GStreamer SDK:

```bash
cmake --preset windows-debug ^
  -DGCS_ALLOW_EXTERNAL_GSTREAMER=ON ^
  -DGCS_EXTERNAL_GSTREAMER_ROOT=C:/gstreamer/1.0/msvc_x86_64
```

The external SDK must contain usable development files, runtime tools, and required plugins. CMake verifies it with both `pkg-config` and `gst-inspect-1.0`; if the SDK is incomplete, too old, or missing plugins, configure fails.

How GStreamer files are staged:

- Qt itself is downloaded automatically.
- GStreamer is bootstrapped by [cmake/BootstrapGStreamer.cmake](cmake/BootstrapGStreamer.cmake) when the managed SDK is selected and `GCS_FETCH_GSTREAMER=ON`.
- On Windows, CMake downloads the official GStreamer MSVC SDK installer into `External/GStreamer` and installs it silently into a project-local prefix.
- On macOS, CMake downloads the official runtime and development `.pkg` files and merges them into `External/GStreamer`.
- On Linux, automatic GStreamer download is still unavailable. The stock Linux presets use the distro packages through `pkg-config`.
- CMake queries `pkg-config` for `pluginsdir`, `pluginscannerdir`, `giomoduledir`, and related paths.
- On Windows, those directories are copied next to the built app, because local `.exe` execution usually needs nearby DLLs, plugins, and the plugin scanner.
- On macOS and Linux, stale bundled GStreamer folders are removed so the selected SDK/runtime root is used consistently.

Per-OS quick start:

macOS:

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
./build/macos-debug/GCS_player.app/Contents/MacOS/GCS_player
```

On a fresh macOS build directory, the first `cmake --preset ...` or `cmake --build --preset ...`
can take a few minutes while CMake expands the managed GStreamer packages and `gst-inspect-1.0`
builds a local plugin registry under `build/<preset>`.

Linux:

Install the build tools and the GStreamer plugins used by the video receiver. Qt is fetched by `aqtinstall` during CMake configure.

```bash
# Arch / EndeavourOS
sudo pacman -S --needed cmake ninja pkgconf gstreamer gst-plugins-base \
  gst-plugins-good gst-plugins-bad gst-libav

# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build pkg-config libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev gstreamer1.0-tools gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav
```

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
