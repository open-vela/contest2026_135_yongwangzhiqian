// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.gateway

import com.shaniu.companion.protocol.CompanionState
import com.shaniu.companion.protocol.ConsoleErrorCode
import com.shaniu.companion.protocol.ConsoleEvent
import com.shaniu.companion.protocol.ConsoleEventEnvelope
import com.shaniu.companion.protocol.DeviceReport
import com.shaniu.companion.protocol.Emotion
import com.shaniu.companion.protocol.FirmwareRelease
import com.shaniu.companion.protocol.FirmwareReleaseCatalog
import com.shaniu.companion.protocol.GatewayConnection
import com.shaniu.companion.protocol.MutationContext
import com.shaniu.companion.protocol.PermissionLevel
import com.shaniu.companion.protocol.PermissionState
import com.shaniu.companion.protocol.PersonaMode
import com.shaniu.companion.protocol.Presence
import com.shaniu.companion.protocol.PrivacyCapability
import com.shaniu.companion.protocol.TurnPhase
import com.shaniu.companion.protocol.UpdatePhase
import com.shaniu.companion.protocol.UpdateState

/** In-process test fixture. It has no socket, BLE, credential or persistence path. */
class FakeConsoleGateway(
    private val deviceId: String,
    private val clock: () -> Long = System::currentTimeMillis,
) {
    private var generation = 1L
    private var sequence = 0L
    private var lastEnvelope: ConsoleEventEnvelope? = null
    private var report = DeviceReport(
        claimed = true,
        presence = Presence.ONLINE,
        gateway = GatewayConnection.ONLINE,
        batteryPercent = 78,
        charging = false,
        firmwareVersion = BASE_VERSION,
        revision = 1,
        turn = TurnPhase.IDLE,
        volumePercent = 55,
        personaMode = PersonaMode.GENTLE,
        emotion = Emotion.NEUTRAL,
        permissions = mapOf(
            PrivacyCapability.MICROPHONE to PermissionState.ALLOWED,
            PrivacyCapability.CAMERA to PermissionState.NOT_GRANTED,
            PrivacyCapability.LOCATION to PermissionState.NOT_GRANTED,
            PrivacyCapability.LONG_TERM_MEMORY to PermissionState.NOT_GRANTED,
            PrivacyCapability.AUTHORIZED_VOICE to PermissionState.ALLOWED,
        ),
    )

    fun connectSnapshot(): ConsoleEventEnvelope = snapshot()

    fun restartSnapshot(): ConsoleEventEnvelope {
        generation += 1
        sequence = 0
        report = report.copy(turn = TurnPhase.IDLE)
        return snapshot()
    }

    fun recoverySnapshot(): ConsoleEventEnvelope = snapshot()

    fun nextTurn(current: TurnPhase): ConsoleEventEnvelope {
        val next = when (current) {
            TurnPhase.IDLE, TurnPhase.OFFLINE, TurnPhase.ERROR -> TurnPhase.LISTENING
            TurnPhase.LISTENING -> TurnPhase.THINKING
            TurnPhase.THINKING -> TurnPhase.SPEAKING
            TurnPhase.SPEAKING -> TurnPhase.IDLE
        }
        report = report.copy(turn = next)
        return event(ConsoleEvent.TurnChanged(next))
    }

    fun cancelTurn(): ConsoleEventEnvelope {
        report = report.copy(turn = TurnPhase.IDLE)
        return event(ConsoleEvent.TurnChanged(TurnPhase.IDLE))
    }

    fun setPermission(
        capability: PrivacyCapability,
        state: PermissionState,
    ): ConsoleEventEnvelope {
        report = report.copy(permissions = report.permissions + (capability to state))
        return event(ConsoleEvent.PermissionChanged(capability, state))
    }

    /** Simulates device-confirmed preference state; a receipt alone never updates the UI. */
    fun setPreferences(
        volumePercent: Int = report.volumePercent,
        personaMode: PersonaMode = report.personaMode,
    ): ConsoleEventEnvelope {
        require(volumePercent in 0..100)
        check(report.revision < Long.MAX_VALUE)
        report = report.copy(
            revision = report.revision + 1,
            volumePercent = volumePercent,
            personaMode = personaMode,
        )
        return event(
            ConsoleEvent.SettingsChanged(
                revision = report.revision,
                volumePercent = report.volumePercent,
                personaMode = report.personaMode,
            ),
        )
    }

    /** Simulates a model/device report. There is intentionally no matching user mutation. */
    fun reportEmotion(emotion: Emotion): ConsoleEventEnvelope {
        report = report.copy(emotion = emotion)
        return event(ConsoleEvent.EmotionChanged(emotion))
    }

    /** Emits one intentionally out-of-sequence event for reducer recovery tests. */
    fun eventGap(): ConsoleEventEnvelope {
        sequence += 1
        report = report.copy(batteryPercent = 77)
        return event(ConsoleEvent.BatteryChanged(percent = 77, charging = false))
    }

    fun duplicateLast(): ConsoleEventEnvelope? = lastEnvelope

    fun firmwareReleases(): FirmwareReleaseCatalog = FirmwareReleaseCatalog(
        deviceId = deviceId,
        generation = generation,
        releases = listOf(TARGET_RELEASE),
    )

    fun beginUpdate(releaseManifestSha256: String): ConsoleEventEnvelope {
        require(releaseManifestSha256 == TARGET_RELEASE.manifestSha256)
        report = report.copy(
            update = UpdateState(
                phase = UpdatePhase.DOWNLOADING,
                targetVersion = TARGET_RELEASE.targetVersion,
                progressPercent = 0,
            ),
        )
        return event(ConsoleEvent.UpdateChanged(report.update))
    }

    fun advanceUpdate(): ConsoleEventEnvelope {
        val current = report.update
        val next = when (current.phase) {
            UpdatePhase.DOWNLOADING -> when {
                current.progressPercent < 35 -> current.copy(progressPercent = 35)
                current.progressPercent < 75 -> current.copy(progressPercent = 75)
                else -> current.copy(phase = UpdatePhase.VERIFYING, progressPercent = 100)
            }
            UpdatePhase.VERIFYING -> current.copy(phase = UpdatePhase.STAGED)
            UpdatePhase.STAGED -> current.copy(phase = UpdatePhase.REBOOTING)
            UpdatePhase.REBOOTING -> current.copy(phase = UpdatePhase.TRIAL)
            UpdatePhase.TRIAL -> current.copy(phase = UpdatePhase.CONFIRMED)
            else -> current
        }

        report = report.copy(
            firmwareVersion = if (next.phase == UpdatePhase.CONFIRMED) {
                TARGET_RELEASE.targetVersion
            } else {
                report.firmwareVersion
            },
            update = next,
        )
        if (next.phase == UpdatePhase.TRIAL) {
            generation += 1
            sequence = 0
            return snapshot()
        }
        if (next.phase == UpdatePhase.CONFIRMED) {
            return snapshot()
        }
        return event(ConsoleEvent.UpdateChanged(next))
    }

    fun failUpdate(): ConsoleEventEnvelope {
        report = report.copy(
            update = report.update.copy(
                phase = UpdatePhase.FAILED,
                error = ConsoleErrorCode.UPDATE_SIGNATURE_INVALID,
            ),
        )
        return event(ConsoleEvent.UpdateChanged(report.update))
    }

    fun rollbackUpdate(): ConsoleEventEnvelope {
        generation += 1
        sequence = 0
        report = report.copy(
            firmwareVersion = BASE_VERSION,
            update = UpdateState(
                phase = UpdatePhase.ROLLED_BACK,
                targetVersion = TARGET_RELEASE.targetVersion,
                progressPercent = 100,
            ),
        )
        return snapshot()
    }

    private fun snapshot(): ConsoleEventEnvelope = event(ConsoleEvent.StateSnapshot(report))

    private fun event(value: ConsoleEvent): ConsoleEventEnvelope {
        sequence += 1
        return ConsoleEventEnvelope(
            eventId = "fake-$generation-$sequence",
            deviceId = deviceId,
            generation = generation,
            sequence = sequence,
            occurredAtEpochMs = clock(),
            event = value,
        ).also { lastEnvelope = it }
    }

    companion object {
        const val BASE_VERSION = "18.6.149+209"
        const val TARGET_VERSION = "18.6.150+210"
        val TARGET_RELEASE = FirmwareRelease(
            manifestSha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            targetVersion = TARGET_VERSION,
            requiredSourceVersion = BASE_VERSION,
            requiredSourceRootSha256 =
                "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
            boardFamily = "bk7258",
            physicalBoard = "aidk_ai_toy",
            layoutIdentity = "bk7258-aidk-ai-toy-16m",
            layoutSha256 = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
            packageSha256 = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
            packageSizeBytes = 2_457_600,
        )

        fun contextFrom(
            state: CompanionState,
            nowEpochMs: Long,
            locallyConfirmed: Boolean = false,
        ) = MutationContext(
            nowEpochMs = nowEpochMs,
            deviceId = state.deviceId,
            claimed = state.claimed,
            gatewayOnline = state.gateway == GatewayConnection.ONLINE && !state.needsSnapshot,
            generation = state.generation,
            revision = state.revision,
            grantedLevels = setOf(
                PermissionLevel.L1_PREFERENCE,
                PermissionLevel.L2_PRIVACY,
                PermissionLevel.L3_ADMIN,
            ),
            locallyConfirmed = locallyConfirmed,
        )
    }
}
