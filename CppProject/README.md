# Sandbox — native C++ rewrite

The active rewrite is developed directly in the repository's \`main\` branch.

## Technology

- C++20 for application and game logic.
- SDL 3.4.20 for rendering, desktop input, touch input, and platform integration.
- Box2D 3.2.0 for rigid-body physics and ragdoll joints.
- CMake FetchContent with pinned upstream release tags.

Third-party source is downloaded during CMake configuration rather than copied into this repository.

## Implemented foundation

- Fixed-step physics at 60 Hz with a bounded frame accumulator.
- Arena floor and side walls.
- Dynamic boxes and balls.
- Six-part ragdoll connected with revolute joints.
- GRAB, FREEZE, CLONE, and DELETE tools.
- Group freeze, clone, and delete for ragdolls.
- Spawn panel for boxes, balls, and Melon ragdolls.
- Spawn quantity from 1 to 10 and tap-to-place workflow.
- Mouse and single-touch interaction.
- Camera pan, mouse-wheel zoom, pause, and simulation speed controls.
- Physics grid and FPS/object counters.
- Desktop CMake target and Android native shared-library target.

## Desktop build

Requirements: CMake 3.22+, a C/C++20 compiler, and Git.

\`\`\`sh
cmake -S CppProject -B CppProject/build -DCMAKE_BUILD_TYPE=Release
cmake --build CppProject/build --config Release
\`\`\`

Run the \`sandbox\` executable from \`CppProject/build\`.

## Android

The CMake project defines an Android shared-library target named \`main\`. SDL3 handles the native application entry point and Android input layer. Android packaging and the Gradle wrapper are being added as a separate migration step.

No Android APK has been built or device-tested as part of this commit.

## Controls

- Mouse or touch: select, drag, and place objects.
- Middle mouse drag or touch on empty space: pan the camera.
- Mouse wheel: zoom.
- \`1\`: GRAB.
- \`2\`: FREEZE.
- \`3\`: CLONE.
- \`4\`: DELETE.
- \`Space\` or \`Escape\`: pause.
- \`Tab\`: toggle spawn panel.
- \`-\` / \`+\`: change simulation speed.

The original Rust project remains in the repository as a reference. The previous Unity prototype is also retained for reference; C++ is now the active rewrite.

The existing Android APK workflow is manual-only and is not triggered by ordinary pushes.
