// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.bluetooth.BluetoothDevice
import android.content.Context

/** Device selection/CONNECT permission precede this owner. It copies bootstrap
 * proof and configuration; callers may clear their inputs after construction.
 * State callbacks run on the transport worker and must be posted to UI.
 */
class ProvisioningConnection(
    context: Context,
    device: BluetoothDevice,
    bootstrap: ProvisionBootstrap,
    bundle: ByteArray,
    stateChanged: (ProvisionClaimProtocol.State) -> Unit,
    recover: Boolean = false,
) : AutoCloseable {
    private val binding = ProvisionBindingTransaction(
        ProvisionBindingStore(context.applicationContext),
        bootstrap.deviceId,
        recover,
    )
    private val recoveryTransaction = binding.recoveryTransaction()
    private val certificatePin = bootstrap.certificatePin()
    private val tls = bootstrap.newTls()
    private val protocol: ProvisionClaimProtocol =
        ProvisionClaimProtocol(bootstrap, bundle, ::send, { state ->
            stateChanged(binding.project(state))
        }, binding::beforeApply, recoveryTransaction,
            beforeApplyConfiguration = { transaction, configuration ->
                ProvisionSettings.useControlKey(configuration) { key ->
                    binding.beforeApply(transaction, key, if (key == null) null else certificatePin)
                }
            })
    private val transport: AndroidProvisionGatt = AndroidProvisionGatt(context, device, tls,
        object : AndroidProvisionGatt.Events {
            override fun tlsEstablished() = protocol.start()
            override fun plaintext(bytes: ByteArray) = protocol.receive(bytes)
            override fun closed(reason: String) {
                try { protocol.disconnected() } finally { protocol.close() }
            }
        })

    // Starting only after all fields are assigned removes constructor/worker
    // races when a fast callback attempts to send authentication.
    init { transport.start() }
    private fun send(bytes: ByteArray): Unit = transport.send(bytes)
    override fun close() = transport.close()
}
