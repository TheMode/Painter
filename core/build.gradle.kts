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

sourceSets {
    main {
        java {
            srcDir(generatedSourcesDir)
        }
    }
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
    dependsOn("generateJextract")
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
}

tasks.withType<JavaExec> {
    jvmArgs("--enable-preview")
}

dependencies {
    testImplementation(platform("org.junit:junit-bom:5.10.0"))
    testImplementation("org.junit.jupiter:junit-jupiter")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher")
}
