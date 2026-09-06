// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.gateway

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.Base64

class GatewaySecurityTest {
    @Test
    fun explicitBlankOriginLeavesGatewayDisabled() {
        assertNull(GatewayTransportConfiguration.fromExplicit(null))
        assertNull(GatewayTransportConfiguration.fromExplicit(""))
        assertNull(GatewayTransportConfiguration.fromExplicit("   "))
    }

    @Test
    fun configurationAcceptsSystemTrustOrCanonicalSha256Pins() {
        val systemTrust = GatewayTransportConfiguration.fromExplicit(
            "https://gateway.example",
        )!!
        assertFalse(systemTrust.certificatePinningEnabled)

        val pin = "sha256/" + Base64.getEncoder().encodeToString(ByteArray(32) { 0x5a })
        val pinned = GatewayTransportConfiguration.fromExplicit(
            "https://gateway.example:443",
            setOf(pin),
        )!!
        assertTrue(pinned.certificatePinningEnabled)
        assertEquals(setOf(pin), pinned.certificatePins)
    }

    @Test
    fun configurationRejectsMalformedOrImplicitlyTrimmedValues() {
        listOf(
            setOf("sha1/" + "A".repeat(44)),
            setOf("sha256/not-base64"),
            setOf("sha256/" + Base64.getEncoder().encodeToString(ByteArray(31))),
        ).forEach { pins ->
            assertThrows(IllegalArgumentException::class.java) {
                GatewayTransportConfiguration.fromExplicit("https://gateway.example", pins)
            }
        }
        assertThrows(IllegalArgumentException::class.java) {
            GatewayTransportConfiguration.fromExplicit(" https://gateway.example")
        }
    }

    @Test
    fun tokenPolicyAcceptsBearerAlphabetAndRejectsHeaderInjection() {
        val token = "abc.DEF_0123-xyz~+/="
        assertEquals(token, GatewayTokenPolicy.requireValid(token))

        listOf(
            "short",
            "token with spaces and enough length",
            "valid-looking-token\r\nInjected: yes",
            "x".repeat(GatewayTokenPolicy.MAX_TOKEN_CHARS + 1),
        ).forEach { invalid ->
            assertThrows(IllegalArgumentException::class.java) {
                GatewayTokenPolicy.requireValid(invalid)
            }
        }
    }
}
