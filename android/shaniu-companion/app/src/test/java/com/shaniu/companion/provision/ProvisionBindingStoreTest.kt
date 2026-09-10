// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Assert.assertArrayEquals
import javax.crypto.spec.SecretKeySpec
import org.junit.Test

class ProvisionBindingStoreTest {
    private class FakeBackend : ProvisionBindingStore.Backend {
        private val values = linkedMapOf<String, String>()
        var failNextCommit = false
        var commits = 0

        override fun contains(key: String) = values.containsKey(key)
        override fun getString(key: String) = values[key]
        override fun commit(changes: Map<String, String?>): Boolean {
            commits++
            if (failNextCommit) {
                failNextCommit = false
                return false
            }
            changes.forEach { (key, value) -> if (value == null) values.remove(key) else values[key] = value }
            return true
        }

        fun legacy(deviceId: String, transaction: String) { values[deviceId] = transaction }
        fun set(key: String, value: String) { values[key] = value }
    }

    /** Models Android's memory-before-disk commit, including failed writes. */
    private class MemoryFirstPreferences {
        val values = mutableMapOf<String, String>()
        var fail = false
        var throwOnCommit = false
        var commits = 0
        val preferences = java.lang.reflect.Proxy.newProxyInstance(
            android.content.SharedPreferences::class.java.classLoader,
            arrayOf(android.content.SharedPreferences::class.java),
        ) { proxy, method, args ->
            when (method.name) {
                "hashCode" -> System.identityHashCode(proxy)
                "equals" -> proxy === args!![0]
                "contains" -> values.containsKey(args!![0])
                "getString" -> values[args!![0]] ?: args[1]
                "edit" -> editor()
                else -> error("Unexpected preferences operation: ${method.name}")
            }
        } as android.content.SharedPreferences

        private fun editor(): android.content.SharedPreferences.Editor {
            val changes = mutableMapOf<String, String?>()
            return java.lang.reflect.Proxy.newProxyInstance(
                android.content.SharedPreferences.Editor::class.java.classLoader,
                arrayOf(android.content.SharedPreferences.Editor::class.java),
            ) { proxy, method, args ->
                when (method.name) {
                    "putString" -> { changes[args!![0] as String] = args[1] as String?; proxy }
                    "remove" -> { changes[args!![0] as String] = null; proxy }
                    "commit" -> {
                        commits++
                        changes.forEach { (k, v) -> if (v == null) values.remove(k) else values[k] = v }
                        if (throwOnCommit) throw IllegalStateException("Injected write failure")
                        !fail
                    }
                    else -> error("Unexpected editor operation: ${method.name}")
                }
            } as android.content.SharedPreferences.Editor
        }
    }

    @Test fun androidFailedWriteCannotPublishInMemoryBindingAcrossStoreInstances() {
        val memory = MemoryFirstPreferences()
        fun store() = ProvisionBindingStore(ProvisionBindingStore.SharedPreferencesBackend(memory.preferences))
        val first = store()
        val otherPage = store()
        first.begin(device, bytes(transaction))
        memory.fail = true
        assertFalse(first.commit(device, transaction))
        assertEquals(device, memory.values["@bound"]) // Android cache already changed.
        assertFalse(memory.values.containsKey("@pending:$device"))
        for (reader in listOf(first, otherPage, store())) {
            assertThrows(IllegalStateException::class.java) { reader.boundDeviceId() }
            assertThrows(IllegalStateException::class.java) { reader.pending(device) }
            assertThrows(IllegalStateException::class.java) { reader.begin(otherDevice, bytes(otherTransaction)) }
        }
        assertEquals(2, memory.commits)
        // A new process reloads the actual durable file, not the failed cache.
        val diskReload = MemoryFirstPreferences()
        diskReload.values["@pending:$device"] = transaction
        val restored = ProvisionBindingStore(ProvisionBindingStore.SharedPreferencesBackend(diskReload.preferences))
        assertNull(restored.boundDeviceId())
        assertEquals(transaction, restored.pending(device))
        assertTrue(restored.commit(device, transaction))
    }

    @Test fun androidCommitExceptionAlsoInvalidatesSharedReaders() {
        val memory = MemoryFirstPreferences()
        val backend = ProvisionBindingStore.SharedPreferencesBackend(memory.preferences)
        val reader = ProvisionBindingStore.SharedPreferencesBackend(memory.preferences)
        memory.throwOnCommit = true
        assertThrows(IllegalStateException::class.java) { backend.commit(mapOf("@bound" to device)) }
        assertThrows(IllegalStateException::class.java) { reader.getString("@bound") }
        assertThrows(IllegalStateException::class.java) { reader.commit(emptyMap()) }
    }

