// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.nio.CharBuffer
import java.nio.charset.StandardCharsets

/** Complete network/cloud candidate. SCB2 carries a cloud credential; SCB3
 * also carries the independent owner control key. Neither carries a device private key.
 * Caller clears the returned sensitive bytes after the connection copies them.
 */
object ProvisionSettings {
    /** Extracts only the owner credential from a locally encoded candidate.
     * Validate envelope lengths before treating any trailing bytes as a key.
     * This is not a replacement for the board's complete configuration decoder.
     */
    internal fun <T> useControlKey(candidate: ByteArray, block: (ByteArray?) -> T): T {
        require(candidate.size in 32..16384)
        val header = ByteBuffer.wrap(candidate)
        val magic = header.int
        require(magic in 0x53434231..0x53434233)
        val cloudSize = header.getInt(28)
        val caSize = header.getInt(24)
        require(caSize in 1..4096)
        require(if (magic == 0x53434231) cloudSize == 0 else cloudSize in 24..16384)
        val networkSize = 32L + (candidate[4].toInt() and 255) +
            (candidate[5].toInt() and 255) + (candidate[6].toInt() and 255) + caSize
        val ownerSize = if (magic == 0x53434233) 32 else 0
        require(networkSize + cloudSize + ownerSize == candidate.size.toLong())
        if (cloudSize != 0) require(header.getInt(networkSize.toInt()) == 0x43434631)
        val key = if (ownerSize == 0) null else candidate.copyOfRange(candidate.size - 32, candidate.size)
        return try {
            require(key == null || key.any { it != 0.toByte() })
            block(key)
        } finally { key?.fill(0) }
    }

    fun encodeCloud(ssid: String, password: CharArray, baseUrl: String,
                    key: CharArray, dialect: CloudSettings.Dialect,
                    asrModel: String, chatModel: String, ttsModel: String,
                    ipv4: ByteArray, caDer: ByteArray, utcSeconds: Long,
                    controlKey: ByteArray? = null): ByteArray {
        require(controlKey == null || (controlKey.size == 32 && controlKey.any { it != 0.toByte() }))
        val cloud = CloudSettings.encode(baseUrl, key, dialect, asrModel, chatModel, ttsModel)
        var network: ByteArray? = null
        try {
            val uri = java.net.URI(baseUrl)
            network = encode(ssid, password, uri.host, ipv4,
                if (uri.port == -1) 443 else uri.port, caDer, utcSeconds)
            val output = network.copyOf(network.size + cloud.size + (controlKey?.size ?: 0))
            output[3] = (if (controlKey == null) '2' else '3').code.toByte()
            ByteBuffer.wrap(output).putInt(28, cloud.size)
            cloud.copyInto(output, network.size)
            controlKey?.copyInto(output, network.size + cloud.size)
            return output
        } finally { cloud.fill(0); network?.fill(0) }
    }

    fun encode(ssid: String, password: CharArray, host: String, ipv4: ByteArray,
               port: Int, caDer: ByteArray, utcSeconds: Long): ByteArray {
        val ssidBytes = StandardCharsets.UTF_8.newEncoder().encode(CharBuffer.wrap(ssid))
        val passwordBytes = StandardCharsets.UTF_8.newEncoder().encode(CharBuffer.wrap(password))
        try {
            require(ssidBytes.remaining() in 1..32 && (0 until ssidBytes.remaining()).none { ssidBytes[it] == 0.toByte() })
            val count = passwordBytes.remaining()
            require(count == 0 || count in 8..64)
            require((0 until count).none { passwordBytes[it] == 0.toByte() })
            if (count == 64) require((0 until count).all {
                passwordBytes[it].toInt().toChar() in "0123456789abcdefABCDEF"
            })
            require(host.length in 1..127 && host.split('.').all {
                it.length in 1..63 && Regex("[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?").matches(it)
            })
            require(ipv4.size == 4 && (ipv4[0].toInt() and 255) in 1..223 &&
                    (ipv4[0].toInt() and 255) != 127)
            require(port in 1..65535 && caDer.size in 1..4096 &&
                    utcSeconds in 1704067200L..4133980799L)
            val name = host.toByteArray(StandardCharsets.US_ASCII)
            return ByteBuffer.allocate(32 + ssidBytes.remaining() + count + name.size + caDer.size)
                .putInt(0x53434231).put(ssidBytes.remaining().toByte()).put(count.toByte())
                .put(name.size.toByte()).put(0).putShort(port.toShort()).putShort(0)
                .put(ipv4).putLong(utcSeconds).putInt(caDer.size).putInt(0)
                .put(ssidBytes).put(passwordBytes).put(name).put(caDer).array()
        } finally {
            if (ssidBytes.hasArray()) ssidBytes.array().fill(0)
            if (passwordBytes.hasArray()) passwordBytes.array().fill(0)
        }
    }
}
