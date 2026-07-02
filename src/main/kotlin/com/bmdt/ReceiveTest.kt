package com.bmdt

import java.net.DatagramPacket
import java.net.DatagramSocket

fun main() {
    val socket = DatagramSocket(5005)
    socket.soTimeout = 5000
    val buffer = ByteArray(2048)
    val packet = DatagramPacket(buffer, buffer.size)
    println("Listening on port 5005...")
    while (true) {
        try {
            socket.receive(packet)
            val msg = String(packet.data, 0, packet.length)
            println("Received: $msg")
        } catch (e: java.net.SocketTimeoutException) {
            println("Timeout - no data")
            break
        }
    }
}