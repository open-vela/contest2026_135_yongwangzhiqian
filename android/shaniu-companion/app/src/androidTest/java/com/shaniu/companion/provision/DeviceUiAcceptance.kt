// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.app.Instrumentation
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.graphics.Bitmap
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import com.shaniu.companion.MainActivity
import com.shaniu.companion.ota.BkpackInspector
import java.io.File
import java.util.UUID

/** Synthetic in-memory snapshots only. No binding, credential, BLE or device
 * mutation. Reflection keeps fixture injection out of the production APK API.
 */
internal object DeviceUiAcceptance {
    /** Local ProvisionActivity validation only: no bootstrap, binding, DNS, BLE or key material. */
    fun runProvisionInputValidationProbe(instrumentation: Instrumentation) {
        val activity = instrumentation.startActivitySync(
            Intent(instrumentation.targetContext, ProvisionActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        ) as ProvisionActivity
        fun field(name: String) = ProvisionActivity::class.java.getDeclaredField(name).apply {
            isAccessible = true
        }
        val showPage = ProvisionActivity::class.java.getDeclaredMethod(
            "showPage", Int::class.javaPrimitiveType!!,
        ).apply { isAccessible = true }
        try {
            instrumentation.runOnMainSync {
                showPage.invoke(activity, 1)
                (field("ssid").get(activity) as TextView).text = "test-only-wifi"
                (field("password").get(activity) as TextView).text = "test-only"
                (field("cloudUrl").get(activity) as TextView).text = "https://cloud.example/v1"
                (field("cloudKey").get(activity) as TextView).text = ""
                (field("asrModel").get(activity) as TextView).text = "test-asr"
                (field("chatModel").get(activity) as TextView).text = "test-chat"
                (field("ttsModel").get(activity) as TextView).text = "test-tts"
                fun views(root: View): List<View> = listOf(root) + if (root is ViewGroup)
                    (0 until root.childCount).flatMap { views(root.getChildAt(it)) } else emptyList()
                val save = views(activity.window.decorView).single {
                    it is TextView && it.text.toString() == "保存并连接"
                }
                repeat(2) {
                    save.performClick()
                    check((field("status").get(activity) as TextView).text.toString() ==
                        "请填写语音服务 Key 后再保存；当前事务会同时保存 Wi-Fi 和语音服务。")
                    check((field("ssid").get(activity) as TextView).text.toString() == "test-only-wifi")
                    check((field("password").get(activity) as TextView).text.toString() == "test-only")
                    check((field("cloudKey").get(activity) as TextView).text.isEmpty())
                    check(field("connection").get(activity) == null)
                    check(field("cloudResolving").get(activity) == false)
                    check(field("cloudLookupGeneration").get(activity) == 0L)
                }
            }
        } finally {
            instrumentation.runOnMainSync { activity.finish() }
            instrumentation.waitForIdleSync()
        }
    }

    fun run(instrumentation: Instrumentation) {
        val activity = instrumentation.startActivitySync(Intent(instrumentation.targetContext, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
        fun set(name: String, value: Any?) {
            MainActivity::class.java.getDeclaredField(name).apply { isAccessible = true }.set(activity, value)
        }
        fun get(name: String): Any? = MainActivity::class.java.getDeclaredField(name).apply {
            isAccessible = true
        }.get(activity)
        val render = MainActivity::class.java.getDeclaredMethod("render").apply { isAccessible = true }
        val confirmExpectedOta = MainActivity::class.java.getDeclaredMethod("confirmExpectedOta").apply {
            isAccessible = true
        }
        fun views(root: View): List<View> = listOf(root) + if (root is ViewGroup)
            (0 until root.childCount).flatMap { views(root.getChildAt(it)) } else emptyList()
        fun row(title: String) = views(activity.window.decorView).single {
            it.contentDescription?.toString()?.startsWith("$title，") == true
        }
        fun text(value: String) = views(activity.window.decorView).any { it is TextView && it.text.toString() == value }
        fun containsText(value: String) = views(activity.window.decorView).any {
            it is TextView && it.text.toString().contains(value)
        }
        fun button(value: String) = views(activity.window.decorView).single {
            it is TextView && it.text.toString() == value
        }
        val base = DeviceControlProtocol.Snapshot(0, true, false, 50, 0, 0, 0,
            memorySupported = true, memoryEnabled = false, wifiReady = true)
        val preferenceName = "device-ui-ota-${UUID.randomUUID()}"
        val testPreferences = instrumentation.targetContext.getSharedPreferences(
            preferenceName, Context.MODE_PRIVATE)
        lateinit var originalPreferences: SharedPreferences
        instrumentation.runOnMainSync {
            originalPreferences = get("preferences") as SharedPreferences
        }
        try {
            instrumentation.runOnMainSync {
                set("provisionedDeviceId", "ui-synthetic-device")
                set("legacyConsoleMode", false)
                set("currentTab", 3)
                set("directSnapshot", base)
                render.invoke(activity)
                check(row("跨重启记忆").isEnabled && row("删除已保存的记忆").isEnabled)
                check(text("已关闭 · 不读取或新增保存"))
                set("directSnapshot", base.copy(busy = true, memoryEnabled = null, memoryPending = true))
                render.invoke(activity)
                check(!row("跨重启记忆").isEnabled && !row("删除已保存的记忆").isEnabled)
                check(!row("清空近期对话").isEnabled)
                set("directSnapshot", base.copy(memoryEnabled = null, memoryFailed = true))
                render.invoke(activity)
                check(!row("跨重启记忆").isEnabled && !row("删除已保存的记忆").isEnabled)

                /* Enter through the actual overview-page row, not the
                 * settings-page duplicate. No connection or OTA command is
                 * injected. */
                set("currentTab", 0)
                set("directSnapshot", base)
                render.invoke(activity)
                row("固件更新").performClick()
                check(text("固件更新"))
                check(text("连接设备后读取"))
                check(!button("从手机开始升级").isEnabled)

                /* An OTA-capability-free STATUS is old firmware. */
                set("directSnapshot", base.copy(otaSupported = false))
                render.invoke(activity)
                check(!button("从手机开始升级").isEnabled)

                /* Use only a unique, test-owned expected record. Calling the
                 * real confirmation method must reject a mismatched INFO. */
                set("preferences", testPreferences)
                check(testPreferences.edit()
                    .putString("ota_expected_version", "1.2.3+4")
                    .putLong("ota_expected_counter", 4)
                    .putString("ota_expected_catalog", "synthetic-catalog")
                    .putString("ota_expected_device", "ui-synthetic-device")
                    .commit())
                set("otaStatus", DeviceControlProtocol.OtaStatus(3, 6, 100, 100, 0))
                set("directFirmwareInfo", DeviceControlProtocol.FirmwareInfo(99, 0, 0, 1, 999))
                set("otaMessage", "")
                confirmExpectedOta.invoke(activity)
                render.invoke(activity)
                check(containsText("已结束，等待版本核对"))
                check(text("设备未确认本次升级完成，请根据设备状态重试或恢复。"))
                check(!views(activity.window.decorView).filterIsInstance<TextView>().any {
                    it.text.toString().startsWith("设备已确认完成升级：")
                })

                /* The same terminal report only becomes success after INFO
                 * matches both version and security counter. */
                set("directFirmwareInfo", DeviceControlProtocol.FirmwareInfo(1, 2, 3, 4, 4))
                set("otaMessage", "")
                confirmExpectedOta.invoke(activity)
                render.invoke(activity)
                check(text("设备已确认完成升级：1.2.3+4。"))

                /* A terminal device error must remain visible and can never
                 * reuse the confirmation success wording. */
                set("otaStatus", DeviceControlProtocol.OtaStatus(3, 8, 100, 100, -5))
                set("otaMessage", "")
                confirmExpectedOta.invoke(activity)
                render.invoke(activity)
                check(containsText("升级失败"))
                check(containsText("设备错误 -5"))
                check(!views(activity.window.decorView).filterIsInstance<TextView>().any {
                    it.text.toString().startsWith("设备已确认完成升级：")
                })

                set("currentTab", 0)
                set("directSnapshot", base.copy(wifiReady = false))
                render.invoke(activity)
                check(text("设备 Wi-Fi 未就绪，暂时无法发起云端对话"))
                set("directSnapshot", base.copy(busy = true, memoryEnabled = null, memoryPending = true))
                render.invoke(activity)
                check(text("正在处理记忆设置"))
                check(!text("停止这次对话"))
                runFirmwareInspectionLifecycleProbe(activity)
                set("currentTab", 3)
                set("directSnapshot", base)
                render.invoke(activity)
            }
            runProvisionRecoveryCancelProbe(instrumentation)
            runProvisionBootstrapNormalModeProbe(instrumentation)
            instrumentation.waitForIdleSync()
            val screenshot = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
            File(instrumentation.targetContext.cacheDir, "device-ui-acceptance.png").outputStream().use {
                check(screenshot.compress(Bitmap.CompressFormat.PNG, 100, it))
            }
            screenshot.recycle()
        } finally {
            instrumentation.runOnMainSync {
                set("preferences", originalPreferences)
                testPreferences.edit().clear().commit()
                instrumentation.targetContext.deleteSharedPreferences(preferenceName)
                activity.finish()
            }
            instrumentation.waitForIdleSync()
        }
    }

    private fun runFirmwareInspectionLifecycleProbe(activity: MainActivity) {
        fun field(name: String) = MainActivity::class.java.getDeclaredField(name).apply {
            isAccessible = true
        }
        val finish = MainActivity::class.java.getDeclaredMethod(
            "finishFirmwareInspection", Long::class.javaPrimitiveType, Pair::class.java,
            Boolean::class.javaPrimitiveType,
        ).apply { isAccessible = true }
        val select = MainActivity::class.java.getDeclaredMethod(
            "selectTab", Int::class.javaPrimitiveType,
        ).apply { isAccessible = true }
        val image = BkpackInspector.Image(1, "0".repeat(64))
        val metadata = BkpackInspector.Metadata("aidk_ai_toy", "1.2.3+4", 4, "0".repeat(64),
            "1".repeat(64), image, image)
        val stale = File(activity.cacheDir, "ota-stale-${UUID.randomUUID()}").apply { writeText("stale") }
        val late = File(activity.cacheDir, "ota-late-${UUID.randomUUID()}").apply { writeText("late") }
        try {
            field("currentTab").set(activity, 4)
            field("firmwareInspectionEpoch").set(activity, 20L)
            field("firmwareInspectionPending").set(activity, true)
            finish.invoke(activity, 19L, metadata to stale, false)
            check(!stale.exists())
            check(field("firmwareInspectionPending").get(activity) == true)

            select.invoke(activity, 5)
            select.invoke(activity, 4)
            finish.invoke(activity, 20L, metadata to late, false)
            check(!late.exists())
            check(field("firmwareInspectionPending").get(activity) == false)

            field("firmwareInspectionEpoch").set(activity, 21L)
            field("firmwareInspectionPending").set(activity, true)
            finish.invoke(activity, 21L, null, true)
            check(field("firmwareInspectionPending").get(activity) == false)
            check(field("firmwareInspectionMessage").get(activity) != null)
        } finally {
            stale.delete(); late.delete()
        }
    }

    private fun runProvisionRecoveryCancelProbe(instrumentation: Instrumentation) {
        val activity = instrumentation.startActivitySync(
            Intent(instrumentation.targetContext, ProvisionActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        ) as ProvisionActivity
        val preferencesName = "device-ui-recovery-${UUID.randomUUID()}"
        val preferences = instrumentation.targetContext.getSharedPreferences(
            preferencesName, Context.MODE_PRIVATE)
        val store = ProvisionBindingStore(
            ProvisionBindingStore.SharedPreferencesBackend(preferences),
        )
        val deviceId = "ui-recovery-device"
        val receipt = store.begin(deviceId, ByteArray(16) { (it + 1).toByte() })
        fun field(name: String): java.lang.reflect.Field =
            ProvisionActivity::class.java.getDeclaredField(name).apply { isAccessible = true }
        fun get(name: String): Any? = field(name).get(activity)
        fun set(name: String, value: Any?) = field(name).set(activity, value)
        val showPage = ProvisionActivity::class.java.getDeclaredMethod(
            "showPage", Int::class.javaPrimitiveType!!,
        ).apply { isAccessible = true }
        val resultButton = get("resultButton") as android.widget.Button
        class CloseProbe : AutoCloseable {
            var count = 0
            var detachedBeforeClose = false
            var epochBeforeClose = Long.MIN_VALUE
            override fun close() {
                count++
                epochBeforeClose = get("epoch") as Long
                detachedBeforeClose = get("recoveryControl") == null
            }
        }
        try {
            instrumentation.runOnMainSync {
                set("bindingStore", store)
                showPage.invoke(activity, 2)
                val first = CloseProbe()
                val before = get("epoch") as Long
                set("recoveryControl", first)
                resultButton.performClick()
                check(get("page") == 0)
                check((get("status") as TextView).text.toString() ==
                    "已取消本次连接，认领结果仍需核对。")
                check(first.count == 1 && first.detachedBeforeClose &&
                    first.epochBeforeClose > before)
                check(store.pending(deviceId) == receipt)

                showPage.invoke(activity, 2)
                val second = CloseProbe()
                val beforeBack = get("epoch") as Long
                set("recoveryControl", second)
                activity.onBackPressed()
                check(get("page") == 0)
                check(second.count == 1 && second.detachedBeforeClose &&
                    second.epochBeforeClose > beforeBack)
                check(store.pending(deviceId) == receipt)
                activity.onBackPressed()
                check(second.count == 1)
            }
        } finally {
            instrumentation.runOnMainSync { activity.finish() }
            preferences.edit().clear().commit()
            instrumentation.targetContext.deleteSharedPreferences(preferencesName)
        }
    }

    private fun runProvisionBootstrapNormalModeProbe(instrumentation: Instrumentation) {
        val activity = instrumentation.startActivitySync(
            Intent(instrumentation.targetContext, ProvisionActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        ) as ProvisionActivity
        fun field(name: String) = ProvisionActivity::class.java.getDeclaredField(name).apply {
            isAccessible = true
        }
        val nextPage = ProvisionActivity::class.java.getDeclaredMethod("nextPage").apply {
            isAccessible = true
        }
        val bootstrap = ProvisionBootstrap.parse(
            """{"protocol":"provision-bootstrap-v1","device_id":"ui-normal-device","certificate_sha256":"${"ab".repeat(32)}","possession_secret":"${java.util.Base64.getEncoder().encodeToString(ByteArray(32) { 42 })}"}""".toCharArray(),
        )
        try {
            instrumentation.runOnMainSync {
                val adapter = checkNotNull(instrumentation.targetContext
                    .getSystemService(android.bluetooth.BluetoothManager::class.java)?.adapter)
                field("selected").set(activity, adapter.getRemoteDevice("02:00:00:00:00:01"))
                field("bootstrap").set(activity, bootstrap)
                field("ca").set(activity, null)
                field("developerMode").set(activity, false)
                nextPage.invoke(activity)
                check(field("page").get(activity) == 1)
                check((field("status").get(activity) as TextView).text.toString().contains("设置网络"))
            }
        } finally {
            bootstrap.close()
            instrumentation.runOnMainSync { activity.finish() }
        }
    }
}
