Qt is now bootstrapped with `aqtinstall` during CMake configure.

`uv` is preferred. If `uv` is not available, CMake creates a build-local virtual environment and installs `aqtinstall` there instead of touching the global Python environment.

Examples:

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
./build/macos-debug/GCS_player.app/Contents/MacOS/GCS_player
```

`*-gstreamer` presets now require a system GStreamer SDK that is visible through `pkg-config`; the plain presets keep video support optional.
