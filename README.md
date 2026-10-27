# CRAT3R — Compute, Rasterization and Tracing for 3D Rendering

This is the exercise framework for the CRAT3R master's project course. It is written in C++ and GLSL and uses Vulkan.

## Requirements

- A GPU and driver with Vulkan 1.3 support
- The [Vulkan SDK](https://vulkan.lunarg.com/) 1.3 or newer, which provides `glslc` for compiling shaders
- CMake 3.20 or newer
- A C++20 compiler (MSVC, GCC, or Clang)

## Directory Structure

```
├── src/aXX/       # C++ code for assignment XX
├── shaders/aXX/   # GLSL shaders for assignment XX
├── assets/        # Models and textures
└── deps/          # Source code of third-party dependencies
```

Each assignment has its own folder, numbered `a01`, `a02`, and so on. Most of your work goes into `src/aXX/main.cpp` and the matching folder in `shaders/aXX/`.

## Build

The project uses CMake. From the repository root, run:

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

To build only one assignment, pass its target name:

```bash
cmake --build build --target a01
```

The executables are written to `build/`. Run them from the repository root so that relative paths to `assets/` and `shaders/` resolve correctly.

## Shader Compilation

All shaders in `shaders/` and its subdirectories are compiled to SPIR-V automatically during the build. Running `cmake --build build` recompiles any shaders that have changed since the last build. Shaders are not hot-reloaded, so you need to rebuild and restart the program to see your changes. If you'd really like to have hot reloading, you're welcome to implement it yourself :)

## Assets and Licenses

Some of the models and textures in `assets/` are made by third parties and are used under the following licenses.

| Asset                                                                                                   | Author                                       | License                                                       |
| ------------------------------------------------------------------------------------------------------- | -------------------------------------------- | ------------------------------------------------------------- |
| [Shiba](https://sketchfab.com/3d-models/shiba-faef9fe5ace445e7b2989d1c1ece361c)                         | [zixisun02](https://sketchfab.com/zixisun51) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)     |
| [Littlest Tokyo](https://sketchfab.com/3d-models/littlest-tokyo-94b24a60dc1b48248de50bf087c0f042)       | [glenatron](https://sketchfab.com/glenatron) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)     |
| [Hidden Alley](https://blog.polyhaven.com/hidden-alley/)                                                | Poly Haven and community contributors        | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |
| [Kloofendal 48d Partly Cloudy (Pure Sky)](https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky) | Poly Haven                                   | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |
