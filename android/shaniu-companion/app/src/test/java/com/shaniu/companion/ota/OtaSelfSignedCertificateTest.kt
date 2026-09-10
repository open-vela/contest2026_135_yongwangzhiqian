// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayInputStream
import java.math.BigInteger
import java.security.KeyPairGenerator
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import java.security.spec.ECGenParameterSpec
import java.util.Date

class OtaSelfSignedCertificateTest {
    @Test
    fun v3CertificateHasDnsSanAndVerifiesItsSignature() {
        val pair = KeyPairGenerator.getInstance("EC").apply {
            initialize(ECGenParameterSpec("secp256r1"))
        }.generateKeyPair()
        val now = System.currentTimeMillis()
        val certificate = OtaSelfSignedCertificate.create(
            pair.public,
            pair.private,
            "shaniu-update.local",
            BigInteger.valueOf(413),
            Date(now - 60_000),
            Date(now + 60_000)
        )
        val parsed = CertificateFactory.getInstance("X.509")
            .generateCertificate(ByteArrayInputStream(certificate.encoded)) as X509Certificate

        assertEquals(3, parsed.version)
        assertEquals("SHA256withECDSA", parsed.sigAlgName)
        assertTrue(OtaSelfSignedCertificate.hasDnsName(parsed, "shaniu-update.local"))
        assertFalse(OtaSelfSignedCertificate.hasDnsName(parsed, "wrong.invalid"))
        parsed.verify(pair.public)
        parsed.checkValidity(Date(now))
    }
}