    private val device = "aidk-toy:42"
    private val otherDevice = "aidk-toy:43"
    private val transaction = "0123456789abcdef0123456789abcdef"
    private val otherTransaction = "fedcba9876543210fedcba9876543210"
    private fun cipher() = ControlKeyCipher { SecretKeySpec(ByteArray(32) { 7 }, "AES") }

    @Test fun dailyTrustSurvivesRecoveryAndCannotBeReplacedOrRemoved() {
        val backend = FakeBackend()
        val key = ByteArray(32) { 9 }
        val pin = "42".repeat(32)
        val store = ProvisionBindingStore(backend, cipher())
        val owner = ProvisionBindingTransaction(store, device, false)
        owner.beforeApply(bytes(transaction), key, pin)
        backend.set("@pending-pin:$device", "43".repeat(32))
        assertFalse(store.commit(device, transaction))
        backend.commit(mapOf("@pending-pin:$device" to null))
        assertFalse(store.commit(device, transaction))
        assertEquals(transaction, store.pending(device))
        backend.set("@pending-pin:$device", pin)
        val restored = ProvisionBindingStore(backend, cipher())
        val recovery = ProvisionBindingTransaction(restored, device, true)
        assertEquals(ProvisionClaimProtocol.State.COMMITTED,
            recovery.project(ProvisionClaimProtocol.State.COMMITTED))
        var borrowedKey: ByteArray? = null
        var borrowedPin: ByteArray? = null
        restored.useControlIdentity(device) { k, p ->
            assertArrayEquals(key, k)
            assertArrayEquals(ByteArray(32) { 0x42 }, p)
            borrowedKey = k; borrowedPin = p
        }
        assertTrue(borrowedKey!!.all { it == 0.toByte() })
        assertTrue(borrowedPin!!.all { it == 0.toByte() })
        backend.set("@control-pin", "43".repeat(32))
        assertThrows(Exception::class.java) {
            restored.useControlIdentity(device) { _, _ -> throw AssertionError("Untrusted pin escaped") }
        }
        backend.commit(mapOf("@control-pin" to null))
        assertThrows(Exception::class.java) { restored.useControlKey(device) { } }
        assertThrows(Exception::class.java) { restored.useControlIdentity(device) { _, _ -> } }
        backend.set("@control-pin", pin)
        assertTrue(restored.clearBound(device))
        assertNull(backend.getString("@control-pin"))
    }

    @Test fun encryptedControlKeySurvivesPendingRestartAndAtomicCommit() {
        val backend = FakeBackend()
        val key = ByteArray(32) { (it + 1).toByte() }
        val store = ProvisionBindingStore(backend, cipher())
        store.begin(device, bytes(transaction), key)
        assertThrows(Exception::class.java) { store.useControlKey(device) { } }
        val sealed = backend.getString("@pending-control:$device")!!
        assertEquals(80, sealed.length)
        backend.failNextCommit = true
        assertFalse(store.commit(device, transaction))
        assertEquals(sealed, backend.getString("@pending-control:$device"))
        val restarted = ProvisionBindingStore(backend, cipher())
        assertTrue(restarted.commit(device, transaction))
        var borrowed: ByteArray? = null
        restarted.useControlKey(device) { assertArrayEquals(key, it); borrowed = it }
        assertTrue(borrowed!!.all { it == 0.toByte() })
        assertThrows(IllegalStateException::class.java) {
            restarted.useControlKey(device) { borrowed = it; error("test callback") }
        }
        assertTrue(borrowed!!.all { it == 0.toByte() })
        assertThrows(Exception::class.java) { restarted.useControlKey(otherDevice) { } }
        assertTrue(restarted.clearBound(device))
        assertNull(backend.getString("@control"))
    }

    @Test fun pendingControlIdentityIsExactTransactionScopedAndWiped() {
        val backend = FakeBackend()
        val key = ByteArray(32) { (it + 3).toByte() }
        val pin = "42".repeat(32)
        val store = ProvisionBindingStore(backend, cipher())
        store.begin(device, bytes(transaction), key, pin)
        var borrowedKey: ByteArray? = null
        var borrowedPin: ByteArray? = null
        store.usePendingControlIdentity(device, transaction) { pendingKey, pendingPin ->
            assertArrayEquals(key, pendingKey)
            assertArrayEquals(ByteArray(32) { 0x42 }, pendingPin)
            borrowedKey = pendingKey; borrowedPin = pendingPin
        }
        assertTrue(borrowedKey!!.all { it == 0.toByte() })
        assertTrue(borrowedPin!!.all { it == 0.toByte() })
        assertThrows(IllegalArgumentException::class.java) {
            store.usePendingControlIdentity(device, otherTransaction) { _, _ -> }
        }
        assertTrue(store.commit(device, transaction))
        assertThrows(Exception::class.java) {
            store.usePendingControlIdentity(device, transaction) { _, _ -> }
        }
    }

