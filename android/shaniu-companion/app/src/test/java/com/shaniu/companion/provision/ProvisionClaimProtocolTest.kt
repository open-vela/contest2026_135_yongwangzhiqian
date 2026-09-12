// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.util.Base64
import org.junit.Assert.*
import org.junit.Test

class ProvisionClaimProtocolTest {
    @Test fun configurationPersistencePrecedesApplyAndWipesCopiesOnFailure() {
        for (failStorage in listOf(false, true)) {
            val sent = mutableListOf<ByteArray>()
            val bundle = ByteArray(1300) { (it % 127).toByte() }
            val expected = bundle.copyOf()
            var saved = false
            var borrowedConfig: ByteArray? = null
            var borrowedReceipt: ByteArray? = null
            bootstrap().use { qr ->
                val protocol = ProvisionClaimProtocol(qr, bundle, { packet ->
                    if (packet[4] == 4.toByte()) assertTrue(saved)
                    sent += packet.copyOf()
                }, {}, { fail("Must not save a second receipt") },
                    beforeApplyConfiguration = { tx, configuration ->
                        borrowedReceipt = tx; borrowedConfig = configuration
                        assertArrayEquals(expected, configuration)
                        assertArrayEquals(sent.first().copyOfRange(12, 28), tx)
                        if (failStorage) error("storage unavailable")
                        saved = true
                    })
                bundle.fill(0) // The caller no longer owns the uploaded copy.
                protocol.start()
                protocol.receive(response(sent.last(), 3))
                protocol.receive(response(sent.last(), 4))
                protocol.receive(response(sent.last(), 4))
                if (failStorage) {
                    assertThrows(IllegalStateException::class.java) {
                        protocol.receive(response(sent.last(), 4))
                    }
                    assertTrue(sent.none { it[4] == 4.toByte() })
                    assertEquals(ProvisionClaimProtocol.State.FAILED, protocol.state)
                } else {
                    protocol.receive(response(sent.last(), 4))
                    assertEquals(4, sent.last()[4].toInt())
                }
                assertTrue(borrowedConfig!!.all { it == 0.toByte() })
                assertTrue(borrowedReceipt!!.all { it == 0.toByte() })
                protocol.close()
            }
        }
    }

    @Test fun recoveryOnlyQueriesSavedTransactionAfterLocalConfirmation() {
        for (result in listOf(6, 9)) {
            val sent = mutableListOf<ByteArray>()
            val tx = ByteArray(16) { 7 }
            bootstrap().use { qr ->
                val protocol = ProvisionClaimProtocol(qr, byteArrayOf(0),
                    { sent += it.copyOf() }, {}, { fail("Recovery must not APPLY") }, tx)
                protocol.start()
                assertArrayEquals(tx, sent.last().copyOfRange(12, 28))
                protocol.receive(response(sent.last(), 2))
                assertEquals(1, sent.size)
                protocol.receive(response(sent.last(), 3))
                assertEquals(5, sent.last()[4].toInt())
                assertEquals(32, sent.last().size)
                protocol.receive(response(sent.last(), result))
                val expected = if (result == 6) ProvisionClaimProtocol.State.COMMITTED
                               else ProvisionClaimProtocol.State.NOT_COMMITTED
                assertEquals(expected, protocol.state)
                protocol.disconnected(); protocol.close()
                assertEquals(expected, protocol.state)
                assertTrue(sent.all { it[4].toInt() in listOf(1, 5) })
            }
        }
    }

    private fun bootstrap() = ProvisionBootstrap.parse(("""{"protocol":"provision-bootstrap-v1",
        "device_id":"test-device","certificate_sha256":"${"00".repeat(32)}",
        "possession_secret":"${Base64.getEncoder().encodeToString(ByteArray(32) { 42 })}"} """).toCharArray())

    private fun response(request: ByteArray, state: Int, error: Int = 0): ByteArray =
        ByteBuffer.allocate(40).putInt(0x53505631).putInt(0x80000000.toInt())
            .put(request, 8, 20).putInt(8).putInt(state).putInt(error).array()

