# Third-party notices

## SDL

The project uses SDL 3.4.20 from https://github.com/libsdl-org/SDL, pinned to the \`release-3.4.20\` tag. The Android Java activity integration under \`android-project/app/src/main/java/org/libsdl/app\` is copied from that upstream release.

SDL is distributed under the zlib license. Copyright (C) 1997-2026 Sam Lantinga. The complete license text is available in the SDL repository: https://github.com/libsdl-org/SDL/blob/release-3.4.20/LICENSE.txt.

## Box2D

The project uses Box2D 3.2.0 from https://github.com/erincatto/box2d, pinned to the \`v3.2.0\` tag. Box2D is distributed under the MIT License, Copyright (c) 2022 Erin Catto. The complete license text is available at https://github.com/erincatto/box2d/blob/v3.2.0/LICENSE.

Both dependencies are fetched from their upstream repositories by CMake FetchContent for native builds. Their source trees are not vendored into this repository, except for the SDL Android Java integration required by SDLActivity.
