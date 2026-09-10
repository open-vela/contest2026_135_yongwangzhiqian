// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import org.junit.Assert.assertArrayEquals
import org.junit.Test

class NfcHandoverProtocolTest {
    private fun hex(value: String) = value.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    private val select = hex("00A4040008F05348414E495501")

    @Test fun selectionReturnsOnlyVersionWhileForeground() {
        assertArrayEquals(hex("53484E019000"), NfcHandoverProtocol.respond(select, true))
        assertArrayEquals(hex("53484E019000"), NfcHandoverProtocol.respond(select + byteArrayOf(0), true))
        assertArrayEquals(hex("6985"), NfcHandoverProtocol.respond(select, false))
    }

    @Test fun malformedOrOtherApplicationsNeverSucceed() {
        for (length in 0 until select.size) {
            val reply = NfcHandoverProtocol.respond(select.copyOf(length), true)
            check(!reply.contentEquals(hex("53484E019000")))
        }
        for (index in select.indices) {
            val changed = select.copyOf()
            changed[index] = (changed[index].toInt() xor 1).toByte()
            check(!NfcHandoverProtocol.respond(changed, true).contentEquals(hex("53484E019000")))
        }
        assertArrayEquals(hex("6A82"), NfcHandoverProtocol.respond(select + byteArrayOf(1), true))
        assertArrayEquals(hex("6A82"), NfcHandoverProtocol.respond(select + byteArrayOf(0, 0), true))
        assertArrayEquals(hex("6D00"), NfcHandoverProtocol.respond(hex("00B00000"), true))
    }
    @Test fun locatorRequiresCurrentSelectionAndIsSingleUse() {
        val session = NfcHandoverSession()
        val put = hex("80DA0000080102030405060708")
        assertArrayEquals(hex("6985"), session.exchange(put, true).first)
        session.exchange(select, true)
        val result = session.exchange(put, true)
        assertArrayEquals(hex("9000"), result.first)
        assertArrayEquals(hex("0102030405060708"), result.second)
        assertArrayEquals(hex("6985"), session.exchange(put, true).first)
        session.exchange(select, true)
        session.reset()
        assertArrayEquals(hex("6985"), session.exchange(put, true).first)
        session.exchange(select, true)
        session.exchange(put, false)
        assertArrayEquals(hex("6985"), session.exchange(put, true).first)
    }

    @Test fun malformedLocatorNeverSuppliesDiscoveryHint() {
        val put = hex("80DA0000080102030405060708")
        val session = NfcHandoverSession()
        for (length in 0 until put.size) {
            session.exchange(select, true)
            check(session.exchange(put.copyOf(length), true).second == null)
        }
        for (index in 1..4) {
            session.exchange(select, true)
            val bad = put.copyOf()
            bad[index] = (bad[index].toInt() xor 1).toByte()
            check(session.exchange(bad, true).second == null)
        }
        session.exchange(select, true)
        check(session.exchange(put + byteArrayOf(0), true).second == null)
    }
}