    @Test fun waitsForPhysicalConfirmationThenUploadsAndCommits() {
        val sent = mutableListOf<ByteArray>()
        val states = mutableListOf<ProvisionClaimProtocol.State>()
        val candidate = ByteArray(2000) { (it % 127).toByte() }
        val qr = bootstrap()
        val protocol = ProvisionClaimProtocol(qr, candidate, { sent += it.copyOf() }, states::add)
        qr.close()
        assertTrue(sent.isEmpty())
        protocol.start()
        assertEquals(1, sent.size)
        assertEquals(1, sent.last()[4].toInt())
        response(sent.last(), 2).forEach { protocol.receive(byteArrayOf(it)) }
        assertEquals(ProvisionClaimProtocol.State.LOCAL_CONFIRMATION, protocol.state)
        assertEquals(1, sent.size)
        protocol.receive(response(sent.last(), 3))
        assertEquals(2, sent.last()[4].toInt())
        assertEquals(2000, ByteBuffer.wrap(sent.last(), 32, 4).int)
        protocol.receive(response(sent.last(), 4))
        assertEquals(3, sent.last()[4].toInt())
        assertEquals(0, ByteBuffer.wrap(sent.last(), 32, 4).int)
        assertArrayEquals(candidate.copyOfRange(0, 1020), sent.last().copyOfRange(36, sent.last().size))
        protocol.receive(response(sent.last(), 4))
        assertEquals(1020, ByteBuffer.wrap(sent.last(), 32, 4).int)
        assertArrayEquals(candidate.copyOfRange(1020, 2000), sent.last().copyOfRange(36, sent.last().size))
        protocol.receive(response(sent.last(), 4))
        assertEquals(4, sent.last()[4].toInt())
        protocol.receive(response(sent.last(), 5))
        assertEquals(ProvisionClaimProtocol.State.VERIFYING, protocol.state)
        protocol.receive(response(sent.last(), 6))
        assertEquals(ProvisionClaimProtocol.State.COMMITTED, protocol.state)
        protocol.disconnected()
        assertEquals(ProvisionClaimProtocol.State.COMMITTED, protocol.state)
        protocol.close()
        // Caller data ownership is unchanged; the protocol copied it.
        assertEquals(1, candidate[1].toInt())
    }

    @Test fun rejectsWrongTransactionAndDoesNotLeakConfigurationBeforeConfirmation() {
        val sent = mutableListOf<ByteArray>()
        bootstrap().use { qr ->
            val protocol = ProvisionClaimProtocol(qr, ByteArray(20) { 99 }, { sent += it.copyOf() }, {})
            protocol.start()
            val bad = response(sent.last(), 3).also { it[12] = (it[12].toInt() xor 1).toByte() }
            assertThrows(IllegalStateException::class.java) { protocol.receive(bad) }
            assertEquals(1, sent.size)
            assertEquals(ProvisionClaimProtocol.State.FAILED, protocol.state)
            protocol.close()
        }
    }

    @Test fun interruptedApplyRemainsUnconfirmed() {
        val sent = mutableListOf<ByteArray>()
        bootstrap().use { qr ->
            val protocol = ProvisionClaimProtocol(qr, byteArrayOf(42), { sent += it.copyOf() }, {})
            protocol.start()
            protocol.receive(response(sent.last(), 3))
            protocol.receive(response(sent.last(), 4))
            protocol.receive(response(sent.last(), 4))
            assertEquals(ProvisionClaimProtocol.State.VERIFYING, protocol.state)
            protocol.disconnected()
            assertEquals(ProvisionClaimProtocol.State.UNCONFIRMED, protocol.state)
            protocol.close()
            assertEquals(ProvisionClaimProtocol.State.UNCONFIRMED, protocol.state)
        }
    }

