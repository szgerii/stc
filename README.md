# Shader Transpiler Core

_Shader Transpiler Core_ (STC) is a transpiler that aims to translate code snippets coming from CPU-side imperative/general purpose languages to shader languages. The main purpose is to be able to provide an automatic interface through which the end user doesn't even have to be aware that their code is being transpiled and run on the GPU. This means that instead of adding a bunch of decorators/attributes to the source language and requiring the end user to think in terms of coding for the GPU, the code is mostly transpiled as it would behave in the source context. So the aim is to allow "trivial" GPU-side parallelization of arbitrary CPU-side user code, as long as it doesn't rely on CPU or source language features that would make this impossible.

The initial aim is to support Julia to GLSL transpilation, for the optimization of interactive tessellation of parametric geometries in the [Juliagebra](https://github.com/Csabix/Juliagebra) project. Hopefully, in the future, other source and target languages will be supported as well (e.g. HLSL or SPIR-V). The above use case demonstrates the previously mentioned "hidden transpilation to GPU" flow's usefulness: Juliagebra aims to target not just CS people, but also those who might not be too familiar with deeper programming or computer architecture concepts, such as the quirks of GPU programming. However, the parallelization of tessellating parametric curves and surfaces (which has to be performed in an interactive manner), would be a noticable speed up during the interactive updates of the library. The problem is that the actual code that has to be evaluated/sampled comes from the end user. Using this transpiler, the same parametric functions that the user would normally write (for the CPU) can be turned into OpenGL compute shaders, and their evaluation can be executed in parallel.

A Julia-side API for the transpiler will also be implemented, as a separate Julia Pkg, that will act as a wrapper for interacting with the "unsafe" C++ code through `ccall`.

# Current Progress

Here is a (very) rough overview of the development timeline and its current progress:

- [x] Project setup (CMake, testing, etc.)
- [x] Implement base structures and features, like BumpArena, Shader IR (SIR) type system, SIR AST base, etc.
- [ ] Shader IR (SIR) semantic verification pass
- [x] Julia IR
- [x] Julia -> SIR lowering
- [x] GLSL code generation
- [x] Symbol interning
- [ ] Julia Pkg API
- [ ] Julia type inference (bidirectional type checking)
- [ ] Configuration API
- [ ] SIR-level implicit casting support
- [ ] Multi-file and multi-function support
- [ ] Expand supported Julia and GLSL subsets
- [ ] Tests
- [ ] CI/CD (should be as simple as auto running pre-existing CMake targets on GitHub)
- [ ] Wiki?

Note that these aren't necessarily in "chronological" order, and the points differ vastly in difficulty and time needed to implement them.

# Build System

The project uses CMake for the build and development pipeline. It pulls together a couple of different tools and build requirements, which are listed below. Entries marked _(optional)_ will not cause the config/build process to fail if they are not available on the development system, but they will be automatically enabled when accessible through PATH.

The standard build flow is the following:

```bash
cmake -B build
cmake --build build
```

See [Compilation output](#compilation-output) for what this actually produces.

## Note for development using Visual Studio

### Approach #1

The recommended way to use Visual Studio for development is to simply not, whenever possible. VS doesn't naturally support a lot of tools (e.g. clang-tidy, Catch2) that other environments can detect and use without extra setup.

### Approach #2

If [Approach #1](#approach-1) is not applicable, the second-best way to use Visual Studio is to open the root directory directly, rather than the CMake generated solution and project files. VS will still integrate with CMake, and will use it for configuring and building the project. The reasoning for this approach is that this allows the use of tools like clang-tidy, which Visual Studio (and MSVC in general) mostly ignores otherwise, when ran directly on the generated files.

There are a couple of build configurations provided (see _CMakeSettings.json_ for details). The main difference is the generator they use (Ninja or VS, where Ninja is **highly** recommended over VS), and whether Debug or Release config is used for building. All configs are x64-based, though x86 can be set up later, if needed. All configs also use MSVC, though, again, this can be extended in the future to include clang, gcc, etc. (however, with other compilers, any environment other than VS probably offers a better dev experience, see [Approach #1](#approach-1)).

VS support is not perfect, but it should be a viable option for development. As I personally don't mainly use VS, I did not want to dedicate any more time to implementing every single build step two times (once for clang/gcc, and once for MSVC). One imperfection is that with Ninja as a generator, clang-tidy correctly respects the warnings as errors option during building, whereas with the VS generators, it does not. Another nuisance is that VS tends to miss and/or misrecognize the more dynamic parts of the build process (e.g. addition/removal of sandbox targets), this is usually solved by deleting the CMake cache and reconfiguring. If that doesn't fix the issue, restarting VS might help. If that still doesn't fix the issue, it might be time to reconsider [Approach #1](#approach-1).

## libjulia linking

The CMake configuration uses the julia executable (from PATH) to locate the libjulia shared library, and its header files. It adds these as include and link dependencies to the produced library automatically. This should work regardless of development platform, although it has only been tested in Windows and Linux environments. Note that this means building the transpiler requires an installed julia executable. In the future there might be a Julia-independent option to build the transpiler itself, but as Julia is currently the only supported source language, there is no use for this right now.

This unfortunately means that a lot of static/runtime analysis tools start reporting violations from libjulia and/or llvm. One suppression file is provided for LSan in `misc/lsan.supp`, and one for Valgrind in `misc/valgrind.supp`. Valgrind still detects a couple of "still reachable"s, but their frame info is so generic that I couldn't find a way to suppress them with a general pattern.

The script `scripts/init_debug_env.sh` can be used to locate (or specify) the LSan and Valgrind suppression files and add them to the current environment for use. For this, the script must be run sourced.

## Compilation flags

On MSVC, the following flag adjustments are made by CMake:

- enabled `/W4`: for strict, "linter-like" compile-time warning behavior
- enabled `/fsanitize=address`: Address Sanitization (ASan) for strict memory bug detection
- disabled `/RTC1`: incompatible with ASan, but ASan + clang-tidy should mostly cover bugs detected by RTC1
- changes `/ZI` to `/Zi`: edit-and-continue is incompatible with ASan, but because of the runtime/debugging nature of a transpiler, it was deemed a fair compromise (might be tweaked later if I change my mind about edit-and-continue helpfulness in the future)
- sets `/INCREMENTAL:NO`: incremental linking is, again, incompatible with ASan, but there's fairly "little" linking happening in the project

On other compilers the following flags are enabled:

- `-Wall -Wextra -Wpedantic -Wconversion`: for strict compiler warnings, like on MSVC
- `-fsanitize=address`: address sanitization, like in MSVC (with fewer incompatibilities)
- `-fno-omit-frame-pointer`: should lead to a more consistently nice looking output from address sanitization
- `-fvisibility=hidden` (for non-Debug builds): strips external access from final library for non-exported symbols (forces gcc/clang to mimic MSVC behavior). this is only used for non-Debug builds, otherwise sandboxes couldn't test/use non-exported symbols.

## Compilation output

A build, unless configured to operate otherwise, will output the produced library files to `lib/`. This uses headers from `include/` and source files from `src/` to build the final library. It also has `libjulia` linked automatically.

### Sandbox

Any source files named `sb_*.cpp` in the `sandbox/` directory will be built as a separate executable (and have a unique target). These all have the built library linked automatically, and its produced output copied next to the executables.

Through sandbox executables, it's easy to produce example, demo, manual test or playground code that interacts with the library from an "external" perspective. Most files in `sandbox/` will thus probably contain development-related, temporary code that is useful while writing the transpiler.

## [ccache](https://ccache.dev/) (optional)

ccache is included in the build process to speed up compilation time through compiler caching.

## [clang-format](https://clang.llvm.org/docs/ClangFormat.html) (optional)

clang-format is used for consistent code style. The CMake config provides the check_format and fix_format targets for retrieving a list of code style violations and automatically fixing them, respectively. The rules set up in the `.clang-format` file are mostly out of personal preference, that is, what I find to be _"readable"_ and _"nice-looking"_ for C++ code.

A sample pre-commit hook script is also included in the `scripts/` directory, which verifies that the code contains no style violations. The script performs the validation on the current state of the directory, so any changes that are present, but are not staged for the current commit should be stashed for the duration before committing.

To use the pre-commit hook locally:

```bash
cp scripts/pre-commit .git/hooks/
```

## [clang-tidy](https://clang.llvm.org/extra/clang-tidy/) (optional)

clang-tidy is used for static analysis. A strict set of rules is defined in the `.clang-tidy` file, to keep the code modern, readable and safe.

## [Catch2](https://github.com/catchorg/Catch2) (optional)

Catch2 is used for unit tests. It is acquired through FetchContent and built when building the library itself. Unit tests can be run through the `tests` target.

By default, Catch2 and testing targets are not included in builds. To use them, add the `-DBUILD_TESTING=ON` switch when configuring through CMake.

## [Doxygen](https://doxygen.nl) (with [Graphviz](https://graphviz.org)) (optional)

Doxygen is used for automatic documentation generation, both in HTML and LaTeX formats. If `pdflatex` is found, a PDF version of the LaTeX docs will also be generated as a post-build step. Documentation can be generated through the `docs` target.

The documentation parameters are, and will always be, subject to change as the project grows and it becomes clearer what features are actually useful versus just pointless fluff.

## Dependency build caching

Dependencies acquired from FetchContent are cached in `_deps_<PLATFORM>` directories when they are first built. This ensures that later (re)builds don't need to perform an entire build of external dependencies every time. The reason for separate directories based on the development platform is to be able to create and maintain a build directory for both, for example, Windows and WSL during development. See the example below.

Currently, this only affects Catch2 building.

Take a regular Windows/WSL setup, both of their command lines open in the same directory (a clone of this repo). The following command sequences, executed in any interleaved order, will never conflict, and both of them will compile Catch2 exactly one time, once for Windows and once for Linux. Deleting the build directories and performing another build doesn't invalidate external dependencies.

On Windows:

```powershell
cmake -B build_win
cmake --build build_win
rm -R -Force build_win
cmake -B build_win
cmake --build build_win
```

On WSL (or in a dual-booted linux system):

```bash
cmake -B build_linux
cmake --build build_linux
rm -rf build_linux
cmake -B build_linux
cmake --build build_linux
```