    @Test fun missingOrSubstitutedEncryptedControlKeyCannotDowngradeToLegacyClaim() {
        val backend = FakeBackend()
        val store = ProvisionBindingStore(backend, cipher())
        store.begin(device, bytes(transaction), ByteArray(32) { 1 })
        val sealed = backend.getString("@pending-control:$device")!!
        backend.commit(mapOf("@pending-control:$device" to null))
        assertFalse(store.commit(device, transaction))
        store.begin(otherDevice, bytes(otherTransaction), ByteArray(32) { 2 })
        backend.set("@pending-control:$device", backend.getString("@pending-control:$otherDevice")!!)
        assertFalse(store.commit(device, transaction))
        backend.set("@pending-control:$device", sealed)
        assertTrue(store.commit(device, transaction))
        assertTrue(store.notCommitted(otherDevice, otherTransaction))
        assertNull(backend.getString("@pending-control:$otherDevice"))
    }

    @Test fun controlCipherRejectsTamperingAndInvalidKeys() {
        val cipher = cipher()
        assertThrows(IllegalArgumentException::class.java) { cipher.seal("scope", ByteArray(32)) }
        val sealed = cipher.seal("scope", ByteArray(32) { 1 })
        assertThrows(Exception::class.java) { cipher.use("other", sealed) { } }
        val changed = (if (sealed[0] == 'A') 'B' else 'A') + sealed.substring(1)
        assertThrows(Exception::class.java) { cipher.use("scope", changed) { } }
        assertThrows(Exception::class.java) { cipher.use("scope", sealed.dropLast(1)) { } }
    }

    @Test fun committedTransactionAtomicallyBindsAndConsumesReceipt() {
        val backend = FakeBackend()
        val store = ProvisionBindingStore(backend)
        assertEquals(transaction, store.begin(device, bytes(transaction)))

        assertTrue(store.commit(device, transaction))
        assertEquals(device, store.boundDeviceId())
        assertNull(store.pending(device))
    }

    @Test fun failedCommitRetainsPendingAndDoesNotPublishBinding() {
        val backend = FakeBackend()
        val store = ProvisionBindingStore(backend)
        store.begin(device, bytes(transaction))
        backend.failNextCommit = true

        assertFalse(store.commit(device, transaction))
        assertNull(store.boundDeviceId())
        assertEquals(transaction, store.pending(device))
    }

    @Test fun unconfirmedSurvivesRestartWithoutChangingBinding() {
        val backend = FakeBackend()
        ProvisionBindingStore(backend).begin(device, bytes(transaction))
        val afterRestart = ProvisionBindingStore(backend)

        assertTrue(afterRestart.unconfirmed(device, transaction))
        assertEquals(transaction, afterRestart.pending(device))
        assertNull(afterRestart.boundDeviceId())
    }

    @Test fun notCommittedAndConfirmedFailureClearOnlyMatchingReceipt() {
        val backend = FakeBackend()
        val store = ProvisionBindingStore(backend)
        store.begin(device, bytes(transaction))
        assertTrue(store.notCommitted(device, transaction))
        assertNull(store.pending(device))

        store.begin(device, bytes(transaction))
        assertTrue(store.confirmedFailure(device, transaction))
        assertNull(store.pending(device))
    }

    @Test fun wrongDeviceOrTransactionFailsClosed() {
        val backend = FakeBackend()
        val store = ProvisionBindingStore(backend)
        store.begin(device, bytes(transaction))

        assertFalse(store.commit(otherDevice, transaction))
        assertFalse(store.commit(device, otherTransaction))
        assertFalse(store.notCommitted(otherDevice, transaction))
        assertFalse(store.confirmedFailure(device, otherTransaction))
        assertFalse(store.unconfirmed(device, otherTransaction))
        assertEquals(transaction, store.pending(device))
        assertNull(store.boundDeviceId())
    }

    @Test fun laterCommitReplacesExistingBoundDevice() {
        val backend = FakeBackend()
        val store = ProvisionBindingStore(backend)
        store.begin(device, bytes(transaction))
        assertTrue(store.commit(device, transaction))
        store.begin(otherDevice, bytes(otherTransaction))

        assertTrue(store.commit(otherDevice, otherTransaction))
        assertEquals(otherDevice, store.boundDeviceId())
        assertNull(store.pending(otherDevice))
    }

    @Test fun legacyPendingReceiptIsReadAndMigratedByCommit() {
        val backend = FakeBackend()
        backend.legacy(device, transaction)
        val store = ProvisionBindingStore(backend)

        assertEquals(transaction, store.pending(device))
        assertTrue(store.commit(device, transaction))
        assertEquals(device, store.boundDeviceId())
        assertNull(store.pending(device))
    }

    @Test fun clearBoundRequiresMatchingDeviceAndRetainsPendingRecovery() {
        val backend = FakeBackend()
        val store = ProvisionBindingStore(backend)
        store.begin(device, bytes(transaction))
        assertTrue(store.commit(device, transaction))
        store.begin(otherDevice, bytes(otherTransaction))

        assertFalse(store.clearBound(otherDevice))
        assertTrue(store.clearBound(device))
        assertNull(store.boundDeviceId())
        assertEquals(otherTransaction, store.pending(otherDevice))
    }

    @Test fun failedClearLeavesAuthoritativeBindingIntact() {
        val backend = FakeBackend()
        val store = ProvisionBindingStore(backend)
        store.begin(device, bytes(transaction))
        assertTrue(store.commit(device, transaction))
        backend.failNextCommit = true

        assertFalse(store.clearBound(device))
        assertEquals(device, store.boundDeviceId())
    }

    @Test fun corruptDurableValuesCannotBeSilentlyOverwritten() {
        val corruptPending = FakeBackend().also { it.set("@pending:$device", "bad") }
        val pendingStore = ProvisionBindingStore(corruptPending)
        assertThrows(IllegalStateException::class.java) { pendingStore.pending(device) }
        assertThrows(IllegalStateException::class.java) {
            pendingStore.begin(device, bytes(transaction))
        }

        val corruptBound = FakeBackend().also { it.set("@bound", "not a device") }
        assertThrows(IllegalStateException::class.java) {
            ProvisionBindingStore(corruptBound).boundDeviceId()
        }
    }

    @Test fun committedStateIsPublishedOnlyAfterAtomicBindingCommit() {
        val backend = FakeBackend()
        val store = ProvisionBindingStore(backend)
        val session = ProvisionBindingTransaction(store, device, recover = false)
        session.beforeApply(bytes(transaction))
        backend.failNextCommit = true

        assertEquals(
            ProvisionClaimProtocol.State.UNCONFIRMED,
            session.project(ProvisionClaimProtocol.State.COMMITTED),
        )
        assertEquals(transaction, store.pending(device))
        assertNull(store.boundDeviceId())

        val recovery = ProvisionBindingTransaction(store, device, recover = true)
        assertEquals(
            ProvisionClaimProtocol.State.COMMITTED,
            recovery.project(ProvisionClaimProtocol.State.COMMITTED),
        )
        assertEquals(device, store.boundDeviceId())
        assertNull(store.pending(device))
    }

    @Test fun failedReconciliationRetainsReceiptAsUnconfirmed() {
        val backend = FakeBackend()
        val store = ProvisionBindingStore(backend)
        store.begin(device, bytes(transaction))
        val recovery = ProvisionBindingTransaction(store, device, recover = true)

        assertEquals(
            ProvisionClaimProtocol.State.UNCONFIRMED,
            recovery.project(ProvisionClaimProtocol.State.FAILED),
        )
        assertEquals(transaction, store.pending(device))
    }

    @Test fun verifiedFailureClearsReceiptOnlyWhenItsWriteSucceeds() {
        val backend = FakeBackend()
        val store = ProvisionBindingStore(backend)
        val session = ProvisionBindingTransaction(store, device, recover = false)
        session.beforeApply(bytes(transaction))
        backend.failNextCommit = true

        assertEquals(
            ProvisionClaimProtocol.State.UNCONFIRMED,
            session.project(ProvisionClaimProtocol.State.FAILED),
        )
        assertEquals(transaction, store.pending(device))
    }

    private fun bytes(hex: String): ByteArray = ByteArray(hex.length / 2) {
        hex.substring(it * 2, it * 2 + 2).toInt(16).toByte()
    }
}
