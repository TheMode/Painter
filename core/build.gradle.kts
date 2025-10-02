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

val os = System.getProperty("os.name").lowercase()
val isWindows = os.contains("windows")
val isMac = os.contains("mac")
val isLinux = !isWindows && !isMac

tasks.register<Exec>("compileNativeLib") {
    group = "build"
    description = "Compile C code into a shared library"

    val cSources = fileTree(engineDir) { include("**/*.c") }
    val headers = fileTree(engineDir) { include("**/*.h") }
    inputs.files(cSources, headers)

    val libName = when {
        isMac -> "libpainter.dylib"
        isWindows -> "painter.dll"
        else -> "libpainter.so"
    }
    val outputLib = file("$nativeLibDir/$libName")
    outputs.file(outputLib)

    doFirst {
        nativeLibDir.mkdirs()

        val args = mutableListOf<String>()
        args += "clang"

        // common
        args += listOf("-O2", "-I${engineDir.absolutePath}")

        // shared library + OS-specific bits
        if (isWindows) {
            // Define PAINTER_BUILD_DLL so symbols are exported
            args += listOf("-DPAINTER_BUILD_DLL")
            // No -fPIC, no -lm
            args += listOf("-shared")
            // If you need MSVC runtime dynamically: args += listOf("-Xlinker", "/DLL")
            // Ensure your C exports use __declspec(dllexport) or a .def file
        } else if (isMac) {
            args += listOf("-shared", "-fPIC")
            // -lm is in libSystem, not required; harmless to omit
        } else { // Linux
            args += listOf("-shared", "-fPIC")
            args += listOf("-lm")
        }

        // sources and output
        args += cSources.files.map { it.absolutePath }
        args += listOf("-o", outputLib.absolutePath)

        commandLine(args)
    }
}

fun findOnPath(names: List<String>): String? {
    val path = System.getenv("PATH") ?: System.getenv("Path") ?: return null
    val sep = File.pathSeparator                                  // ";" on Windows, ":" on Unix
    val dirs = path.split(sep).map(::File)
    return dirs.flatMap { d -> names.map { File(d, it) } }
        .firstOrNull { it.exists() && it.canExecute() }
        ?.absolutePath
}

tasks.register<Exec>("generateJextract") {
    group = "build"
    description = "Generate Java bindings from C headers using jextract"

    inputs.files(fileTree(engineDir) { include("**/*.h") })
    outputs.dir(generatedSourcesDir)

    doFirst {
        generatedSourcesDir.deleteRecursively()
        generatedSourcesDir.mkdirs()
    }

    // try common Windows and Unix names
    val candidates = if (System.getProperty("os.name").lowercase().contains("windows"))
        listOf("jextract.exe", "jextract.bat", "jextract.cmd")
    else
        listOf("jextract")

    // prefer PATH, then JAVA_HOME\bin
    val fromPath = findOnPath(candidates)
    val fromJavaHome = System.getenv("JAVA_HOME")?.let { home ->
        candidates.map { File("$home/bin", it) }.firstOrNull { it.exists() && it.canExecute() }?.absolutePath
    }
    val jextract = fromPath ?: fromJavaHome ?: "jextract" // last-resort

    val mainHeader = file("$engineDir/painter.h")

    // If it’s a .bat/.cmd on Windows, run via cmd.exe
    val isScript = jextract.endsWith(".bat", true) || jextract.endsWith(".cmd", true)
    if (isScript && System.getProperty("os.name").lowercase().contains("windows")) {
        commandLine(
            "cmd", "/c", jextract,
            "--output", generatedSourcesDir.absolutePath,
            "--target-package", "net.minestom.painter.generated",
            "--header-class-name", "PainterNative",
            "-I", engineDir.absolutePath,
            mainHeader.absolutePath
        )
    } else {
        commandLine(
            jextract,
            "--output", generatedSourcesDir.absolutePath,
            "--target-package", "net.minestom.painter.generated",
            "--header-class-name", "PainterNative",
            "-I", engineDir.absolutePath,
            mainHeader.absolutePath
        )
    }
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
    options.compilerArgs.addAll(
        listOf(
            "--enable-preview",
            "-Xlint:preview"
        )
    )
}

tasks.withType<Test> {
    jvmArgs("--enable-preview", "--enable-native-access=ALL-UNNAMED")
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
    jvmArgs("--enable-preview", "--enable-native-access=ALL-UNNAMED")
}

dependencies {
    testImplementation(platform("org.junit:junit-bom:5.10.0"))
    testImplementation("org.junit.jupiter:junit-jupiter")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher")
}
