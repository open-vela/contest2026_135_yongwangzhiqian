// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.gateway

import com.shaniu.companion.protocol.CompanionStore
import com.shaniu.companion.protocol.Emotion
import com.shaniu.companion.protocol.EventDisposition
import com.shaniu.companion.protocol.PersonaMode
import com.shaniu.companion.protocol.UpdatePhase
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class FakeConsoleGatewayTest {
    @Test
    fun otaNeedsTrialSnapshotBeforeConfirmation() {
        val gateway = FakeConsoleGateway(DEVICE) { 1_000 }
        val store = CompanionStore(DEVICE)
        assertEquals(EventDisposition.APPLIED, store.apply(gateway.connectSnapshot()))
        assertEquals(
            EventDisposition.APPLIED,
            store.apply(gateway.beginUpdate(FakeConsoleGateway.TARGET_RELEASE.manifestSha256)),
        )

        val phases = mutableListOf(store.state.update.phase)
        while (store.state.update.phase != UpdatePhase.CONFIRMED) {
            assertEquals(EventDisposition.APPLIED, store.apply(gateway.advanceUpdate()))
            phases += store.state.update.phase
        }

        assertEquals(
            listOf(
                UpdatePhase.DOWNLOADING,
                UpdatePhase.DOWNLOADING,
                UpdatePhase.DOWNLOADING,
                UpdatePhase.VERIFYING,
                UpdatePhase.STAGED,
                UpdatePhase.REBOOTING,
                UpdatePhase.TRIAL,
                UpdatePhase.CONFIRMED,
            ),
            phases,
        )
        assertEquals(2L, store.state.generation)
        assertEquals(FakeConsoleGateway.TARGET_VERSION, store.state.firmwareVersion)
        assertNull(store.state.update.error)
    }

    @Test
    fun failedUpdateCanOnlyClaimRollbackAfterNewSnapshot() {
        val gateway = FakeConsoleGateway(DEVICE) { 1_000 }
        val store = CompanionStore(DEVICE)
        store.apply(gateway.connectSnapshot())
        store.apply(gateway.beginUpdate(FakeConsoleGateway.TARGET_RELEASE.manifestSha256))
        store.apply(gateway.failUpdate())

        assertEquals(UpdatePhase.FAILED, store.state.update.phase)
        assertEquals(EventDisposition.APPLIED, store.apply(gateway.rollbackUpdate()))
        assertEquals(UpdatePhase.ROLLED_BACK, store.state.update.phase)
        assertEquals(2L, store.state.generation)
        assertEquals(FakeConsoleGateway.BASE_VERSION, store.state.firmwareVersion)
    }

    @Test
    fun preferencesAndEmotionChangeOnlyAfterReportedEvents() {
        val gateway = FakeConsoleGateway(DEVICE) { 1_000 }
        val store = CompanionStore(DEVICE)
        store.apply(gateway.connectSnapshot())

        assertEquals(
            EventDisposition.APPLIED,
            store.apply(gateway.setPreferences(volumePercent = 72, personaMode = PersonaMode.PLAYFUL)),
        )
        assertEquals(72, store.state.volumePercent)
        assertEquals(PersonaMode.PLAYFUL, store.state.personaMode)
        assertEquals(2L, store.state.revision)

        assertEquals(EventDisposition.APPLIED, store.apply(gateway.reportEmotion(Emotion.HAPPY)))
        assertEquals(Emotion.HAPPY, store.state.emotion)
        assertEquals(2L, store.state.revision)
    }

    companion object {
        private const val DEVICE = "shaniu-test"
    }
}
