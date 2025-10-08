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
    implementation("net.minestom:minestom:2025.10.05-1.21.8")

    testImplementation(platform("org.junit:junit-bom:5.10.0"))
    testImplementation("org.junit.jupiter:junit-jupiter")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher")
    // Demo server dependencies (test-only)
    testImplementation("ch.qos.logback:logback-classic:1.5.19")
}

tasks.withType<Test> {
    jvmArgs("--enable-preview", "--enable-native-access=ALL-UNNAMED")
    useJUnitPlatform()
}

tasks.withType<JavaExec> {
    jvmArgs("--enable-preview", "--enable-native-access=ALL-UNNAMED")
}

tasks.register<JavaExec>("runDemo") {
    group = "application"
    description = "Run the Painter demo server"
    mainClass.set("net.minestom.paint.demo.Demo")
    classpath = sourceSets["test"].runtimeClasspath
    standardInput = System.`in`
    
    // Allow passing system properties
    systemProperties(System.getProperties().filter { 
        it.key.toString().startsWith("painter.") 
    }.mapKeys { it.key.toString() })
}