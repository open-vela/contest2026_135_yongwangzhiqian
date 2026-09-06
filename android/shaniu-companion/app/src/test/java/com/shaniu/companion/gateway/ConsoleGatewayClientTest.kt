// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.gateway

import com.shaniu.companion.protocol.ConsoleEvent
import com.shaniu.companion.protocol.ConsoleEventEnvelope
import com.shaniu.companion.protocol.ConsoleMutation
import com.shaniu.companion.protocol.ConsoleMutationArguments
import com.shaniu.companion.protocol.ConsoleMutationReceipt
import com.shaniu.companion.protocol.ConsoleOperation
import com.shaniu.companion.protocol.ConsoleWireV1
import com.shaniu.companion.protocol.DeviceReport
import com.shaniu.companion.protocol.Emotion
import com.shaniu.companion.protocol.FirmwareRelease
import com.shaniu.companion.protocol.FirmwareReleaseCatalog
import com.shaniu.companion.protocol.GatewayConnection
import com.shaniu.companion.protocol.MutationReceiptStatus
import com.shaniu.companion.protocol.PermissionState
import com.shaniu.companion.protocol.PersonaMode
import com.shaniu.companion.protocol.Presence
import com.shaniu.companion.protocol.PrivacyCapability
import com.shaniu.companion.protocol.TurnPhase
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.IOException

class ConsoleGatewayClientTest {
    @Test
    fun originAcceptsOnlyCredentialFreeHttpsRoot() {
        assertEquals("https://gateway.example/", GatewayOrigin.parse("https://gateway.example").toString())
        listOf(
            "http://gateway.example",
            "https://token@gateway.example",
            "https://gateway.example/private",
        ).forEach { value ->
            assertThrows(IllegalArgumentException::class.java) { GatewayOrigin.parse(value) }
        }
    }

    @Test
    fun snapshotRequestIsBoundToDeviceAndContainsNoCredential() {
        val transport = RecordingTransport(GatewayHttpResponse(200, ConsoleWireV1.encodeEvent(snapshot())))
        val result = client(transport).fetchSnapshot(DEVICE)

        assertTrue(result is GatewayCallResult.Success)
        val request = transport.requests.single()
        assertEquals(GatewayHttpMethod.GET, request.method)
        assertEquals(
            "https://gateway.example/console/v1/devices/shaniu-test/snapshot",
            request.uri.toString(),
        )
        assertFalse(request.headers.keys.any { it.equals("Authorization", ignoreCase = true) })
        assertEquals(null, request.body)
    }

    @Test
    fun callersCannotOverrideCredentialOrRoutingHeaders() {
        listOf("Authorization", "Proxy-Authorization", "Cookie", "Host").forEach { name ->
            assertThrows(IllegalArgumentException::class.java) {
                GatewayHttpRequest(
                    method = GatewayHttpMethod.GET,
                    uri = java.net.URI("https://gateway.example/test"),
                    headers = mapOf(name to "caller-controlled"),
                )
            }
        }
    }

    @Test
    fun releaseCatalogMustMatchRequestedGeneration() {
        val mismatched = FirmwareReleaseCatalog(
            deviceId = DEVICE,
            generation = 8,
            releases = listOf(release()),
        )
        val transport = RecordingTransport(
            GatewayHttpResponse(200, ConsoleWireV1.encodeReleaseCatalog(mismatched)),
        )

        val result = client(transport).fetchFirmwareReleases(DEVICE, generation = 7)

        assertFailure(GatewayFailureReason.PROTOCOL_ERROR, retryable = false, result)
    }

