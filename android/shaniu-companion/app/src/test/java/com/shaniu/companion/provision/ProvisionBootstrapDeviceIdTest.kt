// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.provision

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ProvisionBootstrapDeviceIdTest {
    @Test fun acceptsTheSamePublicDeviceLocatorFormatAsBootstrapParsing() {
        assertEquals("aidk-toy:42", ProvisionBootstrap.validDeviceId("aidk-toy:42"))
        assertEquals("A._-:9", ProvisionBootstrap.validDeviceId("A._-:9"))
    }

    @Test fun rejectsMalformedOrOversizedActivityResults() {
        assertNull(ProvisionBootstrap.validDeviceId(null))
        assertNull(ProvisionBootstrap.validDeviceId("../other-device"))
        assertNull(ProvisionBootstrap.validDeviceId("x".repeat(129)))
    }
}
