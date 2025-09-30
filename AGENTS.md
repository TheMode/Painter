# Painter Project Guide for LLM Agents

## TL;DR
- Domain-specific language (DSL) for declaratively generating Minecraft worlds.
- Hybrid codebase: native C parser/executor (`engine/`) wrapped by Java (FFM/JExtract) in `core/`.
- Build pipeline compiles the C engine with `clang`, generates Java bindings via `jextract`, and packages a shared library for tests/consumers.
- Primary entrypoints: `engine/painter.c` (parser + interpreter) and generated Java bindings consumed by future JVM modules (e.g., `minestom`).
- Tests live under `core/src/test`; use Gradle (`./gradlew :core:test`).

## Repo Layout
- `engine/`: Hand-written C17 code implementing the tokenizer, parser, executor, macro system, and section/palette logic.
  - `painter.c`/`painter.h`: AST definitions, parser, section generation routines, macro registry wiring.
  - `tokenizer.c`/`tokenizer.h`: Lexer for the Painter DSL (numbers, identifiers, punctuation, brackets, etc.).
  - `builtin_macros.c`/`.h`: Built-in macro implementations, currently `#sphere` (filled 3D spheres); extendable via the registry API.
- `core/`: JVM module that wraps the native engine using Project Panama (FFM) bindings generated via `jextract`.
  - `build.gradle.kts`: Drives native compilation (`clang`) and `jextract` generation; enables Java 25 preview features.
  - `src/test/README.md`: Notes on JUnit tests for the native bindings; actual test sources go under `src/test/java/net/minestom/painter/`.
- `minestom/`: Placeholder module for Minestom integration (currently empty sources).
- `worlds/`: Sample `.paint` scripts demonstrating the DSL, e.g. `tour.paint`, `test_sphere.paint`.
- `README.md`: User-facing overview of features and macro usage. References `MACRO_IMPLEMENTATION.md`; that file is not in the repo, so confirm requirements before relying on it.

## Language Highlights (Painter DSL)
- Block placement: `[x z] block_name[properties]` or `[x y z]` (Y defaults to `0`).
- Variables & expressions: standard arithmetic, identifier references, coordinate literals.
- Loops: `for i in start..end { ... }`.
- Occurrences: `@type(args) [condition] { ... }` for noise/structured repeats (see `worlds/tour.paint`).
- Macros: `#macro .arg=value`; built-ins live in `engine/builtin_macros.c` and populate via `register_builtin_macros`.

## Build & Toolchain
- Requirements: `clang` for native build, `jextract` on PATH for binding generation, Java 25 toolchain.
- Key commands:
  - `./gradlew :core:compileJava` (triggers native build + jextract).
  - `./gradlew :core:test` (runs JUnit tests; sets working directory to repo root so the shared library resolves).
  - `./gradlew clean` >> `cleanGenerated` to remove jextract output.
- Native output written to `core/build/native/` (`libpainter.dylib` on macOS).
- Generated Java sources land under `core/build/generated/sources/jextract/java` and are added to the `main` source set automatically.

## Extending the Engine
- Parser additions: edit `engine/painter.c` and adjust enums/structs in `engine/painter.h`.
- Token changes: update `engine/tokenizer.*` to recognize new symbols/keywords.
- New macros: implement in `engine/builtin_macros.c`, declare in `.h`, and append to `BUILTIN_MACROS` so they auto-register.
- Memory management: every AST node allocation should be paired with cleanup in `instruction_free`, `expression_free`, etc.
- After native changes, rerun `./gradlew :core:compileJava` or `:core:test` to rebuild the shared library and regenerate bindings.

## Working Tips for Agents
- Prefer `rg` for code search (configured in this environment).
- When touching native code, ensure the shared library still builds (run `./gradlew :core:compileJava`).
- For Java-side tests, follow the cleanup pattern noted in `core/src/test/README.md` (`PainterParser.freeProgram`).
- If adding new DSL samples, keep them in `worlds/` and update `README.md` accordingly.
- `minestom/` currently lacks sources; coordinate with maintainers before assuming runtime integration.

