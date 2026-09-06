// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class ConsoleWireV1Test {
    @Test
    fun firmwareMutationHasStableVectorAndRoundTrips() {
        val mutation = otaMutation()
        val expected = """{"protocol":"console-v1","kind":"mutation","request_id":"request-1","device_id":"shaniu-test","generation":7,"expected_revision":3,"issued_at_ms":1000,"expires_at_ms":31000,"operation":"firmware.update","arguments":{"release_manifest_sha256":"$MANIFEST_SHA"}}"""

        assertEquals(expected, ConsoleWireV1.encodeMutation(mutation))
        assertEquals(mutation, ConsoleWireV1.decodeMutation(expected))
    }

    @Test
    fun stateSnapshotRoundTripsWithExplicitPermissions() {
        val envelope = ConsoleEventEnvelope(
            eventId = "event-1",
            deviceId = DEVICE,
            generation = 7,
            sequence = 9,
            occurredAtEpochMs = 2_000,
            event = ConsoleEvent.StateSnapshot(report()),
        )

        assertEquals(envelope, ConsoleWireV1.decodeEvent(ConsoleWireV1.encodeEvent(envelope)))
    }

    @Test
    fun reportedEmotionRoundTripsAndUnknownValueFailsClosed() {
        val envelope = ConsoleEventEnvelope(
            eventId = "emotion-1",
            deviceId = DEVICE,
            generation = 7,
            sequence = 10,
            occurredAtEpochMs = 2_100,
            event = ConsoleEvent.EmotionChanged(Emotion.HAPPY),
        )
        val encoded = ConsoleWireV1.encodeEvent(envelope)

        assertEquals(envelope, ConsoleWireV1.decodeEvent(encoded))
        assertWireFailure(ConsoleWireFailure.UNSUPPORTED_TYPE) {
            ConsoleWireV1.decodeEvent(encoded.replace("\"happy\"", "\"angry\""))
        }
    }

    @Test
    fun releaseCatalogCarriesOnlyVerifiedMetadata() {
        val catalog = FirmwareReleaseCatalog(
            deviceId = DEVICE,
            generation = 7,
            releases = listOf(release()),
        )

        assertEquals(
            catalog,
            ConsoleWireV1.decodeReleaseCatalog(ConsoleWireV1.encodeReleaseCatalog(catalog)),
        )
    }

    @Test
    fun acceptedReceiptCannotCarryAnError() {
        val invalid = """{"protocol":"console-v1","kind":"mutation.receipt","request_id":"request-1","device_id":"shaniu-test","generation":7,"revision":3,"status":"accepted","error":"revision_conflict"}"""

        assertWireFailure(ConsoleWireFailure.SCHEMA_MISMATCH) {
            ConsoleWireV1.decodeReceipt(invalid)
        }
    }

    @Test
    fun unknownFieldsAndMissingPermissionStatesFailClosed() {
        val mutationWithUrl = ConsoleWireV1.encodeMutation(otaMutation()).replace(
            "\"arguments\":{",
            "\"arguments\":{\"firmware_url\":\"https://invalid.example/image.bin\",",
        )
        assertWireFailure(ConsoleWireFailure.SCHEMA_MISMATCH) {
            ConsoleWireV1.decodeMutation(mutationWithUrl)
        }

        val snapshot = ConsoleWireV1.encodeEvent(
            ConsoleEventEnvelope(
                eventId = "event-1",
                deviceId = DEVICE,
                generation = 7,
                sequence = 1,
                occurredAtEpochMs = 2_000,
                event = ConsoleEvent.StateSnapshot(report()),
            ),
        ).replace("\"camera\":\"not_granted\",", "")
        assertWireFailure(ConsoleWireFailure.SCHEMA_MISMATCH) {
            ConsoleWireV1.decodeEvent(snapshot)
        }

        val snapshotWithoutEmotion = ConsoleWireV1.encodeEvent(
            ConsoleEventEnvelope(
                eventId = "event-2",
                deviceId = DEVICE,
                generation = 7,
                sequence = 2,
                occurredAtEpochMs = 2_001,
                event = ConsoleEvent.StateSnapshot(report()),
            ),
        ).replace("\"emotion\":\"neutral\",", "")
        assertWireFailure(ConsoleWireFailure.SCHEMA_MISMATCH) {
            ConsoleWireV1.decodeEvent(snapshotWithoutEmotion)
        }
    }

    @Test
    fun unsupportedVersionAndOversizeMessageHaveDistinctFailures() {
        val wrongVersion = ConsoleWireV1.encodeMutation(otaMutation()).replace(
            "\"console-v1\"",
            "\"console-v2\"",
        )
        assertWireFailure(ConsoleWireFailure.UNSUPPORTED_PROTOCOL) {
            ConsoleWireV1.decodeMutation(wrongVersion)
        }

        assertWireFailure(ConsoleWireFailure.MESSAGE_TOO_LARGE) {
            ConsoleWireV1.decodeEvent("x".repeat(ConsoleWireV1.MAX_MESSAGE_BYTES + 1))
        }
    }

    @Test
    fun forbiddenOperationHasNoWireRepresentation() {
        val forbidden = otaMutation().copy(
            operation = ConsoleOperation.RAW_SHELL,
            arguments = ConsoleMutationArguments.None,
        )

        assertWireFailure(ConsoleWireFailure.UNSUPPORTED_TYPE) {
            ConsoleWireV1.encodeMutation(forbidden)
        }
        val inbound = ConsoleWireV1.encodeMutation(otaMutation()).replace(
            "\"firmware.update\"",
            "\"raw_shell\"",
        )
        assertWireFailure(ConsoleWireFailure.UNSUPPORTED_TYPE) {
            ConsoleWireV1.decodeMutation(inbound)
        }
    }

    private fun assertWireFailure(expected: ConsoleWireFailure, block: () -> Unit) {
        val error = assertThrows(ConsoleWireException::class.java, block)
        assertEquals(expected, error.failure)
    }

    private fun otaMutation() = ConsoleMutation(
        requestId = "request-1",
        deviceId = DEVICE,
        generation = 7,
        expectedRevision = 3,
        issuedAtEpochMs = 1_000,
        expiresAtEpochMs = 31_000,
        operation = ConsoleOperation.REQUEST_FIRMWARE_UPDATE,
        arguments = ConsoleMutationArguments.FirmwareUpdate(MANIFEST_SHA),
    )

    private fun report() = DeviceReport(
        claimed = true,
        presence = Presence.ONLINE,
        gateway = GatewayConnection.ONLINE,
        batteryPercent = 78,
        charging = false,
        firmwareVersion = "18.6.149+209",
        revision = 3,
        turn = TurnPhase.IDLE,
        volumePercent = 55,
        personaMode = PersonaMode.GENTLE,
        emotion = Emotion.NEUTRAL,
        permissions = PrivacyCapability.entries.associateWith { PermissionState.NOT_GRANTED } +
            (PrivacyCapability.MICROPHONE to PermissionState.ALLOWED),
        update = UpdateState(),
    )

    private fun release() = FirmwareRelease(
        manifestSha256 = MANIFEST_SHA,
        targetVersion = "18.6.150+210",
        requiredSourceVersion = "18.6.149+209",
        requiredSourceRootSha256 = "b".repeat(64),
        boardFamily = "bk7258",
        physicalBoard = "aidk_ai_toy",
        layoutIdentity = "bk7258-aidk-ai-toy-16m",
        layoutSha256 = "c".repeat(64),
        packageSha256 = "d".repeat(64),
        packageSizeBytes = 2_457_600,
    )

    companion object {
        private const val DEVICE = "shaniu-test"
        private const val MANIFEST_SHA =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    }
}
