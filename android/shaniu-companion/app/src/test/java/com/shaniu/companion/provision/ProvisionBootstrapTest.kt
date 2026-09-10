// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.provision

import java.util.Base64
import org.junit.Assert.*
import org.junit.Test

class ProvisionBootstrapTest {
    private val bytes = ByteArray(32) { (it + 1).toByte() }
    private val secret = Base64.getEncoder().encodeToString(bytes)
    private val valid = """{"protocol":"provision-bootstrap-v1","device_id":"test-device","certificate_sha256":"${"ab".repeat(32)}","possession_secret":"$secret"}"""

    @Test fun ownedSecretAndTemporaryCopiesAreClearedOnCloseAndFailure() {
        val input = valid.toCharArray()
        val bootstrap = ProvisionBootstrap.parse(input)
        input.fill('\u0000')
        assertEquals("test-device", bootstrap.deviceId)
        assertFalse(bootstrap.toString().contains(secret))
        assertNotNull(bootstrap.newTls())
        lateinit var borrowed: ByteArray
        assertThrows(IllegalStateException::class.java) {
            bootstrap.usePossessionSecret {
                borrowed = it
                assertArrayEquals(bytes, it)
                throw IllegalStateException("synthetic failure")
            }
        }
        assertArrayEquals(ByteArray(32), borrowed)
        bootstrap.close()
        bootstrap.close()
        assertThrows(IllegalStateException::class.java) { bootstrap.newTls() }
        assertThrows(IllegalStateException::class.java) { bootstrap.usePossessionSecret { fail() } }
    }

    @Test fun rejectsAmbiguousMalformedAndUnboundedBootstrapWithoutEchoingInput() {
        val invalid = listOf(
            valid.dropLast(1) + ",\"device_id\":\"other\"}",
            valid.dropLast(1) + ",\"extra\":\"value\"}",
            valid + " {}", valid.replace("provision-bootstrap-v1", "provision-bootstrap-v2"),
            valid.replace("test-device", "../device"), valid.replace("test-device", ""),
            valid.replace("\"device_id\":\"test-device\",", ""),
            valid.replace("\"test-device\"", "123"),
            valid.replace("ab".repeat(32), "AB".repeat(32)),
            valid.replace(secret, secret.dropLast(1)),
            valid.replace(secret, "x".repeat(44)), "x".repeat(1025),
        )
        for (text in invalid) {
            val error = assertThrows(IllegalArgumentException::class.java) {
                ProvisionBootstrap.parse(text.toCharArray())
            }
            assertEquals("Invalid provisioning bootstrap", error.message)
            assertNull(error.cause)
        }
    }
}
