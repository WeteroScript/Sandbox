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
- Three switchable maps: Test Range, City, and Laboratory.
- Arena floor, side walls, and map-specific static geometry.
- Dynamic boxes, balls, barrels, and platforms.
- Six-part Melon and Robot ragdolls with revolute joints; robot geometry and colors survive clone/save/load.
- Water buoyancy/drag and animated lava-flow forces use shape-accurate overlap with liquid zones; circles use circle/rectangle area integration and rotated boxes use polygon clipping.
- Fluid-overlap calculations use fixed-size geometry buffers to avoid temporary heap allocations during simulation.
- GRAB, FREEZE, CLONE, DELETE, and ROTATE tools; rotating a ragdoll transforms the complete body group around its centroid and preserves its joint graph.
- Group freeze, clone, and delete for ragdolls.
- Rod and rope distance constraints, spring joints, motorized pistons, proximity buttons, and interactive toggles.
- Two-category spawn catalogue for props and mechanisms.
- Spawn catalogue for boxes, balls, barrels, platforms, Melon, Robot, and all six mechanisms.
- Placement feedback when a mechanism cannot find the required nearby movable bodies.
- Spawn quantity from 1 to 10 and tap-to-place workflow.
- Clear-scene action requires a second confirmation tap to prevent accidental deletion.
- Mouse and touch interaction, including two-finger camera pan and pinch zoom.
- Versioned scene save/load with map selection, body transforms, velocities, dimensions, colors, freeze state, ragdoll joint links, and mechanism connections.
- Automatic restore of the last saved scene on startup; F5 saves and F9 loads.
- Mouse-wheel zoom anchored to the pointer position, pause, and simulation speed controls.
- Procedural object materials with per-vertex lighting, beveled edges, wood grain, metal panels, fasteners, surface highlights, and contact shadows.
- Map-specific visual detailing for city facades, laboratory panels, and test-range markings.
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
- `5`: ROTATE selected object or complete ragdoll by 15 degrees.
- `Space` or `Escape`: pause.
- `Tab`: toggle spawn panel.
- `M`: cycle Test Range, City, and Laboratory.
- Tap a toggle or button in the scene to activate it.
- `F5`: save scene.
- `F9`: load scene.
- `-` / `+`: change simulation speed.

The original Rust project remains in the repository as a reference. The previous Unity prototype is archived; C++ is now the active rewrite.

## Physics regression test

CTest covers Melon and Robot joint graphs, character-preserving clone, grouped freeze and rotation, scene persistence, mechanism restoration, partial circle immersion, malformed save rejection, duplicate-constraint rejection, mechanism endpoint deletion, and piston-anchor cleanup.
