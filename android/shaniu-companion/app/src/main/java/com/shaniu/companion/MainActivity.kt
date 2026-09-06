// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.SharedPreferences
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import com.shaniu.companion.gateway.AndroidKeystoreTokenStore
import com.shaniu.companion.gateway.ConsoleGatewayClient
import com.shaniu.companion.gateway.GatewayCallResult
import com.shaniu.companion.gateway.GatewayEventConnection
import com.shaniu.companion.gateway.GatewayEventObserver
import com.shaniu.companion.gateway.GatewayFailureReason
import com.shaniu.companion.gateway.GatewayTokenPolicy
import com.shaniu.companion.gateway.GatewayTransportConfiguration
import com.shaniu.companion.gateway.OkHttpConsoleGatewaySession
import com.shaniu.companion.protocol.CompanionState
import com.shaniu.companion.protocol.CompanionStore
import com.shaniu.companion.protocol.ConsoleEventEnvelope
import com.shaniu.companion.protocol.ConsoleMutation
import com.shaniu.companion.protocol.ConsoleMutationArguments
import com.shaniu.companion.protocol.ConsoleOperation
import com.shaniu.companion.protocol.ConsolePolicy
import com.shaniu.companion.protocol.EventDisposition
import com.shaniu.companion.protocol.FirmwareRelease
import com.shaniu.companion.protocol.FirmwareReleaseCatalog
import com.shaniu.companion.protocol.GatewayConnection
import com.shaniu.companion.protocol.MutationContext
import com.shaniu.companion.protocol.MutationDecision
import com.shaniu.companion.protocol.MutationReceiptStatus
import com.shaniu.companion.protocol.PermissionLevel
import com.shaniu.companion.protocol.PermissionState
import com.shaniu.companion.protocol.PersonaMode
import com.shaniu.companion.protocol.PrivacyCapability
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicLong

/**
 * Production console-v1 entry point.
 *
 * Endpoint metadata is stored in ordinary preferences. The bearer credential is
 * handled only by [AndroidKeystoreTokenStore] and is never rendered or logged.
 * Device state is reported-state only: accepted mutations are not applied
 * optimistically and must be confirmed by a later snapshot/event.
 */
class MainActivity : Activity() {
    private data class GatewayRuntime(
        val epoch: Long,
        val deviceId: String,
        val session: OkHttpConsoleGatewaySession,
        val client: ConsoleGatewayClient,
    )

    private lateinit var preferences: SharedPreferences
    private lateinit var tokenStore: AndroidKeystoreTokenStore
    private lateinit var content: LinearLayout

    private val ioExecutor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val requestCounter = AtomicLong()

    private var runtime: GatewayRuntime? = null
    private var eventConnection: GatewayEventConnection? = null
    private var store: CompanionStore? = null
    private var releaseCatalog: FirmwareReleaseCatalog? = null
    private var connectionEpoch = 0L
    private var eventStreamEpoch = 0L
    private var currentTab = TAB_OVERVIEW
    private var busy = false
    private var eventConnected = false
    private var destroyed = false
    private var lastAction = "请配置 HTTPS Gateway、设备 ID 和访问令牌"

