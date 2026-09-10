// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.gateway

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Test

class ConsoleEnrollmentTest {
    private val token = "owner-issued-token-1234"
    private val pin = "sha256/${"A".repeat(43)}="
    private val valid = """{"protocol":"shaniu.console-enrollment/1","device_id":"aidk-1","gateway_origin":"https://gateway.example:8770","certificate_pins":["$pin"],"access_token":"$token","expires_at_ms":2000}"""

    @Test fun parsesBoundHttpsEnrollmentAndWipesTemporaryToken() {
        val enrollment = ConsoleEnrollment.parse(valid.toCharArray(), nowMs = 1000)
        assertEquals("aidk-1", enrollment.deviceId)
        assertEquals("https://gateway.example:8770/", enrollment.gatewayOrigin.toString())
        assertEquals(setOf(pin), enrollment.certificatePins)
        lateinit var temporary: CharArray
        assertEquals("ok", enrollment.useAccessToken {
            temporary = it
            assertEquals(token, it.concatToString())
            "ok"
        })
        assertArrayEquals(CharArray(token.length), temporary)
        assertFalse(enrollment.toString().contains(token))
        enrollment.close()
        assertThrows(IllegalStateException::class.java) { enrollment.useAccessToken { } }
    }

    @Test fun rejectsAmbiguousInvalidOrExpiredOwnerPackages() {
        val invalid = listOf(
            valid.replace("\"protocol\":\"shaniu.console-enrollment/1\",", ""),
            valid.dropLast(1) + ",\"extra\":\"x\"}",
            valid.replace("\"device_id\":\"aidk-1\"", "\"device_id\":\"aidk-1\",\"device_id\":\"other\""),
            valid.replace("aidk-1", "../aidk"),
            valid.replace("https://", "http://"),
            valid.replace("[\"$pin\"]", "[]"),
            valid.replace("[\"$pin\"]", "[\"$pin\",\"$pin\"]"),
            valid.replace(token, "token with space"),
            valid.replace("2000", "1000"),
        )
        for (text in invalid) {
            val error = assertThrows(IllegalArgumentException::class.java) {
                ConsoleEnrollment.parse(text.toCharArray(), nowMs = 1000)
            }
            assertEquals("Invalid console enrollment", error.message)
            assertEquals(null, error.cause)
        }
    }

    @Test fun existingBindingCannotBeReplacedByAnotherDevicePackage() {
        ConsoleEnrollment.parse(valid.toCharArray(), 1000, "aidk-1").use {
            assertEquals("aidk-1", it.deviceId)
        }
        for (expected in listOf("other-device", "")) {
            assertThrows(IllegalArgumentException::class.java) {
                ConsoleEnrollment.parse(valid.toCharArray(), 1000, expected)
            }
        }
        // An unbound phone may import owner credentials; the connection path
        // must still authenticate a device snapshot before storing a binding.
        ConsoleEnrollment.parse(valid.toCharArray(), 1000).use {
            assertEquals("aidk-1", it.deviceId)
        }
    }
}
