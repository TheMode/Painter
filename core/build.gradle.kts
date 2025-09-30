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

    // collect sources and headers from engineDir
    val cSources = fileTree(engineDir) { include("**/*.c") }
    val headers  = fileTree(engineDir) { include("**/*.h") }

    inputs.files(cSources, headers)

    val libName = when {
        System.getProperty("os.name").lowercase().contains("mac") -> "libpainter.dylib"
        System.getProperty("os.name").lowercase().contains("windows") -> "painter.dll"
        else -> "libpainter.so"
    }

    val outputLib = file("$nativeLibDir/$libName")
    outputs.file(outputLib)

    doFirst {
        nativeLibDir.mkdirs()
        // build command: clang -shared -fPIC -O2 -I<engineDir> -o <out> <all .c files>
        commandLine(
            "clang",
            "-shared",
            "-fPIC",
            "-O2",
            "-I${engineDir.absolutePath}",
            "-o", outputLib.absolutePath,
            *cSources.files.map { it.absolutePath }.toTypedArray()
        )
    }
}

tasks.register<Exec>("generateJextract") {
    group = "build"
    description = "Generate Java bindings from C headers using jextract"

    // collect all .h headers in engineDir
    inputs.files(fileTree(engineDir) { include("**/*.h") })
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

    // pick one main header to pass to jextract
    val mainHeader = file("$engineDir/painter.h")
    commandLine(
        jextractPath,
        "--output", generatedSourcesDir.absolutePath,
        "--target-package", "net.minestom.painter.generated",
        "--header-class-name", "PainterNative",
        "-I", engineDir.absolutePath,
        mainHeader.absolutePath
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
