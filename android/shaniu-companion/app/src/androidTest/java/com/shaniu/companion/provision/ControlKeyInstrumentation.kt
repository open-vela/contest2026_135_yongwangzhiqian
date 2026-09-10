// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.app.Activity
import android.app.Instrumentation
import android.content.Context
import android.os.Bundle
import com.shaniu.companion.ota.OtaSourceAcceptance
import com.shaniu.companion.ota.OtaSourceAcceptanceFailure
import java.security.KeyStore
import java.util.UUID

/** Real Android Keystore/preferences acceptance; no BLE or user data.
 * Optional cloud_probe=1 verifies public endpoint TLS only, without API credentials.
 * Each run creates and removes only its own uniquely named key and preferences.
 */
class ControlKeyInstrumentation : Instrumentation() {
    private var cloudProbe = false
    private var uiProbe = false
    private var otaSourcePackage: String? = null
    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments)
        cloudProbe = arguments?.getString("cloud_probe") == "1"
        uiProbe = arguments?.getString("ui_probe") == "1"
        otaSourcePackage = arguments?.getString("ota_source_package")
        start()
    }
    override fun onStart() {
        otaSourcePackage?.let { filename ->
            runOtaSourceProbe(filename)
            return
        }
        val name = "shaniu.control.test.${UUID.randomUUID()}"
        var result = Activity.RESULT_CANCELED
        var report = "FAIL: test did not finish"
        try {
            val preferences = targetContext.getSharedPreferences(name, Context.MODE_PRIVATE)
            val backend = ProvisionBindingStore.SharedPreferencesBackend(preferences)
            val device = "acceptance-device"
            val transaction = ByteArray(16) { (it + 1).toByte() }
            val control = ByteArray(32) { (it + 3).toByte() }
            val store = ProvisionBindingStore(backend, ControlKeyCipher.android(name))
            val receipt = store.begin(device, transaction, control, "42".repeat(32))
            check(store.boundDeviceId() == null)
            val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
            check(keyStore.getKey(name, null).encoded == null)
            val restarted = ProvisionBindingStore(
                ProvisionBindingStore.SharedPreferencesBackend(
                    targetContext.getSharedPreferences(name, Context.MODE_PRIVATE)),
                ControlKeyCipher.android(name))
            check(restarted.pending(device) == receipt)
            check(restarted.commit(device, receipt))
            var borrowed: ByteArray? = null
            var borrowedPin: ByteArray? = null
            restarted.useControlIdentity(device) { key, pin ->
                check(key.contentEquals(control)); borrowed = key
                check(pin.contentEquals(ByteArray(32) { 0x42 })); borrowedPin = pin
            }
            check(borrowed!!.all { it == 0.toByte() })
            check(borrowedPin!!.all { it == 0.toByte() })
            check(restarted.pending(device) == null)
            check(restarted.clearBound(device))
            check(!preferences.contains("@control"))
            control.fill(0)
            ControlTlsAcceptance.run("$name.tls")
            if (uiProbe) DeviceUiAcceptance.run(this)
            if (cloudProbe) {
                val endpoint = CloudEndpoint.resolve(CloudSettings.MIMO_TOKEN_PLAN_URL)
                check(endpoint.address.size == 4 && endpoint.caDer.size in 1..4096)
            }
            result = Activity.RESULT_OK
            report = "PASS: Android Keystore non-exportable key, encrypted pending recovery, authenticated certificate pin, plaintext wipe, Android provider TLS fragmentation and pin rejection"
            if (uiProbe) report += "; synthetic bound UI, memory control gates and Wi-Fi state"
            if (cloudProbe) report += "; cloud endpoint TLS 1.2 and system trust anchor"
        } catch (error: Exception) {
            report = "FAIL: " + generateSequence(error as Throwable) { it.cause }.take(5)
                .joinToString(" <- ") { "${it.javaClass.simpleName} at ${it.stackTrace.firstOrNull()}: ${it.message}" }
        } finally {
            try {
                targetContext.deleteSharedPreferences(name)
                KeyStore.getInstance("AndroidKeyStore").apply { load(null) }.deleteEntry(name)
            } catch (_: Exception) { result = Activity.RESULT_CANCELED; report = "FAIL: test cleanup" }
        }
        finish(result, Bundle().apply { putString("stream", report) })
    }

    private fun runOtaSourceProbe(filename: String) {
        var result = Activity.RESULT_CANCELED
        val report = try {
            val measured = OtaSourceAcceptance.run(targetContext, filename)
            result = Activity.RESULT_OK
            "PHONE_HTTPS_SOURCE_PASS requests=${measured.requests} bytes=${measured.bytes} " +
                "key_aliases=${measured.keyAliases} extract_dirs=${measured.extractDirectories} " +
                "catalog_sha256=${measured.catalogSha256} ap_sha256=${measured.apSha256} " +
                "cp_sha256=${measured.cpSha256} NOT_BOARD_OTA"
        } catch (error: OtaSourceAcceptanceFailure) {
            "PHONE_HTTPS_SOURCE_FAIL stage=${error.stage} class=${error.failureClass} NOT_BOARD_OTA"
        } catch (error: Throwable) {
            "PHONE_HTTPS_SOURCE_FAIL stage=runner class=${error.javaClass.simpleName} NOT_BOARD_OTA"
        }
        finish(result, Bundle().apply { putString("stream", report) })
    }
}
