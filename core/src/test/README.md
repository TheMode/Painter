# Painter Core - Test Suite

This directory contains the test suite for the Painter C engine and Java bindings.

## Running Tests

```bash
# Run all tests
./gradlew :core:test

# Run tests with more output
./gradlew :core:test --info

# Run a specific test class
./gradlew :core:test --tests ParserTest
./gradlew :core:test --tests ForLoopTest
```

## Writing New Tests

1. Create a new test class in `src/test/java/net/minestom/painter/`
2. Use JUnit 5 annotations:
   - `@DisplayName` for readable test names
   - `@Test` for test methods
3. Always free program memory after use:
   ```java
   MemorySegment program = PainterParser.parseString(code);
   try {
       // test code here
   } finally {
       PainterParser.freeProgram(program);
   }
   ```
