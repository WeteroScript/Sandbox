# Sandbox — native C++ rewrite

The active rewrite is developed directly in the repository's `main` branch.

## Technology

- C++20 for application and game logic.
- SDL 3.4.20 for rendering, desktop input, touch input, and Android platform integration.
- Box2D 3.2.0 for rigid-body physics and ragdoll joints.
- CMake FetchContent with pinned upstream release tags.

Third-party C++ source is fetched during CMake configuration. The Android project includes the upstream SDL Java activity integration.

## Implemented foundation

- Fixed-step physics at 60 Hz with a bounded frame accumulator.
- Arena floor and side walls.
- Dynamic boxes and balls.
- Six-part ragdoll connected with revolute joints.
- GRAB, FREEZE, CLONE, and DELETE tools.
- Group freeze, clone, and delete for ragdolls.
- Spawn panel for boxes, balls, and Melon ragdolls.
- Spawn quantity from 1 to 10 and tap-to-place workflow.
- Mouse and touch interaction, including two-finger camera pan and pinch zoom.
- Versioned scene save/load with body transforms, velocities, dimensions, colors, freeze state, and ragdoll joint links.
- Automatic restore of the last saved scene on startup; F5 saves and F9 loads.
- Mouse-wheel zoom, pause, and simulation speed controls.
- Physics grid and FPS/object counters.
- Desktop CMake target and Android Gradle/CMake project for arm64-v8a.

## Desktop build

Requirements: CMake 3.22+, a C/C++20 compiler, and Git.

```sh
cmake -S CppProject -B CppProject/build -DCMAKE_BUILD_TYPE=Release
cmake --build CppProject/build --config Release
ctest --test-dir CppProject/build --output-on-failure
```

Run the `sandbox` executable from `CppProject/build`.

## Android build

Open `CppProject/android-project` in Android Studio. The project uses the SDL 3.4.20 Android activity, compiles the native `main` shared library through CMake, and targets arm64-v8a on Android API 23+.

From that directory, the debug package command is:

```sh
./gradlew assembleDebug
```

The output is under `app/build/outputs/apk/debug/`. The GitHub Actions APK workflow is manual-only and builds this C++ project. No APK has been built or device-tested in this development pass.

## Controls

- Mouse or touch: select, drag, and place objects.
- Middle mouse drag or one-finger drag on empty space: pan the camera.
- Two-finger gesture: pan and pinch-zoom the camera.
- Mouse wheel: zoom.
- `1`: GRAB.
- `2`: FREEZE.
- `3`: CLONE.
- `4`: DELETE.
- `Space` or `Escape`: pause.
- `Tab`: toggle spawn panel.
- `F5`: save scene.
- `F9`: load scene.
- `-` / `+`: change simulation speed.

The original Rust project remains in the repository as a reference. The previous Unity prototype is archived; C++ is now the active rewrite.

## Physics regression test

CTest includes a smoke test for ragdoll joints, grouped freeze, scene persistence, clone, and delete.
