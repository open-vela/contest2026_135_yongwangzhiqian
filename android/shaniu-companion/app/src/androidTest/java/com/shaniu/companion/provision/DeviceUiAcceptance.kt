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
import java.io.File
import java.util.UUID

/** Synthetic in-memory snapshots only. No binding, credential, BLE or device
 * mutation. Reflection keeps fixture injection out of the production APK API.
 */
internal object DeviceUiAcceptance {
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
                set("currentTab", 3)
                set("directSnapshot", base)
                render.invoke(activity)
            }
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
}