    @Test fun validatedRemoteAuthenticationFailureRetainsOnlyStageAndCode() {
        val sent = mutableListOf<ByteArray>()
        bootstrap().use { qr ->
            val protocol = ProvisionClaimProtocol(qr, byteArrayOf(42), { sent += it.copyOf() }, {})
            protocol.start()
            protocol.receive(response(sent.last(), 7, -9))

            assertEquals(ProvisionClaimProtocol.State.FAILED, protocol.state)
            assertEquals(ProvisionClaimProtocol.State.AUTHENTICATING, protocol.failureStage)
            assertEquals(-9, protocol.failureCode)
            protocol.close()
        }
    }

    @Test fun verifiedApplyFailureDoesNotClaimCommitAndRetainsVerificationCode() {
        val sent = mutableListOf<ByteArray>()
        bootstrap().use { qr ->
            val protocol = ProvisionClaimProtocol(qr, byteArrayOf(42), { sent += it.copyOf() }, {})
            protocol.start()
            protocol.receive(response(sent.last(), 3))
            protocol.receive(response(sent.last(), 4))
            protocol.receive(response(sent.last(), 4))
            assertEquals(ProvisionClaimProtocol.State.VERIFYING, protocol.state)

            protocol.receive(response(sent.last(), 7, -12))
            assertEquals(ProvisionClaimProtocol.State.FAILED, protocol.state)
            assertEquals(ProvisionClaimProtocol.State.VERIFYING, protocol.failureStage)
            assertEquals(-12, protocol.failureCode)
            protocol.close()
        }
    }

    @Test fun malformedApplyResponseLeavesOutcomeUnconfirmedWithoutFailureCode() {
        val sent = mutableListOf<ByteArray>()
        bootstrap().use { qr ->
            val protocol = ProvisionClaimProtocol(qr, byteArrayOf(42), { sent += it.copyOf() }, {})
            protocol.start()
            protocol.receive(response(sent.last(), 3))
            protocol.receive(response(sent.last(), 4))
            protocol.receive(response(sent.last(), 4))

            assertThrows(IllegalStateException::class.java) {
                protocol.receive(response(sent.last(), 7, 0))
            }
            assertEquals(ProvisionClaimProtocol.State.UNCONFIRMED, protocol.state)
            assertEquals(ProvisionClaimProtocol.State.VERIFYING, protocol.failureStage)
            assertNull(protocol.failureCode)
            protocol.close()
        }
    }

    @Test fun failedReceiptPersistencePreventsApply() {
        val sent = mutableListOf<ByteArray>()
        bootstrap().use { qr ->
            var receipt: ByteArray? = null
            val protocol = ProvisionClaimProtocol(qr, byteArrayOf(42),
                { sent += it.copyOf() }, {}, { tx -> receipt = tx; error("disk full") })
            protocol.start()
            protocol.receive(response(sent.last(), 3))
            protocol.receive(response(sent.last(), 4))
            assertThrows(IllegalStateException::class.java) {
                protocol.receive(response(sent.last(), 4))
            }
            assertTrue(sent.none { it[4] == 4.toByte() })
            assertEquals(ProvisionClaimProtocol.State.FAILED, protocol.state)
            assertTrue(receipt!!.all { it == 0.toByte() })
            protocol.close()
        }
    }

    @Test fun savesMatchingReceiptBeforeApplyBytes() {
        val sent = mutableListOf<ByteArray>()
        bootstrap().use { qr ->
            var saved: ByteArray? = null
            val protocol = ProvisionClaimProtocol(qr, byteArrayOf(42), { packet ->
                if (packet[4] == 4.toByte()) assertArrayEquals(saved, packet.copyOfRange(12, 28))
                sent += packet.copyOf()
            }, {}, { saved = it.copyOf() })
            protocol.start()
            protocol.receive(response(sent.last(), 3))
            protocol.receive(response(sent.last(), 4))
            protocol.receive(response(sent.last(), 4))
            assertNotNull(saved)
            assertEquals(4, sent.last()[4].toInt())
            protocol.close()
        }
    }
}
