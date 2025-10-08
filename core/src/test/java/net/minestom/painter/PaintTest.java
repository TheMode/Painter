package net.minestom.painter;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * Meta-annotation that combines @Test with Paint program parsing and lifecycle management.
 * The parsed program is injected as a ProgramContext parameter and automatically freed after the test.
 * 
 * Usage:
 * <pre>
 * {@code
 * @PaintTest("""
 *     [0, 0] stone
 *     """)
 * void testSomething(ProgramContext ctx) {
 *     // Use ctx.program() to access the parsed program
 * }
 * }
 * </pre>
 */
@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
@Test
@ExtendWith(PaintProgramExtension.class)
public @interface PaintTest {
    /**
     * The Paint DSL program to parse before the test runs.
     */
    String value();
}
