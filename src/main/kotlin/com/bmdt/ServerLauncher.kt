package com.bmdt

import dev.slimevr.VRServer
import dev.slimevr.config.ConfigManager
import java.io.File

fun main() {
    println("[ServerLauncher] Starting SlimeVR server...")

    // Path to the existing config file (project root).
    // Note: despite the .json extension, ConfigManager parses this as YAML
    // (it uses Jackson's YAMLFactory internally) — the file's actual content
    // is valid YAML (starts with "---"), so this works correctly as-is.
    val configFile = File("config.json")
    if (!configFile.exists()) {
        println("[ServerLauncher] Config file not found at ${configFile.absolutePath} – exiting.")
        return
    }

    val configManager = ConfigManager(configFile.absolutePath)
    configManager.loadConfig()
    // No null-check / reflection fallback needed here: ConfigManager.loadConfig()
    // already guarantees vrConfig is non-null internally (it falls back to
    // `new VRConfig()` on parse failure or missing file — see ConfigManager.java
    // line 57-59). Reaching into the private field via reflection was both
    // unnecessary and fragile against future SlimeVR core changes.

    // Create the full server (UDP + WebSocket)
    val server = VRServer(configManager = configManager)

    // Start the server thread
    server.start()

    println("[ServerLauncher] SlimeVR server started. Press Ctrl+C to stop.")

    try {
        server.join()
    } catch (e: InterruptedException) {
        println("[ServerLauncher] Server stopped.")
    }
}
