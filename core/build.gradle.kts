plugins {
    id("java-library")
}

group = "net.minestom.painter"
version = "1.0-SNAPSHOT"

repositories {
    mavenCentral()
}

java {
    sourceCompatibility = JavaVersion.VERSION_25
    targetCompatibility = JavaVersion.VERSION_25
}

val engineDir = file("../engine")
val generatedSourcesDir = layout.buildDirectory.dir("generated/sources/jextract/java").get().asFile
val nativeLibDir = layout.buildDirectory.dir("native").get().asFile

sourceSets {
    main {
        java {
            srcDir(generatedSourcesDir)
        }
        resources {
            srcDir(nativeLibDir)
        }
    }
}

tasks.register<Exec>("compileNativeLib") {
    group = "build"
    description = "Compile C code into a shared library"
    
    inputs.files(
        file("$engineDir/painter.c"),
        file("$engineDir/painter.h"),
        file("$engineDir/tokenizer.c"),
        file("$engineDir/tokenizer.h")
    )
    
    val libName = when {
        System.getProperty("os.name").lowercase().contains("mac") -> "libpainter.dylib"
        System.getProperty("os.name").lowercase().contains("windows") -> "painter.dll"
        else -> "libpainter.so"
    }
    
    val outputLib = file("$nativeLibDir/$libName")
    outputs.file(outputLib)
    
    doFirst {
        nativeLibDir.mkdirs()
    }
    
    commandLine(
        "clang",
        "-shared",
        "-fPIC",
        "-O2",
        "-I$engineDir",
        "-o", outputLib.absolutePath,
        file("$engineDir/painter.c").absolutePath,
        file("$engineDir/tokenizer.c").absolutePath
    )
}

tasks.register<Exec>("generateJextract") {
    group = "build"
    description = "Generate Java bindings from C headers using jextract"
    
    inputs.files(
        file("$engineDir/painter.h"),
        file("$engineDir/tokenizer.h")
    )
    outputs.dir(generatedSourcesDir)
    
    doFirst {
        generatedSourcesDir.deleteRecursively()
        generatedSourcesDir.mkdirs()
    }
    
    // Find jextract in PATH
    val jextractPath = System.getenv("PATH")?.split(":")
        ?.map { file("$it/jextract") }
        ?.firstOrNull { it.exists() && it.canExecute() }
        ?.absolutePath
        ?: "jextract"
    
    commandLine(
        jextractPath,
        "--output", generatedSourcesDir.absolutePath,
        "--target-package", "net.minestom.painter.generated",
        "--header-class-name", "PainterNative",
        "-I", engineDir.absolutePath,
        file("$engineDir/painter.h").absolutePath
    )
}

tasks.named("compileJava") {
    dependsOn("generateJextract", "compileNativeLib")
}

tasks.named("processResources") {
    dependsOn("compileNativeLib")
}

tasks.register("cleanGenerated") {
    group = "build"
    description = "Clean generated jextract sources"
    doLast {
        generatedSourcesDir.deleteRecursively()
    }
}

tasks.named("clean") {
    dependsOn("cleanGenerated")
}

// Ensure FFM preview features are enabled
tasks.withType<JavaCompile> {
    options.compilerArgs.addAll(listOf(
        "--enable-preview",
        "-Xlint:preview"
    ))
}

tasks.withType<Test> {
    jvmArgs("--enable-preview")
    useJUnitPlatform()
    
    // Set working directory to project root so library path works
    workingDir = rootProject.projectDir
    
    // Ensure test resources include the native library
    dependsOn("compileNativeLib")
    
    testLogging {
        events("passed", "skipped", "failed")
        showStandardStreams = false
    }
}

tasks.withType<JavaExec> {
    jvmArgs("--enable-preview")
}

dependencies {
    testImplementation(platform("org.junit:junit-bom:5.10.0"))
    testImplementation("org.junit.jupiter:junit-jupiter")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher")
}
