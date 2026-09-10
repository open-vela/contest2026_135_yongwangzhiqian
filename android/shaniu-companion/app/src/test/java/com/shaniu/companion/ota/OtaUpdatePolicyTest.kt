// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class OtaUpdatePolicyTest {
    @Test fun confirmsOnlyFirmwareConfirmedPhase() {
        assertTrue(OtaUpdatePolicy.confirmed("device", "device", "1.2.3+4", 9,
            "1.2.3+4", 9, 3, OtaUpdatePolicy.CONFIRMED_PHASE, 0))
        assertFalse(OtaUpdatePolicy.confirmed("device", "device", "1.2.3+4", 9,
            "1.2.3+4", 9, 3, 7, 0))
        assertFalse(OtaUpdatePolicy.confirmed("device", "other", "1.2.3+4", 9,
            "1.2.3+4", 9, 3, OtaUpdatePolicy.CONFIRMED_PHASE, 0))
    }

    @Test fun gateRejectsDoubleStartAndFailedPersistence() {
        val gate = OtaStartGate()
        assertFalse(gate.begin { false })
        assertTrue(gate.begin { true })
        assertFalse(gate.begin { true })
        gate.release()
        assertTrue(gate.begin { true })
    }
}
