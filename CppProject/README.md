# Sandbox — Android mobile game

The active game is a touch-first Android application. The project is developed in the repository's `main` branch.

## Technology

- C++20 for game and simulation logic.
- SDL 3.4.20 for Android rendering and touch input.
- Box2D 3.2.0 for rigid-body physics, ragdoll joints, and fluid forces.
- CMake and the Android Gradle project for the native arm64-v8a application.

## Game features

- Three maps: Test Range, City, and Laboratory.
- Dynamic boxes, balls, barrels, platforms, Melon and Robot ragdolls.
- Water buoyancy, current-relative drag, angular damping and surface waves.
- Lava updrafts and hot currents; acid exposure gradually dissolves immersed props.
- Filled water, lava and acid buckets in the MISC catalogue. Tap a placed bucket to empty it into a finite puddle; nearby puddles of the same liquid join together.
- Rod, rope, spring, piston, button and toggle mechanisms.
- GRAB, FREEZE, CLONE, DELETE and ROTATE touch tools.
- Three catalogue sections: PROPS, MECH and RAZNOE (Разное).
- Large touch targets, tap-to-place, one-finger object manipulation and camera panning, plus two-finger pan and pinch zoom.
- Save/load buttons and bounded 20-action Undo/Redo history.
- Scene saves preserve map, body state, buckets, liquid puddles, corrosion exposure, ragdoll joints and mechanisms.

## Android project

Open `android-project` in Android Studio. The app is configured for landscape orientation and arm64-v8a, with Android API 23+.

To build a debug package from the Android project directory:

```sh
./gradlew assembleDebug
```

The output is under `app/build/outputs/apk/debug/`.

The APK workflow is manual-only. No APK is built automatically by the source-check workflow.

## Touch controls

- Tap a catalogue item, then tap the scene to place it.
- Tap a placed water, lava or acid bucket to pour its contents.
- Drag a body to move it.
- Drag empty space with one finger to pan the camera.
- Use two fingers to pan and pinch-zoom.
- Use the large top buttons to select GRAB, FREEZE, CLONE, DELETE or ROTATE.
- Tap MAP to cycle the maps.
- Tap the simulation-speed button to cycle speeds; tap PAUSE/PLAY to control the simulation.
- Use SAVE and LOAD in the catalogue.
- Use UNDO and REDO in the catalogue to step through up to 20 scene edits.
- Tap a toggle or button mechanism in the scene to activate it.

## Regression tests

CTest covers ragdoll construction and persistence, clone/freeze/rotation, malformed scene rejection, in-memory snapshots, exact liquid immersion, bucket pouring and restoration, water drag, acid corrosion, mechanism endpoint deletion and piston-anchor cleanup.
