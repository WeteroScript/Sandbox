# Sandbox — Unity rewrite

This folder contains the new Unity/C# implementation. The original Rust/Macroquad project remains in the repository and is not modified by this port.

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

## Migration strategy

The Rust implementation is retained as a reference until the Unity port has feature parity. Next milestones are object quantity/placement flow, persistent scene serialization, map presets, water/lava effects, mechanisms, and performance profiling on Android.

No APK is built by this setup step.
