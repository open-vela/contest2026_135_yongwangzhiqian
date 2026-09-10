// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class ConsoleWireV1Test {
    @Test
    fun updateFailuresDecodeWithoutBecomingSchemaErrors() {
        for (error in listOf(
            ConsoleErrorCode.UPDATE_MANIFEST_INVALID,
            ConsoleErrorCode.UPDATE_SIGNATURE_INVALID,
            ConsoleErrorCode.UPDATE_PAIR_MISMATCH,
            ConsoleErrorCode.UPDATE_TRIAL_FAILED,
            ConsoleErrorCode.UPDATE_DEVICE_ERROR,
        )) {
            val json = """{"protocol":"console-v1","kind":"mutation.receipt","request_id":"ota-1","device_id":"shaniu-test","generation":7,"revision":3,"status":"rejected","error":"${error.wireValue}"}"""
            val receipt = ConsoleWireV1.decodeReceipt(json)
            assertEquals(error, receipt.error)
            assertEquals(receipt, ConsoleWireV1.decodeReceipt(ConsoleWireV1.encodeReceipt(receipt)))
        }
    }

    @Test
    fun volumeFailuresDecodeWithoutBecomingSchemaErrors() {
        for (error in listOf(ConsoleErrorCode.VOLUME_TIMEOUT, ConsoleErrorCode.VOLUME_BUSY,
                             ConsoleErrorCode.VOLUME_DEVICE_ERROR, ConsoleErrorCode.VOLUME_NOT_SUPPORTED)) {
            val json = """{"protocol":"console-v1","kind":"mutation.receipt","request_id":"volume-1","device_id":"shaniu-test","generation":7,"revision":3,"status":"rejected","error":"${error.wireValue}"}"""
            val receipt = ConsoleWireV1.decodeReceipt(json)
            assertEquals(error, receipt.error)
            assertEquals(receipt, ConsoleWireV1.decodeReceipt(ConsoleWireV1.encodeReceipt(receipt)))
        }
    }

    @Test
    fun cancellationRemainsPendingUntilReportedIdle() {
        val store = CompanionStore(DEVICE)
        val pending = ConsoleEventEnvelope(
            eventId = "cancel-1", deviceId = DEVICE, generation = 7,
            sequence = 1, occurredAtEpochMs = 2000,
            event = ConsoleEvent.StateSnapshot(report().copy(turn = TurnPhase.CANCELLING)),
        )
        store.apply(ConsoleWireV1.decodeEvent(ConsoleWireV1.encodeEvent(pending)))
        assertEquals(TurnPhase.CANCELLING, store.state.turn)
        val confirmed = pending.copy(eventId = "cancel-2", sequence = 2,
                                    event = ConsoleEvent.StateSnapshot(report().copy(turn = TurnPhase.IDLE)))
        store.apply(ConsoleWireV1.decodeEvent(ConsoleWireV1.encodeEvent(confirmed)))
        assertEquals(TurnPhase.IDLE, store.state.turn)
    }

    @Test
    fun gatewayHttpsVectorsMatchAndroidContract() {
        fun fixture(name: String) = requireNotNull(javaClass.classLoader)
            .getResourceAsStream("console-v1/$name.json")!!.bufferedReader().use { it.readText() }
        val snapshot = ConsoleWireV1.decodeEvent(fixture("snapshot"))
        val report = (snapshot.event as ConsoleEvent.StateSnapshot).report
        assertEquals(null, report.volumePercent)
        assertEquals(null, report.charging)
        assertEquals(Emotion.UNKNOWN, report.emotion)
        assertEquals(UpdatePhase.UNKNOWN, report.update.phase)
        val mutation = ConsoleWireV1.decodeMutation(fixture("persona-mutation"))
        assertEquals(ConsoleOperation.SET_PERSONA_MODE, mutation.operation)
        assertEquals(ConsoleMutationArguments.SetPersonaMode(PersonaMode.QUIET), mutation.arguments)
        assertEquals(mutation, ConsoleWireV1.decodeMutation(ConsoleWireV1.encodeMutation(mutation)))
        val receipt = ConsoleWireV1.decodeReceipt(fixture("receipt"))
        assertEquals(MutationReceiptStatus.ACCEPTED, receipt.status)
        assertEquals(1L, receipt.revision)
    }

    @Test
    fun unknownDeviceValuesRoundTripWithoutInventingDefaults() {
        val envelope = ConsoleEventEnvelope(
            eventId = "unknown-1", deviceId = DEVICE, generation = 7,
            sequence = 1, occurredAtEpochMs = 2000,
            event = ConsoleEvent.StateSnapshot(report().copy(volumePercent = null, charging = null)),
        )
        val encoded = ConsoleWireV1.encodeEvent(envelope)
        assertEquals(envelope, ConsoleWireV1.decodeEvent(encoded))
        val store = CompanionStore(DEVICE)
        assertEquals(null, store.state.volumePercent)
        assertEquals(null, store.state.charging)
        assertEquals(EventDisposition.APPLIED, store.apply(ConsoleWireV1.decodeEvent(encoded)))
        assertEquals(null, store.state.volumePercent)
        assertEquals(null, store.state.charging)
        for (replacement in listOf("-1", "101", "true", "\"50\"")) {
            assertWireFailure(ConsoleWireFailure.SCHEMA_MISMATCH) {
                ConsoleWireV1.decodeEvent(encoded.replace("\"volume_percent\":null",
                                                         "\"volume_percent\":$replacement"))
            }
        }
        assertWireFailure(ConsoleWireFailure.SCHEMA_MISMATCH) {
            ConsoleWireV1.decodeEvent(encoded.replace("\"charging\":null", "\"charging\":0"))
        }
    }

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
    fun releaseCatalogEnforcesSharedGatewayBounds() {
        assertThrows(IllegalArgumentException::class.java) {
            release().copy(packageSizeBytes = 64L * 1024L * 1024L + 1L)
        }
        assertThrows(IllegalArgumentException::class.java) {
            FirmwareReleaseCatalog(
                deviceId = DEVICE,
                generation = 7,
                releases = listOf(release(), release().copy(manifestSha256 = "e".repeat(64))),
            )
        }
        assertThrows(IllegalArgumentException::class.java) {
            FirmwareReleaseCatalog(
                deviceId = DEVICE,
                generation = 7,
                releases = List(33) { index ->
                    release().copy(
                        manifestSha256 = index.toString(16).padStart(64, '0'),
                        targetVersion = "18.6.${150 + index}+${210 + index}",
                    )
                },
            )
        }
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
