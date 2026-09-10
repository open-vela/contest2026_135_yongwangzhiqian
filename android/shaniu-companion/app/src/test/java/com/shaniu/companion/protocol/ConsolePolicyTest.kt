// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Test

class ConsolePolicyTest {
    @Test
    fun validPreferenceMutationIsAccepted() {
        assertSame(
            MutationDecision.Accepted,
            ConsolePolicy.evaluate(
                mutation(operation = ConsoleOperation.SET_PERSONA_MODE),
                context(grantedLevels = setOf(PermissionLevel.L1_PREFERENCE)),
            ),
        )
    }

    @Test
    fun expiredRequestIsRejectedBeforeExecution() {
        assertRejected(
            ConsoleErrorCode.REQUEST_EXPIRED,
            mutation(operation = ConsoleOperation.START_REMOTE_TURN, expiresAtEpochMs = NOW),
            context(grantedLevels = setOf(PermissionLevel.L2_PRIVACY)),
        )
    }

    @Test
    fun missingPrivacyPermissionIsRejected() {
        assertRejected(
            ConsoleErrorCode.PERMISSION_DENIED,
            mutation(operation = ConsoleOperation.REQUEST_SNAPSHOT),
            context(grantedLevels = setOf(PermissionLevel.L1_PREFERENCE)),
        )
    }

    @Test
    fun staleRevisionAndGenerationAreRejected() {
        assertRejected(
            ConsoleErrorCode.STALE_GENERATION,
            mutation(operation = ConsoleOperation.SET_VOLUME, generation = 6),
            context(grantedLevels = setOf(PermissionLevel.L1_PREFERENCE)),
        )
        assertRejected(
            ConsoleErrorCode.REVISION_CONFLICT,
            mutation(operation = ConsoleOperation.SET_VOLUME, expectedRevision = 8),
            context(grantedLevels = setOf(PermissionLevel.L1_PREFERENCE)),
        )
    }

    @Test
    fun adminMutationsRequireExplicitLocalConfirmation() {
        val grants = setOf(PermissionLevel.L3_ADMIN)
        listOf(
            ConsoleOperation.REQUEST_FIRMWARE_UPDATE,
            ConsoleOperation.UNBIND_DEVICE,
            ConsoleOperation.CONFIGURE_PERMISSION,
            ConsoleOperation.DELETE_MEMORY,
        ).forEach {
            val value = mutation(operation = it)
            assertRejected(
                ConsoleErrorCode.LOCAL_CONFIRMATION_REQUIRED,
                value,
                context(grantedLevels = grants, locallyConfirmed = false),
            )
            assertSame(
                MutationDecision.Accepted,
                ConsolePolicy.evaluate(value, context(grantedLevels = grants, locallyConfirmed = true)),
            )
        }
    }

    @Test
    fun forbiddenOperationCannotBeGranted() {
        assertRejected(
            ConsoleErrorCode.UNSUPPORTED_OPERATION,
            mutation(operation = ConsoleOperation.RAW_SHELL),
            context(grantedLevels = PermissionLevel.entries.toSet(), locallyConfirmed = true),
        )
    }

    private fun assertRejected(
        expected: ConsoleErrorCode,
        mutation: ConsoleMutation,
        context: MutationContext,
    ) {
        val rejected = ConsolePolicy.evaluate(mutation, context) as MutationDecision.Rejected
        assertEquals(expected, rejected.code)
    }

    private fun mutation(
        operation: ConsoleOperation,
        generation: Long = GENERATION,
        expectedRevision: Long = REVISION,
        expiresAtEpochMs: Long = NOW + 1_000,
    ) = ConsoleMutation(
        requestId = "request-1",
        deviceId = DEVICE,
        generation = generation,
        expectedRevision = expectedRevision,
        issuedAtEpochMs = NOW - 1_000,
        expiresAtEpochMs = expiresAtEpochMs,
        operation = operation,
        arguments = when (operation) {
            ConsoleOperation.SET_VOLUME -> ConsoleMutationArguments.SetVolume(50)
            ConsoleOperation.SET_PERSONA_MODE ->
                ConsoleMutationArguments.SetPersonaMode(PersonaMode.GENTLE)
            ConsoleOperation.CONFIGURE_PERMISSION -> ConsoleMutationArguments.ConfigurePermission(
                PrivacyCapability.MICROPHONE,
                PermissionState.ALLOWED,
            )
            ConsoleOperation.REQUEST_FIRMWARE_UPDATE -> ConsoleMutationArguments.FirmwareUpdate(
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            )
            ConsoleOperation.DELETE_MEMORY ->
                ConsoleMutationArguments.DeleteMemory(MemoryDeleteScope.ALL)
            else -> ConsoleMutationArguments.None
        },
    )

    private fun context(
        grantedLevels: Set<PermissionLevel>,
        locallyConfirmed: Boolean = false,
    ) = MutationContext(
        nowEpochMs = NOW,
        deviceId = DEVICE,
        claimed = true,
        gatewayOnline = true,
        generation = GENERATION,
        revision = REVISION,
        grantedLevels = grantedLevels,
        locallyConfirmed = locallyConfirmed,
    )

    companion object {
        private const val DEVICE = "shaniu-test"
        private const val NOW = 10_000L
        private const val GENERATION = 7L
        private const val REVISION = 3L
    }
}
