package net.minestom.painter;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import java.lang.foreign.MemorySegment;

import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;

@DisplayName("Painter Parser Tests")
class ParserTest {

    private void assertParseSucceed(String program) {
        MemorySegment programSegment = null;
        try {
            programSegment = PainterParser.parseString(program);
            assertNotNull(programSegment, "Program should parse successfully");
        } finally {
            if (programSegment != null) {
                PainterParser.freeProgram(programSegment);
            }
        }
    }

    private void assertParseFail(String program) {
        assertThrows(RuntimeException.class, () -> {
            PainterParser.parseString(program);
        }, "Should throw exception for invalid syntax");
    }

    @Test
    @DisplayName("Parse simple block placement")
    void testSimpleBlockPlacement() {
        assertParseSucceed("[0 0] air");
    }

    @Test
    @DisplayName("Parse block with properties")
    void testBlockWithProperties() {
        assertParseSucceed("[1 50 0] oak_planks[facing=north,half=top]");
    }

    @Test
    @DisplayName("Parse variable assignment")
    void testVariableAssignment() {
        assertParseSucceed("""
                x = 5
                [x 0] stone
                """);
    }

    @Test
    @DisplayName("Parse arithmetic expressions")
    void testArithmeticExpressions() {
        assertParseSucceed("""
                x = 2
                z = 3
                [x 0 z] dirt
                """);
    }

    @Test
    @DisplayName("Parse function call expressions")
    void testFunctionCalls() {
        assertParseSucceed("""
                [min(4, 2, -8) 0 0] stone
                """);
    }

    @Test
    @DisplayName("Parse nested function calls")
    void testNestedFunctionCalls() {
        assertParseSucceed("""
                value = clamp(5, min(1, 3), max(2, 7))
                [value 0 0] stone
                """);
    }

    @Test
    @DisplayName("Invalid syntax throws error")
    void testInvalidSyntax() {
        assertParseFail("invalid syntax here");
    }

    @Test
    @DisplayName("Simple for loop places blocks correctly")
    void testSimpleForLoop() {
        assertParseSucceed("""
                for i in 0..5 {
                  [i 36 1] stone
                }
                """);
    }

    @Test
    @DisplayName("Nested loops create grid pattern")
    void testNestedLoops() {
        assertParseSucceed("""
                for x in 0..3 {
                  for z in 0..3 {
                    [x 37 z] oak_planks
                  }
                }
                """);
    }

    @Test
    @DisplayName("Loop with arithmetic expressions")
    void testLoopWithArithmetic() {
        assertParseSucceed("""
                for i in 0..5 {
                  [i*2 38 0] diamond_block
                }
                """);
    }

    @Test
    @DisplayName("Loop with variables")
    void testLoopWithVariables() {
        assertParseSucceed("""
                offset = 5
                for i in 0..3 {
                  [i+offset 39 0] gold_block
                }
                """);
    }

    @Test
    @DisplayName("Loop with negative range")
    void testLoopWithNegativeRange() {
        assertParseSucceed("""
                for i in -5..5 {
                  [i 40 0] emerald_block
                }
                """);
    }

    @Test
    @DisplayName("Nested loops with negative ranges")
    void testNestedLoopsWithNegativeRanges() {
        assertParseSucceed("""
                [0 36 0] minecraft:grass_block
                for i in -25..25 {
                  for z in -25..25 {
                    [i 28 z] stone
                  }
                }
                """);
    }

    @Test
    @DisplayName("Parse sphere macro syntax")
    void testParseMacro() {
        assertParseSucceed("#sphere .x=8 .radius=5 .block=stone");
    }

    @Test
    @DisplayName("Parse and execute sphere macro")
    void testCircleMacro() {
        assertParseSucceed("#sphere .x=8 .y=5 .z=8 .radius=5 .block=stone");
    }
}
