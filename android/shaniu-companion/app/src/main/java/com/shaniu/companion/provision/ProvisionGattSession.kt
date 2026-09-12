// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.provision

import java.io.IOException
import java.util.ArrayDeque
import java.util.concurrent.atomic.AtomicLong

/** One worker-owned GATT connection. Platform callbacks must carry the captured
 * generation and the token of the outstanding write-with-response operation.
 * No TLS work belongs on Bluetooth/UI callbacks. Poll tick() even when idle.
 * The platform owner must disconnect/close BluetoothGatt when closed becomes true.
 */
class ProvisionGattSession internal constructor(
    factory: ((ByteArray) -> Unit, (ByteArray) -> Unit) -> ProvisionTlsChannel,
    plaintext: (ByteArray) -> Unit,
    private val nowMs: () -> Long,
) : AutoCloseable {
    constructor(tls: ProvisionTls, plaintext: (ByteArray) -> Unit,
                nowMs: () -> Long = { System.nanoTime() / 1_000_000 }) :
        this(tls::newChannel, plaintext, nowMs)

    class Write internal constructor(val generation: Long, val token: Long, val value: ByteArray)

    val generation = generations.incrementAndGet().also { check(it > 0) }
    private val outbound = ArrayDeque<ByteArray>()
    private val inbound = ArrayDeque<ByteArray>()
    private var outgoingBytes = 0
    private var incomingBytes = 0
    private var offset = 0
    private var mtu = 23
    private var token = 0L
    private var pending: Write? = null
    private var pendingAt = 0L
    private val startedAt = nowMs()
    private var lastNow = startedAt
    private var started = false
    private val channel = factory(::queueCiphertext, plaintext)
    var closed = false
        private set
    var failure: String? = null
        private set
    val established: Boolean get() = !closed && channel.established

    fun start() = guarded {
        check(!started) { "GATT session already started" }
        started = true
        channel.start()
    }

    /** Call only after subscription and MTU negotiation succeeded. In-flight
     * write sizing is immutable; subsequent writes use the new negotiated MTU.
     */
    fun negotiatedMtu(connection: Long, value: Int) {
        if (connection != generation || closed) return
        guarded {
            require(value in 23..517) { "Invalid ATT MTU" }
            mtu = value
        }
    }

    private fun queueCiphertext(bytes: ByteArray) {
        if (bytes.isEmpty() || bytes.size > LIMIT - outgoingBytes) {
            throw IOException("GATT transmit queue full")
        }
        outbound.add(bytes.copyOf())
        outgoingBytes += bytes.size
    }

    /** Copy notification data promptly, but process TLS only in processInput(). */
    fun enqueueIncoming(connection: Long, bytes: ByteArray) {
        if (connection != generation || closed) return
        guarded {
            if (bytes.isEmpty() || bytes.size > mtu - 3 ||
                bytes.size > LIMIT - incomingBytes || inbound.size >= MAX_RX_PACKETS) {
                throw IOException("GATT receive queue full or invalid packet")
            }
            inbound.add(bytes.copyOf())
            incomingBytes += bytes.size
        }
    }

    fun processInput() = guarded {
        check(started) { "GATT session not started" }
        while (inbound.isNotEmpty()) {
            val bytes = inbound.remove()
            incomingBytes -= bytes.size
            try { channel.receive(bytes) } finally { bytes.fill(0) }
        }
    }

    fun send(bytes: ByteArray) = guarded { channel.send(bytes) }

    /** At most one ATT write with response is in flight. Taking a write does
     * not release queue capacity. Only its successful callback does so.
     */
    fun nextWrite(): Write? {
        if (closed) return null
        tick()
        if (closed || pending != null || outbound.isEmpty()) return null
        val head = outbound.peek()
        check(token != Long.MAX_VALUE)
        // Conservative interoperability cap: negotiated MTU is an upper bound,
        // not a required write size.
        val length = minOf(mtu - 3, MAX_COMPATIBLE_ATT_WRITE_BYTES, head.size - offset)
        return Write(generation, ++token, head.copyOfRange(offset, offset + length)).also {
            pending = it
            pendingAt = lastNow
        }
    }

    fun writeCompleted(connection: Long, writeToken: Long, success: Boolean) {
        if (connection != generation || closed) return
        val active = pending ?: return
        if (writeToken != active.token) return
        guarded {
            if (!success) throw IOException("GATT write failed")
            offset += active.value.size
            outgoingBytes -= active.value.size
            active.value.fill(0)
            pending = null
            if (offset == outbound.peek().size) {
                outbound.remove().fill(0)
                offset = 0
            }
        }
    }

    fun disconnected(connection: Long) {
        if (connection == generation && !closed) abort("disconnected")
    }

    /** Monotonic deadlines: complete handshake in 30 s, each write in 5 s,
     * entire provisioning connection in 120 s. No traffic extends the window.
     */
    fun tick() {
        if (closed) return
        val now = nowMs()
        if (now < lastNow) { abort("clock_regressed"); return }
        lastNow = now
        if (now - startedAt >= 120_000) abort("session_timeout")
        else if (!channel.established && now - startedAt >= 30_000) abort("handshake_timeout")
        else if (pending != null && now - pendingAt >= 5_000) abort("write_timeout")
    }

    private fun guarded(action: () -> Unit) {
        tick()
        check(!closed) { "GATT session closed" }
        try { action() } catch (error: Exception) {
            abort("transport_error")
            throw error
        }
    }

    private fun abort(reason: String) {
        failure = reason
        close()
    }

    override fun close() {
        if (closed) return
        closed = true
        pending?.value?.fill(0)
        pending = null
        outbound.forEach { it.fill(0) }
        inbound.forEach { it.fill(0) }
        outbound.clear()
        inbound.clear()
        outgoingBytes = 0
        incomingBytes = 0
        channel.close()
    }

    companion object {
        private val generations = AtomicLong()
        private const val LIMIT = 64 * 1024
        private const val MAX_RX_PACKETS = 256
        private const val MAX_COMPATIBLE_ATT_WRITE_BYTES = 20
    }
}