    private var draftOrigin = ""
    private var draftDeviceId = ""
    private var draftPins = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        tokenStore = AndroidKeystoreTokenStore(applicationContext)
        draftOrigin = preferences.getString(KEY_ORIGIN, "").orEmpty()
        draftDeviceId = preferences.getString(KEY_DEVICE_ID, "").orEmpty()
        draftPins = preferences.getString(KEY_PINS, "").orEmpty()
        setContentView(buildRoot())
        render()
    }

    override fun onDestroy() {
        destroyed = true
        closeRuntime(clearReportedState = true)
        ioExecutor.shutdownNow()
        mainHandler.removeCallbacksAndMessages(null)
        super.onDestroy()
    }

    private fun buildRoot(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(BACKGROUND)
        }
        root.addView(
            TextView(this).apply {
                text = "傻妞 Companion"
                textSize = 23f
                setTextColor(INK)
                setPadding(dp(20), dp(18), dp(20), dp(12))
            },
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            ),
        )
        content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(18), dp(8), dp(18), dp(24))
        }
        root.addView(
            ScrollView(this).apply { addView(content) },
            LinearLayout.LayoutParams(0, 0).apply {
                width = ViewGroup.LayoutParams.MATCH_PARENT
                height = 0
                weight = 1f
            },
        )
        val tabRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(Color.WHITE)
            setPadding(dp(8), dp(6), dp(8), dp(8))
        }
        TABS.forEachIndexed { index, title ->
            tabRow.addView(
                Button(this).apply {
                    text = title
                    isAllCaps = false
                    setOnClickListener {
                        currentTab = index
                        render()
                    }
                },
                LinearLayout.LayoutParams(dp(104), ViewGroup.LayoutParams.WRAP_CONTENT),
            )
        }
        root.addView(
            HorizontalScrollView(this).apply {
                isHorizontalScrollBarEnabled = false
                addView(tabRow)
            },
        )
        return root
    }

    private fun render() {
        if (destroyed) return
        content.removeAllViews()
        statusStrip()
        when (currentTab) {
            TAB_OVERVIEW -> renderOverview()
            TAB_INTERACTION -> renderInteraction()
            TAB_PERSONALITY -> renderPersonality()
            TAB_PRIVACY -> renderPrivacy()
            TAB_UPDATE -> renderUpdate()
        }
    }

    private fun statusStrip() {
        val state = store?.state
        val transport = when {
            busy -> "连接中"
            runtime == null -> "未连接"
            eventConnected -> "HTTPS + WSS 在线"
            else -> "HTTPS 在线 / WSS 断开"
        }
        addCard(
            "连接状态",
            "$transport\n设备：${state?.deviceId ?: draftDeviceId.ifBlank { "未配置" }}" +
                "\n最近操作：$lastAction",
        )
    }

    private fun renderOverview() {
        sectionTitle("连接配置")
        val originInput = editField("HTTPS Gateway，例如 https://gateway.example.com", draftOrigin)
        val deviceInput = editField("设备 ID", draftDeviceId)
        val pinsInput = editField(
            "可选 SPKI pin，每行一个 sha256/...",
            draftPins,
            multiline = true,
        )
        val tokenInput = editField(
            "访问令牌（留空则使用已保存令牌）",
            "",
            secret = true,
        )
        actionButton(if (runtime == null) "保存并连接" else "重新连接", enabled = !busy) {
            draftOrigin = originInput.text.toString().trim()
            draftDeviceId = deviceInput.text.toString().trim()
            draftPins = pinsInput.text.toString().trim()
            val suppliedToken = tokenInput.text.toString()
            tokenInput.setText("")
            connect(draftOrigin, draftDeviceId, draftPins, suppliedToken)
        }
        actionButton("刷新设备快照", enabled = runtime != null && !busy) { refreshSnapshot() }
        actionButton("断开", enabled = runtime != null || busy) {
            closeRuntime(clearReportedState = true)
            lastAction = "已断开；本机配置和加密令牌仍保留"
            render()
        }
        actionButton("清除本机绑定资料", enabled = !busy) {
            confirm(
                title = "清除本机绑定资料",
                message = "将删除 Gateway 地址、设备 ID、证书 pin 和 Keystore 加密令牌。",
            ) { clearProvisioning() }
        }

        sectionTitle("设备报告")
        val state = store?.state
        if (state == null) {
            addMuted("连接成功后显示 Gateway 返回的设备快照。")
        } else {
            addCard(
                "${state.presence} · ${state.turn}",
                "Gateway：${state.gateway}\n" +
                    "固件：${state.firmwareVersion ?: "未知"}\n" +
                    "电量：${state.batteryPercent?.let { "$it%" } ?: "未知"}" +
                    (if (state.charging) "（充电中）" else "") +
                        "\n代次/事件：${state.generation}/${state.lastSequence}\n" +
                        "修订：${state.revision}\n" +
                        "需全量同步：${if (state.needsSnapshot) "是" else "否"}",
            )
        }
    }

    private fun renderInteraction() {
        sectionTitle("语音交互")
        addMuted("手机只发送控制指令；麦克风采集、播放和打断由板端负责。")
        val state = requireReportedState() ?: return
        addCard("当前回合", "${state.turn}\n情绪（只读）：${state.emotion}")
        actionButton("开始远程回合", canMutate()) {
            submitMutation(ConsoleOperation.START_REMOTE_TURN)
        }
        actionButton("取消当前回合", canMutate()) {
            submitMutation(ConsoleOperation.CANCEL_TURN)
        }
    }

    private fun renderPersonality() {
        sectionTitle("偏好设置")
        val state = requireReportedState() ?: return
        addCard(
            "当前设置",
            "音量：${state.volumePercent}%\n人格：${state.personaMode}\n" +
                "只有设备上报新 revision 后界面才确认已生效。",
        )
        val volumeLabel = TextView(this).apply {
            text = getString(R.string.volume_slider_format, state.volumePercent)
            textSize = 16f
            setTextColor(INK)
        }
        content.addView(volumeLabel)
        content.addView(
            SeekBar(this).apply {
                max = 100
                progress = state.volumePercent
                isEnabled = canMutate()
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(bar: SeekBar?, value: Int, fromUser: Boolean) {
                        volumeLabel.text = getString(R.string.volume_slider_format, value)
                    }

                    override fun onStartTrackingTouch(bar: SeekBar?) = Unit

                    override fun onStopTrackingTouch(bar: SeekBar?) {
                        submitMutation(
                            ConsoleOperation.SET_VOLUME,
                            ConsoleMutationArguments.SetVolume(bar?.progress ?: state.volumePercent),
                        )
                    }
                })
            },
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            ),
        )
        PersonaMode.values().forEach { mode ->
            actionButton(
                if (mode == state.personaMode) "${personaLabel(mode)}（当前）" else personaLabel(mode),
                canMutate() && mode != state.personaMode,
            ) {
                submitMutation(
                    ConsoleOperation.SET_PERSONA_MODE,
                    ConsoleMutationArguments.SetPersonaMode(mode),
                )
            }
        }
    }

    private fun renderPrivacy() {
        sectionTitle("隐私权限")
        val state = requireReportedState() ?: return
        addMuted("权限变更属于 L3 管理操作，每次都要求本机确认。")
        PrivacyCapability.values().forEach { capability ->
            val current = state.permissions[capability] ?: PermissionState.NOT_GRANTED
            addCard(capabilityLabel(capability), "设备报告：$current")
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            row.addView(
                compactButton("允许", canMutate() && current != PermissionState.ALLOWED) {
                    confirmPrivacy(capability, PermissionState.ALLOWED)
                },
                LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f),
            )
            row.addView(
                compactButton("拒绝", canMutate() && current != PermissionState.DENIED) {
                    confirmPrivacy(capability, PermissionState.DENIED)
                },
                LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f),
            )
            content.addView(row)
        }
    }

    private fun renderUpdate() {
        sectionTitle("固件更新")
        val state = requireReportedState() ?: return
        addCard(
            "设备报告：${state.update.phase}",
            "当前：${state.firmwareVersion ?: "未知"}\n" +
                "目标：${state.update.targetVersion ?: "无"}\n" +
                "进度：${state.update.progressPercent}%\n" +
                "注意：请求被接受不等于升级成功；仅 CONFIRMED 是完成。",
        )
        actionButton("刷新已验证发布列表", canMutate() && state.generation > 0) {
            fetchReleases()
        }
        val catalog = releaseCatalog
        if (catalog == null) {
            addMuted("尚未从 Gateway 获取发布清单。")
            return
        }
        if (catalog.releases.isEmpty()) {
            addMuted("Gateway 当前没有适用于该设备的发布。")
        }
        catalog.releases.forEach { release -> renderRelease(release) }
    }

    private fun renderRelease(release: FirmwareRelease) {
        addCard(
            release.targetVersion,
            "来源：${release.requiredSourceVersion}\n" +
                "板型：${release.physicalBoard}\n" +
                "布局：${release.layoutIdentity}\n" +
                "清单：${release.manifestSha256.take(16)}…",
        )
        actionButton("请求更新到 ${release.targetVersion}", canMutate()) {
            confirm(
                title = "确认固件更新",
                message = "设备将按已验证清单 ${release.manifestSha256.take(16)}… 尝试更新到 ${release.targetVersion}。",
            ) {
                submitMutation(
                    ConsoleOperation.REQUEST_FIRMWARE_UPDATE,
                    ConsoleMutationArguments.FirmwareUpdate(release.manifestSha256),
                    locallyConfirmed = true,
                )
            }
        }
    }

    private fun connect(originValue: String, deviceId: String, pinsValue: String, token: String) {
        val pins = try {
            parsePins(pinsValue)
        } catch (_: IllegalArgumentException) {
            lastAction = "连接失败：证书 pin 格式无效"
            render()
            return
        }
        val configuration = try {
            GatewayTransportConfiguration.fromExplicit(originValue, pins)
                ?: throw IllegalArgumentException()
        } catch (_: IllegalArgumentException) {
            lastAction = "连接失败：必须填写合法的 HTTPS Gateway 根地址"
            render()
            return
        }
        try {
            CompanionStore(deviceId)
            if (token.isNotEmpty()) GatewayTokenPolicy.requireValid(token)
        } catch (_: IllegalArgumentException) {
            lastAction = "连接失败：设备 ID 或访问令牌格式无效"
            render()
            return
        }

        closeRuntime(clearReportedState = true)
        busy = true
        lastAction = "正在认证并获取设备快照"
        render()
        val epoch = connectionEpoch
        ioExecutor.execute {
            var sessionForCleanup: OkHttpConsoleGatewaySession? = null
            try {
                if (token.isNotEmpty()) tokenStore.storeAccessToken(token)
                val candidateSession = OkHttpConsoleGatewaySession(configuration, tokenStore)
                sessionForCleanup = candidateSession
                val candidateClient = ConsoleGatewayClient(configuration.origin, candidateSession)
                val result = candidateClient.fetchSnapshot(deviceId)
                postToMain {
                    if (!isCurrent(epoch)) {
                        candidateSession.close()
                        return@postToMain
                    }
                    when (result) {
                        is GatewayCallResult.Success -> {
                            val newStore = CompanionStore(deviceId)
                            val disposition = newStore.apply(result.value)
                            if (disposition != EventDisposition.APPLIED) {
                                candidateSession.close()
                                busy = false
                                lastAction = "连接失败：快照未通过状态校验"
                                render()
                                return@postToMain
                            }
                            preferences.edit()
                                .putString(KEY_ORIGIN, originValue)
                                .putString(KEY_DEVICE_ID, deviceId)
                                .putString(KEY_PINS, pinsValue)
                                .apply()
                            store = newStore
                            runtime = GatewayRuntime(epoch, deviceId, candidateSession, candidateClient)
                            releaseCatalog = null
                            busy = false
                            lastAction = "快照已同步，正在建立事件流"
                            render()
                            openEventStream(epoch)
                        }
                        is GatewayCallResult.Failure -> {
                            candidateSession.close()
                            busy = false
                            lastAction = failureText("连接失败", result)
                            render()
                        }
                    }
                }
            } catch (_: Exception) {
                sessionForCleanup?.close()
                postToMain {
                    if (!isCurrent(epoch)) return@postToMain
                    busy = false
                    lastAction = "连接失败：凭据不可用或本机安全存储异常"
                    render()
                }
            }
        }
    }

    private fun openEventStream(epoch: Long) {
        val active = runtime ?: return
        if (active.epoch != epoch) return
        val state = store?.state ?: return
        val streamEpoch = ++eventStreamEpoch
        ioExecutor.execute {
            try {
                val connection = active.session.openEventStream(
                    active.deviceId,
                    state.generation,
                    state.lastSequence,
                    object : GatewayEventObserver {
                        override fun onOpen() = postToMain {
                            if (!isCurrentStream(epoch, streamEpoch)) return@postToMain
                            eventConnected = true
                            lastAction = "事件流已连接"
                            render()
                        }

                        override fun onEvent(event: ConsoleEventEnvelope) = postToMain {
                            if (!isCurrentStream(epoch, streamEpoch)) return@postToMain
                            val disposition = store?.apply(event) ?: return@postToMain
                            lastAction = "事件 ${event.sequence}：$disposition"
                            render()
                            if (disposition == EventDisposition.NEEDS_SNAPSHOT) refreshSnapshot()
                        }

                        override fun onClosed() = postToMain {
                            if (!isCurrentStream(epoch, streamEpoch)) return@postToMain
                            eventConnected = false
                            lastAction = "事件流已关闭；可重新连接恢复"
                            render()
                        }

                        override fun onFailure(failure: GatewayCallResult.Failure) = postToMain {
                            if (!isCurrentStream(epoch, streamEpoch)) return@postToMain
                            eventConnected = false
                            lastAction = failureText("事件流中断", failure)
                            render()
                        }
                    },
                )
                postToMain {
                    if (!isCurrentStream(epoch, streamEpoch)) {
                        connection.close()
                    } else {
                        eventConnection?.close()
                        eventConnection = connection
                    }
                }
            } catch (_: Exception) {
                postToMain {
                    if (!isCurrentStream(epoch, streamEpoch)) return@postToMain
                    eventConnected = false
                    lastAction = "事件流失败：凭据或传输不可用"
                    render()
                }
            }
        }
    }

    private fun refreshSnapshot() {
        val active = runtime ?: return
        if (busy) return
        busy = true
        lastAction = "正在刷新设备快照"
        render()
        ioExecutor.execute {
            val result = active.client.fetchSnapshot(active.deviceId)
            postToMain {
                if (!isCurrent(active.epoch)) return@postToMain
                busy = false
                when (result) {
                    is GatewayCallResult.Success -> {
                        val refreshed = CompanionStore(active.deviceId)
                        if (refreshed.apply(result.value) == EventDisposition.APPLIED) {
                            store = refreshed
                            releaseCatalog = null
                            lastAction = "设备快照已刷新"
                            eventConnection?.close()
                            eventConnection = null
                            eventConnected = false
                            render()
                            openEventStream(active.epoch)
                        } else {
                            lastAction = "刷新失败：快照未通过状态校验"
                            render()
                        }
                    }
                    is GatewayCallResult.Failure -> {
                        lastAction = failureText("刷新失败", result)
                        render()
                    }
                }
            }
        }
    }

    private fun fetchReleases() {
        val active = runtime ?: return
        val state = store?.state ?: return
        if (busy || state.generation <= 0) return
        busy = true
        lastAction = "正在获取固件发布列表"
        render()
        ioExecutor.execute {
            val result = active.client.fetchFirmwareReleases(active.deviceId, state.generation)
            postToMain {
                if (!isCurrent(active.epoch)) return@postToMain
                busy = false
                when (result) {
                    is GatewayCallResult.Success -> {
                        releaseCatalog = result.value
                        lastAction = "已获取 ${result.value.releases.size} 个已验证发布"
                    }
                    is GatewayCallResult.Failure -> lastAction = failureText("发布列表失败", result)
                }
                render()
            }
        }
    }

    private fun submitMutation(
        operation: ConsoleOperation,
        arguments: ConsoleMutationArguments = ConsoleMutationArguments.None,
        locallyConfirmed: Boolean = false,
    ) {
        val active = runtime ?: return
        val state = store?.state ?: return
        if (busy) return
        val now = System.currentTimeMillis()
        val mutation = try {
            ConsoleMutation(
                requestId = "android-$now-${requestCounter.incrementAndGet()}",
                deviceId = active.deviceId,
                generation = state.generation,
                expectedRevision = state.revision,
                issuedAtEpochMs = now,
                expiresAtEpochMs = now + MUTATION_TTL_MS,
                operation = operation,
                arguments = arguments,
            )
        } catch (_: IllegalArgumentException) {
            lastAction = "请求未发送：当前设备状态不可用于该操作"
            render()
            return
        }
        val decision = ConsolePolicy.evaluate(
            mutation,
            MutationContext(
                nowEpochMs = now,
                deviceId = active.deviceId,
                claimed = state.claimed,
                gatewayOnline = state.gateway == GatewayConnection.ONLINE,
                generation = state.generation,
                revision = state.revision,
                grantedLevels = setOf(
                    PermissionLevel.L1_PREFERENCE,
                    PermissionLevel.L2_PRIVACY,
                    PermissionLevel.L3_ADMIN,
                ),
                locallyConfirmed = locallyConfirmed,
            ),
        )
        if (decision is MutationDecision.Rejected) {
            lastAction = "请求未发送：${decision.code.wireValue}"
            render()
            return
        }
        busy = true
        lastAction = "正在提交 ${operation.name}"
        render()
        ioExecutor.execute {
            val result = active.client.submitMutation(mutation)
            postToMain {
                if (!isCurrent(active.epoch)) return@postToMain
                busy = false
                when (result) {
                    is GatewayCallResult.Success -> {
                        lastAction = if (result.value.status == MutationReceiptStatus.ACCEPTED) {
                            "${operation.name} 已受理，等待设备上报确认"
                        } else {
                            "${operation.name} 被拒绝：${result.value.error?.wireValue ?: "unknown"}"
                        }
                    }
                    is GatewayCallResult.Failure -> lastAction = failureText("请求失败", result)
                }
                render()
            }
        }
    }

    private fun confirmPrivacy(capability: PrivacyCapability, state: PermissionState) {
        confirm(
            title = "确认隐私权限变更",
            message = "将 $capability 设置为 $state。此操作会发送到已绑定设备。",
        ) {
            submitMutation(
                ConsoleOperation.CONFIGURE_PERMISSION,
                ConsoleMutationArguments.ConfigurePermission(capability, state),
                locallyConfirmed = true,
            )
        }
    }

    private fun clearProvisioning() {
        closeRuntime(clearReportedState = true)
        val epoch = connectionEpoch
        busy = true
        lastAction = "正在清除本机绑定资料"
        render()
        ioExecutor.execute {
            val cleared = try {
                tokenStore.clearAccessToken()
                true
            } catch (_: Exception) {
                false
            }
            postToMain {
                if (!isCurrent(epoch)) return@postToMain
                busy = false
                if (cleared) {
                    preferences.edit().clear().apply()
                    draftOrigin = ""
                    draftDeviceId = ""
                    draftPins = ""
                    lastAction = "本机绑定资料已清除"
                } else {
                    lastAction = "清除失败：本机安全存储不可用"
                }
                render()
            }
        }
    }

    private fun closeRuntime(clearReportedState: Boolean) {
        connectionEpoch += 1
        eventStreamEpoch += 1
        eventConnection?.close()
        eventConnection = null
        runtime?.session?.close()
        runtime = null
        eventConnected = false
        busy = false
        releaseCatalog = null
        if (clearReportedState) store = null
    }

    private fun canMutate(): Boolean = runtime != null && !busy && store?.state?.needsSnapshot == false

    private fun requireReportedState(): CompanionState? {
        val state = store?.state
        if (state == null) addMuted("请先在“概览”页连接 Gateway 并同步设备快照。")
        return state
    }

    private fun isCurrent(epoch: Long): Boolean = !destroyed && epoch == connectionEpoch

    private fun isCurrentStream(epoch: Long, streamEpoch: Long): Boolean =
        isCurrent(epoch) && streamEpoch == eventStreamEpoch

    private fun postToMain(block: () -> Unit) {
        if (!destroyed) mainHandler.post { if (!destroyed) block() }
    }

    private fun parsePins(value: String): Set<String> {
        if (value.isBlank()) return emptySet()
        val pins = value.split(Regex("[\\s,]+"))
            .filter { it.isNotBlank() }
            .toSet()
        require(pins.size <= GatewayTransportConfiguration.MAX_CERTIFICATE_PINS)
        return pins
    }

    private fun failureText(prefix: String, failure: GatewayCallResult.Failure): String =
        "$prefix：${failure.reason.label()}${if (failure.retryable) "（可重试）" else ""}"

    private fun GatewayFailureReason.label(): String = when (this) {
        GatewayFailureReason.CREDENTIALS_UNAVAILABLE -> "凭据不可用"
        GatewayFailureReason.TRANSPORT_UNAVAILABLE -> "网络不可用"
        GatewayFailureReason.AUTHORIZATION_REVOKED -> "授权已失效"
        GatewayFailureReason.DEVICE_NOT_FOUND -> "设备不存在"
        GatewayFailureReason.REVISION_CONFLICT -> "状态修订冲突"
        GatewayFailureReason.RATE_LIMITED -> "请求过于频繁"
        GatewayFailureReason.SERVER_ERROR -> "Gateway 服务异常"
        GatewayFailureReason.REQUEST_REJECTED -> "请求被拒绝"
        GatewayFailureReason.PROTOCOL_ERROR -> "协议校验失败"
    }

    private fun sectionTitle(title: String) {
        content.addView(
            TextView(this).apply {
                text = title
                textSize = 20f
                setTextColor(INK)
                setPadding(0, dp(18), 0, dp(8))
            },
        )
    }

    private fun addCard(title: String, body: String) {
        content.addView(
            LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setBackgroundColor(Color.WHITE)
                setPadding(dp(16), dp(14), dp(16), dp(14))
                addView(TextView(context).apply {
                    text = title
                    textSize = 17f
                    setTextColor(INK)
                })
                addView(TextView(context).apply {
                    text = body
                    textSize = 14f
                    setTextColor(MUTED)
                    setPadding(0, dp(6), 0, 0)
                })
            },
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            ).apply { setMargins(0, 0, 0, dp(10)) },
        )
    }

    private fun addMuted(message: String) {
        content.addView(TextView(this).apply {
            text = message
            textSize = 14f
            setTextColor(MUTED)
            setPadding(0, dp(4), 0, dp(12))
        })
    }

    private fun editField(
        hintText: String,
        initial: String,
        secret: Boolean = false,
        multiline: Boolean = false,
    ): EditText = EditText(this).apply {
        hint = hintText
        setText(initial)
        setTextColor(INK)
        setHintTextColor(MUTED)
        inputType = when {
            secret -> InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
            multiline -> InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
            else -> InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
        }
        if (multiline) minLines = 2
        content.addView(
            this,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            ).apply { setMargins(0, 0, 0, dp(8)) },
        )
    }

    private fun actionButton(label: String, enabled: Boolean = true, action: () -> Unit) {
        content.addView(
            compactButton(label, enabled, action),
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            ).apply { setMargins(0, dp(3), 0, dp(3)) },
        )
    }

    private fun compactButton(label: String, enabled: Boolean, action: () -> Unit): Button =
        Button(this).apply {
            text = label
            isAllCaps = false
            isEnabled = enabled
            gravity = Gravity.CENTER
            setOnClickListener { action() }
        }

    private fun confirm(title: String, message: String, confirmed: () -> Unit) {
        AlertDialog.Builder(this)
            .setTitle(title)
            .setMessage(message)
            .setNegativeButton("取消", null)
            .setPositiveButton("确认") { _, _ -> confirmed() }
            .show()
    }

    private fun personaLabel(mode: PersonaMode): String = when (mode) {
        PersonaMode.GENTLE -> "温柔"
        PersonaMode.PLAYFUL -> "活泼"
        PersonaMode.QUIET -> "安静"
        PersonaMode.SERIOUS -> "认真"
        PersonaMode.TSUNDERE_LITE -> "轻傲娇"
    }

    private fun capabilityLabel(capability: PrivacyCapability): String = when (capability) {
        PrivacyCapability.MICROPHONE -> "麦克风"
        PrivacyCapability.CAMERA -> "摄像头"
        PrivacyCapability.LOCATION -> "位置"
        PrivacyCapability.LONG_TERM_MEMORY -> "长期记忆"
        PrivacyCapability.AUTHORIZED_VOICE -> "授权声音"
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    companion object {
        private const val PREFERENCES_NAME = "shaniu_gateway_configuration_v1"
        private const val KEY_ORIGIN = "origin"
        private const val KEY_DEVICE_ID = "device_id"
        private const val KEY_PINS = "certificate_pins"
        private const val MUTATION_TTL_MS = 30_000L

        private const val TAB_OVERVIEW = 0
        private const val TAB_INTERACTION = 1
        private const val TAB_PERSONALITY = 2
        private const val TAB_PRIVACY = 3
        private const val TAB_UPDATE = 4
        private val TABS = listOf("概览", "交互", "性格", "隐私", "更新")

        private val BACKGROUND = Color.rgb(245, 247, 250)
        private val INK = Color.rgb(30, 41, 59)
        private val MUTED = Color.rgb(100, 116, 139)
    }
}