    @Test
    fun otaMutationUsesManifestReferenceAndVerifiesReceiptIdentity() {
        val mutation = otaMutation()
        val receipt = ConsoleMutationReceipt(
            requestId = mutation.requestId,
            deviceId = mutation.deviceId,
            generation = mutation.generation,
            revision = 4,
            status = MutationReceiptStatus.ACCEPTED,
        )
        val transport = RecordingTransport(
            GatewayHttpResponse(202, ConsoleWireV1.encodeReceipt(receipt)),
        )

        val result = client(transport).submitMutation(mutation)

        assertEquals(GatewayCallResult.Success(receipt), result)
        val request = transport.requests.single()
        assertEquals(GatewayHttpMethod.POST, request.method)
        assertEquals(ConsoleGatewayClient.MEDIA_TYPE, request.headers["Content-Type"])
        assertEquals(mutation, ConsoleWireV1.decodeMutation(requireNotNull(request.body)))
        assertFalse(request.body!!.contains("http"))
    }

    @Test
    fun transportAndAuthorizationFailuresStayContentFree() {
        val unavailable = client(RecordingTransport(error = IOException("private detail")))
            .fetchSnapshot(DEVICE)
        assertFailure(GatewayFailureReason.TRANSPORT_UNAVAILABLE, retryable = true, unavailable)

        val revoked = client(RecordingTransport(GatewayHttpResponse(401, "private body")))
            .fetchSnapshot(DEVICE)
        assertFailure(GatewayFailureReason.AUTHORIZATION_REVOKED, retryable = false, revoked)

        val incompatible = client(RecordingTransport(GatewayHttpResponse(426, "private body")))
            .fetchSnapshot(DEVICE)
        assertFailure(GatewayFailureReason.PROTOCOL_ERROR, retryable = false, incompatible)
    }

    @Test
    fun credentialFailureIsNotCollapsedIntoRetryableNetworkFailure() {
        val missing = client(
            RecordingTransport(
                error = GatewayTransportException(
                    GatewayFailureReason.CREDENTIALS_UNAVAILABLE,
                    retryable = false,
                ),
            ),
        ).fetchSnapshot(DEVICE)

        assertFailure(GatewayFailureReason.CREDENTIALS_UNAVAILABLE, retryable = false, missing)
    }

    @Test
    fun eventCursorUsesWssAndGenerationSequencePreconditions() {
        assertEquals(
            "wss://gateway.example/console/v1/devices/shaniu-test/events" +
                "?generation=7&after_sequence=12",
            client(RecordingTransport()).eventStreamUri(DEVICE, 7, 12).toString(),
        )
    }

    private fun assertFailure(
        expected: GatewayFailureReason,
        retryable: Boolean,
        result: GatewayCallResult<*>,
    ) {
        val failure = result as GatewayCallResult.Failure
        assertEquals(expected, failure.reason)
        assertEquals(retryable, failure.retryable)
    }

    private fun client(transport: ConsoleGatewayTransport) = ConsoleGatewayClient(
        GatewayOrigin.parse("https://gateway.example"),
        transport,
    )

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

    private fun snapshot() = ConsoleEventEnvelope(
        eventId = "event-1",
        deviceId = DEVICE,
        generation = 7,
        sequence = 1,
        occurredAtEpochMs = 1_000,
        event = ConsoleEvent.StateSnapshot(
            DeviceReport(
                claimed = true,
                presence = Presence.ONLINE,
                gateway = GatewayConnection.ONLINE,
                batteryPercent = 80,
                charging = false,
                firmwareVersion = "18.6.149+209",
                revision = 3,
                turn = TurnPhase.IDLE,
                volumePercent = 50,
                personaMode = PersonaMode.GENTLE,
                emotion = Emotion.NEUTRAL,
                permissions = PrivacyCapability.entries.associateWith {
                    PermissionState.NOT_GRANTED
                },
            ),
        ),
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

    private class RecordingTransport(
        private val response: GatewayHttpResponse? = null,
        private val error: IOException? = null,
    ) : ConsoleGatewayTransport {
        val requests = mutableListOf<GatewayHttpRequest>()

        override fun execute(request: GatewayHttpRequest): GatewayHttpResponse {
            requests += request
            error?.let { throw it }
            return requireNotNull(response)
        }
    }

    companion object {
        private const val DEVICE = "shaniu-test"
        private const val MANIFEST_SHA =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    }
}
