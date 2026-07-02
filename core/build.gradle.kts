plugins {
    kotlin("jvm") version "2.0.20"
    kotlin("plugin.serialization") version "2.0.20"
    `java-library`
    id("com.github.gmazzo.buildconfig") version "5.4.0"
}

kotlin {
    jvmToolchain {
        languageVersion.set(JavaLanguageVersion.of(17))
    }
}

java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(17))
    }
}

tasks.withType<org.jetbrains.kotlin.gradle.tasks.KotlinCompile> {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
        freeCompilerArgs.addAll("-Xvalue-classes", "-opt-in=kotlin.ExperimentalStdlibApi")
    }
}

tasks.withType<JavaCompile> {
    options.encoding = "UTF-8"
}
tasks.withType<Test> {
    systemProperty("file.encoding", "UTF-8")
}
tasks.withType<Javadoc> {
    options.encoding = "UTF-8"
}

tasks.withType<Jar> {
    from("../../LICENSE-APACHE")
    from("../../LICENSE-MIT")
}

dependencies {
    implementation(project(":solarxr-protocol"))

    implementation("com.google.flatbuffers:flatbuffers-java:22.10.26")
    implementation("commons-cli:commons-cli:1.11.0")
    implementation("com.fasterxml.jackson.core:jackson-databind:2.21.0")
    implementation("com.fasterxml.jackson.dataformat:jackson-dataformat-yaml:2.21.0")

    implementation("com.github.jonpeterson:jackson-module-model-versioning:1.2.2")
    implementation("org.apache.commons:commons-math3:3.6.1")
    implementation("org.apache.commons:commons-lang3:3.20.0")
    implementation("org.apache.commons:commons-collections4:4.5.0")

    implementation("com.illposed.osc:javaosc-core:0.8")
    implementation("org.java-websocket:Java-WebSocket:1.+")
    implementation("com.melloware:jintellitype:1.+")

    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.7.1")
    implementation("com.mayakapps.kache:kache:2.1.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.8.1")

    api("com.github.loucass003:EspflashKotlin:v0.11.0")

    implementation(kotlin("reflect"))
    implementation("com.github.SlimeVR:oscquery-kt:566a0cba58")

    implementation("io.ktor:ktor-client-core:2.3.13")
    implementation("io.ktor:ktor-client-cio:2.3.13")

    testImplementation(kotlin("test"))
    testImplementation(platform("org.junit:junit-bom:6.0.2"))
    testImplementation("org.junit.jupiter:junit-jupiter")
    testImplementation("org.junit.platform:junit-platform-launcher")
}

tasks.test {
    useJUnitPlatform()
}