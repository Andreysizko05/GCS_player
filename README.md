# GCS_player

Qt 6 + GStreamer desktop video receiver.

## Dependencies

CMake first tries installed SDKs:

- Qt 6 is searched through CMake/qmake/qtpaths and `pkg-config`; if it is missing, CMake installs Qt with `aqtinstall` into `External/Qt`.
- GStreamer is searched through `pkg-config` and `gst-inspect-1.0`; if it is missing, CMake downloads the official SDK on Windows/macOS. On Linux, install the distro packages below.

Arch / EndeavourOS:

```bash
sudo pacman -S --needed base-devel cmake ninja pkgconf python \
  gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-libav
```

Debian / Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build pkg-config python3 python3-venv \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-tools \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
  gstreamer1.0-libav
```

macOS:

```bash
xcode-select --install
brew install cmake ninja pkg-config python
```

Windows:

- Install Visual Studio Build Tools with the C++ desktop workload.
- Install CMake and Python 3, or make sure they are available in `PATH`.

## Build

Linux:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
./build/linux-debug/GCS_player
```

macOS:

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
./build/macos-debug/GCS_player.app/Contents/MacOS/GCS_player
```

Windows:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
.\build\windows-debug\Debug\GCS_player.exe
```

## Overrides

- Force fresh managed Qt: `-DGCS_QT_FORCE_DOWNLOAD=ON`
- Use a specific Qt SDK: `-DGCS_ALLOW_EXTERNAL_QT=ON -DGCS_EXTERNAL_QT_ROOT=<path>`
- Use a specific GStreamer SDK: `-DGCS_ALLOW_EXTERNAL_GSTREAMER=ON -DGCS_EXTERNAL_GSTREAMER_ROOT=<path>`

## License

The GCS_player source code in this repository is licensed under the
[MIT License](LICENSE) (© 2026 Andrii Syzko).

The project links against and (in binary releases) ships third-party components
that keep their own licenses:

- **Qt 6** — LGPL-3.0 (or commercial), used via dynamic linking.
- **GStreamer** — core and base plugins under LGPL-2.1; some optional plugins
  are GPL (e.g. `x264enc`, parts of `gst-libav`).

MIT covers only this repository's own code. These dependencies are **not**
committed here (CMake bootstrap downloads them). When distributing **prebuilt
binaries** that bundle GStreamer plugins, the licenses of those plugins
(LGPL/GPL) apply to the combined distribution — check which plugins are bundled
before publishing release binaries.
