# Sandbox — archived Unity prototype

This folder contains an archived Unity/C# experiment. It is not the active game implementation. Development continues in the native C++ project under CppProject.

## Requirements

- Unity 2022.3 LTS
- Android Build Support (SDK, NDK and OpenJDK) for Android builds

## First launch

1. Open this folder in Unity Hub.
2. Wait for scripts to compile.
3. Select **Sandbox → Setup Project and Android** from the Unity menu.
4. Open `Assets/Scenes/Sandbox.unity` and press Play.

The setup command creates the initial scene, adds it to Build Settings and configures landscape Android player settings. The sandbox runtime currently creates its arena and starter ragdoll procedurally.

## Current port scope

- Runtime-generated 2D arena with solid boundaries.
- Ragdoll character built from Rigidbody2D bodies and HingeJoint2D joints.
- Grab, Freeze, Clone and Delete tools.
- Spawn panel for boxes, balls and melon ragdolls.
- Simulation speed controls (0.25x, 0.5x, 1x, 2x).
- Touch and mouse input through Unity's legacy input API.
- Android landscape configuration.

## Archive status

The C++ rewrite in CppProject is the active implementation. This prototype is kept only for historical reference. No APK is built by this folder.
