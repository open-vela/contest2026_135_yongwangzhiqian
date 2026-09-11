// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import com.google.gson.Gson
import java.nio.file.Files
import java.nio.file.Path
import java.security.KeyStore
import java.util.Base64
import java.util.concurrent.TimeUnit
import org.junit.AfterClass
import org.junit.BeforeClass
import org.junit.Test
import org.junit.Assert.*

class ProvisionActivationTest {
    companion object {
        private lateinit var directory: Path
        private lateinit var ca: ByteArray
        @JvmStatic @BeforeClass fun generateCa() {
            directory = Files.createTempDirectory("shaniu-activation-test-")
            val path = directory.resolve("ca.p12")
            val process = ProcessBuilder(
                Path.of(System.getProperty("java.home"), "bin", "keytool").toString(),
                "-genkeypair", "-alias", "ca", "-keyalg", "EC", "-groupname", "secp256r1",
                "-dname", "CN=activation-test", "-validity", "2", "-ext", "BC=ca:true",
                "-ext", "KU=keyCertSign", "-storetype", "PKCS12", "-keystore", path.toString(),
                "-storepass", "test-only-password", "-noprompt",
            ).redirectErrorStream(true).redirectOutput(directory.resolve("keytool.log").toFile()).start()
            if (!process.waitFor(20, TimeUnit.SECONDS)) { process.destroyForcibly(); fail("CA generation timed out") }
            assertEquals(0, process.exitValue())
            val store = KeyStore.getInstance("PKCS12").apply {
                Files.newInputStream(path).use { load(it, "test-only-password".toCharArray()) }
            }
            ca = store.getCertificate("ca").encoded
        }
        @JvmStatic @AfterClass fun cleanup() { if (::directory.isInitialized) directory.toFile().deleteRecursively() }
    }
    private fun values() = linkedMapOf(
        "protocol" to "provision-activation-v1", "device_id" to "test-device",
        "certificate_sha256" to "ab".repeat(32),
        "possession_secret" to Base64.getEncoder().encodeToString(ByteArray(32) { 42 }),
        "gateway_host" to "gateway.test", "gateway_ipv4" to "192.168.1.2", "gateway_port" to "8443",
        "gateway_ca_der" to Base64.getEncoder().encodeToString(ca),
    )
    private fun bootstrapValues() = linkedMapOf(
        "protocol" to "provision-bootstrap-v1", "device_id" to "test-device",
        "certificate_sha256" to "ab".repeat(32),
        "possession_secret" to Base64.getEncoder().encodeToString(ByteArray(32) { 42 }),
    )
    private fun reject(text: String) {
        try { ProvisionActivation.parse(text.toCharArray()).close(); fail("Accepted invalid activation") }
        catch (error: IllegalArgumentException) { assertEquals("Invalid device activation", error.message) }
    }
    @Test fun completePackageTransfersOnlyBootstrapOwnership() {
        val activation = ProvisionActivation.parse(Gson().toJson(values()).toCharArray())
        assertEquals("gateway.test", activation.host)
        assertEquals(8443, activation.port)
        assertArrayEquals(ca, activation.caCopy()!!)
        assertTrue(activation.hasLegacyRoute)
        assertEquals("ProvisionActivation(redacted)", activation.toString())
        val bootstrap = activation.takeBootstrap()
        activation.close()
        assertEquals("test-device", bootstrap.deviceId)
        bootstrap.usePossessionSecret { assertArrayEquals(ByteArray(32) { 42 }, it) }
        bootstrap.close()
    }
    @Test fun fourFieldBootstrapPackageIsNormalActivationWithoutGatewayRoute() {
        val activation = ProvisionActivation.parse(Gson().toJson(bootstrapValues()).toCharArray())
        assertFalse(activation.hasLegacyRoute)
        assertNull(activation.caCopy())
        assertNull(activation.host)
        assertNull(activation.address)
        assertNull(activation.port)
        activation.takeBootstrap().use { assertEquals("test-device", it.deviceId) }
        activation.close()
    }
    @Test fun rejectsDuplicateMissingUnknownAndTrailingFields() {
        val valid = Gson().toJson(values())
        reject(valid.dropLast(1) + ",\"device_id\":\"other\"}")
        reject(valid + "{}")
        reject(Gson().toJson(values().apply { remove("gateway_ca_der") }))
        reject(Gson().toJson(values().apply { put("device_key", "forbidden") }))
        reject(" ".repeat(ProvisionActivation.MAX_BYTES + 1))
    }
    @Test fun rejectsInvalidRoutesAndCertificates() {
        for ((field, value) in listOf("gateway_ipv4" to "127.0.0.1", "gateway_ipv4" to "192.168.01.2",
            "gateway_ipv4" to "224.0.0.1", "gateway_host" to "https://gateway.test", "gateway_port" to "0",
            "gateway_port" to "08443", "gateway_ca_der" to "YWJj",
            "gateway_ca_der" to Base64.getEncoder().encodeToString(ca + byteArrayOf(0)))) {
            reject(Gson().toJson(values().apply { put(field, value) }))
        }
    }
    @Test fun rejectsInvalidBootstrapAndMixedFields() {
        reject(Gson().toJson(bootstrapValues().apply { put("certificate_sha256", "zz".repeat(32)) }))
        reject(Gson().toJson(bootstrapValues().apply { put("possession_secret", "bad") }))
        reject(Gson().toJson(bootstrapValues().apply { put("gateway_host", "gateway.test") }))
        reject(Gson().toJson(bootstrapValues().apply { put("protocol", "provision-activation-v1") }))
    }
}
