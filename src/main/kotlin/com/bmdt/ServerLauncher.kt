package com.bmdt

import dev.slimevr.VRServer
import dev.slimevr.config.ConfigManager
import java.io.File

fun main() {
    println("[ServerLauncher] Starting SlimeVR server...")

    // Path to the existing config file (project root)
    val configFile = File("config.json")
    if (!configFile.exists()) {
        println("[ServerLauncher] Config file not found at ${configFile.absolutePath} – exiting.")
        return
    }

    // Load configuration – ConfigManager expects a file path string.
    // loadConfig() already falls back to a default VRConfig() internally
    // if the file is missing/empty/corrupt, so vrConfig is never null here.
    val configManager = ConfigManager(configFile.absolutePath)
    configManager.loadConfig()

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