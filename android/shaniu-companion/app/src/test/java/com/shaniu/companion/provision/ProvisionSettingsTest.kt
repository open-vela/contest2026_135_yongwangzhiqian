// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import org.junit.Assert.*
import org.junit.Test

class ProvisionSettingsTest {
    @Test fun ownerKeyIsBoundToTheSameCandidateAndCallerKeepsOwnership() {
        val owner = ByteArray(32) { (it + 1).toByte() }
        val bundle = ProvisionSettings.encodeCloud("lab", "password".toCharArray(),
            "https://cloud.example/v1", "test-key".toCharArray(), CloudSettings.Dialect.MIMO,
            "asr", "chat", "tts", byteArrayOf(192.toByte(),168.toByte(),1,2),
            byteArrayOf(0x30,0), 1800000000, owner)
        val header = ByteBuffer.wrap(bundle)
        assertEquals(0x53434233, header.int)
        val cloudSize = header.getInt(28)
        assertEquals(0x43434631, header.getInt(bundle.size - 32 - cloudSize))
        assertArrayEquals(owner, bundle.copyOfRange(bundle.size - 32, bundle.size))
        var borrowed: ByteArray? = null
        ProvisionSettings.useControlKey(bundle) { assertArrayEquals(owner, it); borrowed = it }
        assertTrue(borrowed!!.all { it == 0.toByte() })
        assertThrows(IllegalStateException::class.java) {
            ProvisionSettings.useControlKey(bundle) { borrowed = it; error("save failed") }
        }
        assertTrue(borrowed!!.all { it == 0.toByte() })
        for (bad in listOf(bundle.copyOf(bundle.size - 1), bundle.copyOf(bundle.size + 1),
            bundle.copyOf().also { ByteBuffer.wrap(it).putInt(28, Int.MAX_VALUE) },
            bundle.copyOf().also { it.fill(0, it.size - 32) })) {
            assertThrows(IllegalArgumentException::class.java) {
                ProvisionSettings.useControlKey(bad) { fail("Malformed credential must not be saved") }
            }
        }
        bundle.fill(0)
        assertEquals(1.toByte(), owner[0])
        owner.fill(0)
    }

    @Test fun rejectsMissingEntropyAndMalformedControlKeySizes() {
        for (owner in listOf(ByteArray(0), ByteArray(31) { 1 }, ByteArray(32), ByteArray(33) { 1 })) {
            assertThrows(IllegalArgumentException::class.java) {
                ProvisionSettings.encodeCloud("lab", "password".toCharArray(),
                    "https://cloud.example/v1", "test-key".toCharArray(), CloudSettings.Dialect.MIMO,
                    "asr", "chat", "tts", byteArrayOf(192.toByte(),168.toByte(),1,2),
                    byteArrayOf(0x30,0), 1800000000, owner)
            }
        }
    }

    @Test fun packsCloudAndNetworkInOneTransaction() {
        val key = "test-only-key".toCharArray()
        val bundle = ProvisionSettings.encodeCloud("lab", "password".toCharArray(),
            "https://cloud.example/v1", key, CloudSettings.Dialect.MIMO,
            "asr", "chat", "tts", byteArrayOf(192.toByte(),168.toByte(),1,2),
            byteArrayOf(0x30,0), 1800000000)
        val header = ByteBuffer.wrap(bundle)
        assertEquals(0x53434232, header.int)
        ProvisionSettings.useControlKey(bundle) { assertNull(it) }
        val cloudSize = header.getInt(28)
        val cloud = ByteBuffer.wrap(bundle, bundle.size-cloudSize, cloudSize)
        assertEquals(0x43434631, cloud.int)
        assertEquals(2, cloud.get().toInt())
        cloud.get()
        assertEquals(443, cloud.short.toInt())
        assertEquals(13, cloud.short.toInt())
        assertArrayEquals("test-only-key".toCharArray(), key)
        bundle.fill(0); key.fill('\u0000')
    }

    @Test fun packsCompleteCandidateWithoutDeviceKeys() {
        val password = "test-only-pass".toCharArray()
        val data = ProvisionSettings.encode("lab",password,"gateway.local",
            byteArrayOf(192.toByte(),168.toByte(),1,2),8765,byteArrayOf(0x30,0),1800000000)
        val header = ByteBuffer.wrap(data)
        assertEquals(0x53434231,header.int)
        assertEquals(3,header.get().toInt())
        assertEquals(password.size,header.get().toInt())
        assertEquals(13,header.get().toInt())
        assertEquals(0,header.get().toInt())
        assertEquals(8765,header.short.toInt())
        assertEquals(0,header.short.toInt())
        assertEquals(0xc0a80102.toInt(),header.int)
        assertEquals(1800000000L,header.long)
        assertEquals(2,header.int)
        assertEquals(0,header.int)
        assertEquals(32+3+password.size+13+2,data.size)
        assertArrayEquals("test-only-pass".toCharArray(),password)
        password.fill('\u0000'); data.fill(0)
    }

    @Test fun rejectsMalformedInputsBeforeCreatingBundle() {
        fun encode(ssid: String="lab", psk: String="test-only-pass", host: String="gateway.local",
                   ip: ByteArray=byteArrayOf(192.toByte(),168.toByte(),1,2)) =
            ProvisionSettings.encode(ssid,psk.toCharArray(),host,ip,8765,byteArrayOf(0x30,0),1800000000)
        assertThrows(IllegalArgumentException::class.java) { encode(ssid="中".repeat(11)) }
        assertThrows(IllegalArgumentException::class.java) { encode(psk="short") }
        assertThrows(IllegalArgumentException::class.java) { encode(psk="x".repeat(64)) }
        assertThrows(IllegalArgumentException::class.java) { encode(host="-gateway.local") }
        assertThrows(IllegalArgumentException::class.java) { encode(ip=byteArrayOf(127,0,0,1)) }
        assertThrows(IllegalArgumentException::class.java) { encode(ssid="lab\u0000x") }
    }
}
