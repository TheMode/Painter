package net.minestom.painter;

import org.junit.jupiter.api.extension.*;

import java.lang.foreign.MemorySegment;
import java.lang.reflect.Parameter;

/**
 * JUnit 5 extension that handles the lifecycle of Paint programs in tests.
 * Automatically parses programs before tests and frees them after.
 */
public class PaintProgramExtension implements ParameterResolver, AfterEachCallback {

    private static final ExtensionContext.Namespace NAMESPACE =
            ExtensionContext.Namespace.create(PaintProgramExtension.class);

    private static final String PROGRAM_KEY = "paintProgram";

    @Override
    public boolean supportsParameter(ParameterContext parameterContext, ExtensionContext extensionContext)
            throws ParameterResolutionException {
        Parameter parameter = parameterContext.getParameter();
        return parameter.getType().equals(ProgramContext.class);
    }

    @Override
    public Object resolveParameter(ParameterContext parameterContext, ExtensionContext extensionContext)
            throws ParameterResolutionException {
        
        // Get the @PaintTest annotation from the test method
        PaintTest annotation = extensionContext.getRequiredTestMethod()
                .getAnnotation(PaintTest.class);

        if (annotation == null) {
            throw new ParameterResolutionException(
                    "ProgramContext parameter requires @PaintTest annotation on the test method");
        }

        // Parse the program
        String programSource = annotation.value();
        MemorySegment program;
        try {
            program = PainterParser.parseString(programSource);
        } catch (Exception e) {
            throw new ParameterResolutionException("Failed to parse Paint program", e);
        }

        // Store the program in the extension context for cleanup
        extensionContext.getStore(NAMESPACE).put(PROGRAM_KEY, program);

        // Return the context object
        return new ProgramContext(program);
    }

    @Override
    public void afterEach(ExtensionContext extensionContext) {
        // Free the program after the test
        MemorySegment program = extensionContext.getStore(NAMESPACE)
                .get(PROGRAM_KEY, MemorySegment.class);

        if (program != null) {
            PainterParser.freeProgram(program);
        }
    }
}
