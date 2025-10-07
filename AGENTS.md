# Painter Agent Playbook

## Lightning Overview
- Painter is a Minecraft world generator DSL with a native C17 engine (`engine/`) wrapped by Java (`core/`) via Project Panama, plus a future Minestom module.
- Primary parser + executor live in `engine/painter.c`/`.h`; runtime helpers in `painter_eval.c`, macros/functions/occurrences under `engine/builtin_*`.
- Java bindings are generated with `jextract` and shipped alongside the compiled shared library that Gradle builds through `:core:compileJava`.
- Tests are JUnit 5 (`core/src/test/java/net/minestom/painter/`); `ParserTest.java` covers parser/runtime behavior and must grow with every engine change.

## Module Map for Agents
- `engine/`
  - `tokenizer.c`/`.h`: lexes Painter DSL (numbers, identifiers, delimiters).
  - `painter.c`/`.h`: AST definitions, parser, section generation, macro registry.
  - `painter_eval.c`/`.h`: interpreter and section/palette emission helpers.
  - `builtin_functions.c`, `builtin_macros.c`, `builtin_occurrences.c` (+ headers): runtime primitives; extend by registering new entries in the respective tables.
- `core/`
  - `build.gradle.kts`: drives native build (`clang`) into `core/build/native/` and runs `jextract` into `core/build/generated/sources/jextract/java`.
  - Generated bindings expose `PainterParser`/`PainterContext` wrappers used by tests and downstream JVM consumers.
  - Tests reside under `core/src/test/java/net/minestom/painter/`; see `ParserTest.java` for end-to-end coverage patterns using the generated bindings.
- `minestom/`
  - Currently a placeholder Gradle module ready for future Minestom integration; no source files yet.
- `worlds/`
  - Sample `.paint` programs (e.g., `tour.paint`, `plain_forest.paint`) that exercise DSL features; handy for manual parser checks or new regression cases.

## Build & Test Flow
- Core commands (run from repo root):
  - `./gradlew :core:compileJava` → compiles the C engine with `clang`, generates FFM bindings, and publishes the shared library to test resources.
  - `./gradlew :core:test` → runs JUnit suite with preview features and native access enabled.
- When touching C headers or sources you **must**:
  1. Add or extend coverage in `core/src/test/java/net/minestom/painter/ParserTest.java` to prove the new behavior.
  2. Execute `./gradlew :core:test` and ensure it passes.
  3. Regenerate bindings by re-running `:core:compileJava` if signatures changed.
- For Java-only updates, rerun `:core:test`; Minestom module currently has no tests.

## Practical Tips
- Prefer `rg` for repo search; Gradle already handles include paths (`-I engine`), so no manual flag juggling.
- Use `PainterParser.freeProgram`/`PainterParser.freeContext` in tests to mirror existing memory management discipline.
- Pair every new AST allocation in C with cleanup in `instruction_free`/`expression_free` in `painter.c`.
- Keep new DSL examples in `worlds/` and update `README.md` only if user-facing behavior changes.
- The repo ships with fast-noise headers (`engine/FastNoiseLite.h`); include it directly when adding new noise-powered features.
