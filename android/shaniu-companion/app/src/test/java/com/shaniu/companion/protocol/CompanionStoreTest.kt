// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CompanionStoreTest {
    @Test
    fun duplicateEventIsIdempotent() {
        val store = connectedStore()
        val turn = envelope(sequence = 2, event = ConsoleEvent.TurnChanged(TurnPhase.LISTENING))

        assertEquals(EventDisposition.APPLIED, store.apply(turn))
        assertEquals(EventDisposition.DUPLICATE, store.apply(turn))
        assertEquals(TurnPhase.LISTENING, store.state.turn)
        assertEquals(2L, store.state.lastSequence)
    }

    @Test
    fun eventGapFailsClosedUntilSnapshotRecovery() {
        val store = connectedStore()

        assertEquals(
            EventDisposition.NEEDS_SNAPSHOT,
            store.apply(envelope(sequence = 3, event = ConsoleEvent.TurnChanged(TurnPhase.SPEAKING))),
        )
        assertTrue(store.state.needsSnapshot)
        assertEquals(TurnPhase.IDLE, store.state.turn)
        assertEquals(GatewayConnection.SYNCING, store.state.gateway)

        assertEquals(
            EventDisposition.NEEDS_SNAPSHOT,
            store.apply(envelope(sequence = 4, event = ConsoleEvent.TurnChanged(TurnPhase.THINKING))),
        )
        assertEquals(
            EventDisposition.APPLIED,
            store.apply(
                envelope(
                    sequence = 5,
                    event = ConsoleEvent.StateSnapshot(report(turn = TurnPhase.THINKING, revision = 4)),
                ),
            ),
        )
        assertFalse(store.state.needsSnapshot)
        assertEquals(TurnPhase.THINKING, store.state.turn)
        assertEquals(4L, store.state.revision)
    }

    @Test
    fun higherGenerationNeedsSnapshotAndOldGenerationIsRejected() {
        val store = connectedStore()

        assertEquals(
            EventDisposition.NEEDS_SNAPSHOT,
            store.apply(
                envelope(
                    generation = 2,
                    sequence = 1,
                    event = ConsoleEvent.TurnChanged(TurnPhase.LISTENING),
                ),
            ),
        )
        assertEquals(
            EventDisposition.APPLIED,
            store.apply(
                envelope(
                    generation = 2,
                    sequence = 2,
                    event = ConsoleEvent.StateSnapshot(report(turn = TurnPhase.IDLE)),
                ),
            ),
        )
        assertEquals(2L, store.state.generation)
        assertEquals(
            EventDisposition.STALE_GENERATION,
            store.apply(
                envelope(
                    generation = 1,
                    sequence = 9,
                    event = ConsoleEvent.TurnChanged(TurnPhase.ERROR),
                ),
            ),
        )
        assertEquals(TurnPhase.IDLE, store.state.turn)
    }

    @Test
    fun wrongDeviceAndProtocolNeverChangeState() {
        val store = connectedStore()
        val before = store.state

        assertEquals(
            EventDisposition.WRONG_DEVICE,
            store.apply(
                envelope(
                    deviceId = "other",
                    sequence = 2,
                    event = ConsoleEvent.TurnChanged(TurnPhase.ERROR),
                ),
            ),
        )
        assertEquals(
            EventDisposition.UNSUPPORTED_PROTOCOL,
            store.apply(
                envelope(
                    protocol = "console-v2",
                    sequence = 2,
                    event = ConsoleEvent.TurnChanged(TurnPhase.ERROR),
                ),
            ),
        )
        assertEquals(before, store.state)
    }

    @Test
    fun otaStateIsReportedWithoutInventingSuccess() {
        val store = connectedStore()
        val downloading = UpdateState(
            phase = UpdatePhase.DOWNLOADING,
            targetVersion = "18.6.150+210",
            progressPercent = 35,
        )
        assertEquals(
            EventDisposition.APPLIED,
            store.apply(envelope(sequence = 2, event = ConsoleEvent.UpdateChanged(downloading))),
        )
        assertEquals(UpdatePhase.DOWNLOADING, store.state.update.phase)

        val failure = downloading.copy(
            phase = UpdatePhase.FAILED,
            error = ConsoleErrorCode.UPDATE_SIGNATURE_INVALID,
        )
        assertEquals(
            EventDisposition.APPLIED,
            store.apply(envelope(sequence = 3, event = ConsoleEvent.UpdateChanged(failure))),
        )
        assertEquals(UpdatePhase.FAILED, store.state.update.phase)
        assertEquals(ConsoleErrorCode.UPDATE_SIGNATURE_INVALID, store.state.update.error)
        assertEquals("fake", store.state.firmwareVersion)
    }

    @Test
    fun reportedEmotionDoesNotChangePersonaOrRevision() {
        val store = connectedStore()

        assertEquals(
            EventDisposition.APPLIED,
            store.apply(envelope(sequence = 2, event = ConsoleEvent.EmotionChanged(Emotion.SHY))),
        )
        assertEquals(Emotion.SHY, store.state.emotion)
        assertEquals(PersonaMode.GENTLE, store.state.personaMode)
        assertEquals(1L, store.state.revision)
    }

    @Test
    fun staleReportedPreferenceFailsClosedUntilSnapshot() {
        val store = connectedStore()
        val stale = ConsoleEvent.SettingsChanged(
            revision = 1,
            volumePercent = 80,
            personaMode = PersonaMode.PLAYFUL,
        )

        assertEquals(
            EventDisposition.NEEDS_SNAPSHOT,
            store.apply(envelope(sequence = 2, event = stale)),
        )
        assertEquals(50, store.state.volumePercent)
        assertEquals(PersonaMode.GENTLE, store.state.personaMode)
        assertEquals(ConsoleErrorCode.REVISION_CONFLICT, store.state.lastError)
        assertTrue(store.state.needsSnapshot)
    }

    private fun connectedStore(): CompanionStore = CompanionStore(DEVICE).also { store ->
        assertEquals(
            EventDisposition.APPLIED,
            store.apply(envelope(sequence = 1, event = ConsoleEvent.StateSnapshot(report()))),
        )
    }

    private fun envelope(
        protocol: String = CONSOLE_V1,
        deviceId: String = DEVICE,
        generation: Long = 1,
        sequence: Long,
        event: ConsoleEvent,
    ) = ConsoleEventEnvelope(
        protocol = protocol,
        eventId = "event-$generation-$sequence",
        deviceId = deviceId,
        generation = generation,
        sequence = sequence,
        occurredAtEpochMs = 1_000,
        event = event,
    )

    private fun report(
        turn: TurnPhase = TurnPhase.IDLE,
        revision: Long = 1,
    ) = DeviceReport(
        claimed = true,
        presence = Presence.ONLINE,
        gateway = GatewayConnection.ONLINE,
        batteryPercent = 80,
        charging = false,
        firmwareVersion = "fake",
        revision = revision,
        turn = turn,
        volumePercent = 50,
        personaMode = PersonaMode.GENTLE,
        emotion = Emotion.NEUTRAL,
        permissions = mapOf(PrivacyCapability.MICROPHONE to PermissionState.ALLOWED),
    )

    companion object {
        private const val DEVICE = "shaniu-test"
    }
}
