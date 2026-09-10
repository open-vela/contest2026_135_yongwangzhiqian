// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.DataInputStream
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/** Optional cross-language test against the production C session parser.
 * SHANIU_CONTROL_PEER names the compiled test_control_session host executable.
 * This proves SDC1 interoperability, not TLS/GATT or physical AP behavior.
 */
class DeviceControlInteropTest {
    @Test fun androidExchangesRealFramesWithFirmwareSession() {
        val executable = System.getenv("SHANIU_CONTROL_PEER")
        assumeTrue("Requires the C host peer; see Android README", !executable.isNullOrBlank())
        val peer = ProcessBuilder(requireNotNull(executable), "--pipe-peer")
            .redirectError(ProcessBuilder.Redirect.INHERIT).start()
        val reader = Executors.newSingleThreadExecutor()
        val input = DataInputStream(peer.inputStream)
        val responses = mutableListOf<Pair<DeviceControlProtocol.Command, DeviceControlProtocol.Snapshot>>()
        val protocol = DeviceControlProtocol(ByteArray(32) { 42 }, {
            peer.outputStream.write(it); peer.outputStream.flush()
        }, { command, snapshot -> responses += command to snapshot })
        fun receive(): DeviceControlProtocol.Snapshot {
            val frame = reader.submit<ByteArray> { ByteArray(40).also { input.readFully(it) } }
                .get(3, TimeUnit.SECONDS)
            // The Android parser must accept arbitrary transport fragmentation.
            frame.forEach { protocol.receive(byteArrayOf(it)) }
            return responses.last().second
        }
        fun request(command: DeviceControlProtocol.Command, value: Int = 0): DeviceControlProtocol.Snapshot {
            assertTrue(protocol.request(command, value))
            val result = receive()
            assertEquals(command, responses.last().first)
            return result
        }
        try {
            protocol.start(); receive()
            assertTrue(protocol.authenticated)
            assertEquals(DeviceControlProtocol.Command.AUTH, responses.last().first)
            val status = request(DeviceControlProtocol.Command.STATUS)
            assertTrue(status.ready); assertTrue(status.busy)
            assertEquals(false, status.wifiReady)
            assertEquals(50, status.volume); assertEquals(0, status.persona)
            assertNull(status.turn)
            val refused = request(DeviceControlProtocol.Command.VOLUME, 73)
            assertTrue(refused.error < 0); assertNull(refused.volume)
            assertFalse(request(DeviceControlProtocol.Command.CANCEL).busy)
            assertEquals(73, request(DeviceControlProtocol.Command.VOLUME, 73).volume)
            assertEquals(4, request(DeviceControlProtocol.Command.PERSONA, 4).persona)
            val confirmed = request(DeviceControlProtocol.Command.STATUS)
            assertEquals(73, confirmed.volume); assertEquals(4, confirmed.persona)
            assertEquals(0, confirmed.error)
            assertEquals(true, confirmed.wifiReady)
            assertEquals(0, request(DeviceControlProtocol.Command.CLEAR_HISTORY).error)
            val saving = request(DeviceControlProtocol.Command.MEMORY_SET, 1)
            assertTrue(saving.memoryPending); assertNull(saving.memoryEnabled)
            val retained = request(DeviceControlProtocol.Command.STATUS)
            assertTrue(retained.memorySupported); assertEquals(true, retained.memoryEnabled)
            assertFalse(retained.memoryPending || retained.memoryFailed)
            val deleting = request(DeviceControlProtocol.Command.MEMORY_DELETE)
            assertTrue(deleting.memoryPending); assertNull(deleting.memoryEnabled)
            assertEquals(false, request(DeviceControlProtocol.Command.STATUS).memoryEnabled)
            peer.outputStream.close()
            assertTrue(peer.waitFor(3, TimeUnit.SECONDS))
            assertEquals(0, peer.exitValue())
        } finally {
            protocol.close()
            peer.destroyForcibly()
            input.close()
            peer.outputStream.close()
            reader.shutdownNow()
        }
    }
}
