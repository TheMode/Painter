package net.minestom.painter;

import net.minestom.painter.generated.PainterNative;
import net.minestom.painter.generated.Parser;
import net.minestom.painter.generated.Program;

import java.lang.foreign.*;
import java.nio.file.Files;
import java.nio.file.Path;

/**
 * Example wrapper class for the Painter parser using FFM API.
 */
public class PainterParser {
    
    /**
     * Parse a .paint file and return the program structure.
     * 
     * @param paintFilePath Path to the .paint file
     * @return Memory segment containing the parsed Program structure
     * @throws Exception if parsing fails
     */
    public static MemorySegment parseFile(Path paintFilePath) throws Exception {
        String content = Files.readString(paintFilePath);
        return parseString(content);
    }
    
    /**
     * Parse a .paint string and return the program structure.
     * 
     * @param paintCode The .paint code to parse
     * @return Memory segment containing the parsed Program structure
     */
    public static MemorySegment parseString(String paintCode) {
        try (Arena arena = Arena.ofConfined()) {
            // Allocate memory for the input string
            MemorySegment inputSegment = arena.allocateFrom(paintCode);
            
            // Allocate parser structure
            MemorySegment parser = arena.allocate(Parser.layout());
            
            // Initialize the parser
            PainterNative.parser_init(parser, inputSegment);
            
            // Parse the program
            MemorySegment program = PainterNative.parse_program(parser);
            
            // Check for errors
            boolean hasError = Parser.has_error(parser);
            if (hasError) {
                MemorySegment errorMsg = Parser.error_message(parser);
                String error = errorMsg.getString(0);
                throw new RuntimeException("Parse error: " + error);
            }
            
            return program;
        }
    }
    
    /**
     * Free a program structure.
     * 
     * @param program The program memory segment to free
     */
    public static void freeProgram(MemorySegment program) {
        if (program != null && program.address() != 0) {
            PainterNative.program_free(program);
        }
    }
    
    /**
     * Get the number of instructions in a program.
     * 
     * @param program The program memory segment
     * @return Number of instructions
     */
    public static int getInstructionCount(MemorySegment program) {
        return Program.instruction_count(program);
    }
    
    /**
     * Example usage
     */
    public static void main(String[] args) {
        String sampleCode = """
            // Simple example
            [0 0] air
            x = 5
            [x 0] stone
            
            for i in 0..3 {
              [i 1] dirt
            }
            """;
        
        try {
            MemorySegment program = parseString(sampleCode);
            int count = getInstructionCount(program);
            System.out.println("Parsed " + count + " instructions successfully!");
            freeProgram(program);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}
