plugins {
    id("java")
}

group = "net.minestom.painter"
version = "1.0-SNAPSHOT"

repositories {
    mavenCentral()
}

dependencies {
    implementation(project(":core"))
    implementation("net.minestom:minestom:2026.04.13-1.21.11")

    testImplementation(platform("org.junit:junit-bom:6.0.3"))
    testImplementation("org.junit.jupiter:junit-jupiter")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher")
    // Demo server dependencies (test-only)
    testImplementation("ch.qos.logback:logback-classic:1.5.32")
    testImplementation("me.tongfei:progressbar:0.10.2")
}

tasks.withType<Test> {
    jvmArgs("--enable-preview", "--enable-native-access=ALL-UNNAMED")
    useJUnitPlatform()
    // Demo files are in test sources but aren't JUnit tests
    failOnNoDiscoveredTests.set(false)
}

tasks.withType<JavaExec> {
    jvmArgs("--enable-preview", "--enable-native-access=ALL-UNNAMED")
}

tasks.register<JavaExec>("runDemo") {
    group = "application"
    description = "Run the Painter demo server"
    mainClass.set("net.minestom.paint.Demo")
    classpath = sourceSets["test"].runtimeClasspath
    standardInput = System.`in`
    
    // Allow passing system properties
    systemProperties(System.getProperties().filter { 
        it.key.toString().startsWith("painter.") 
    }.mapKeys { it.key.toString() })
}