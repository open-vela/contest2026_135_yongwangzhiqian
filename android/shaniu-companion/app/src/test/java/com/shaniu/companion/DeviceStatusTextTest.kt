// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import com.shaniu.companion.provision.DeviceControlProtocol
import org.junit.Assert.*
import org.junit.Test

class DeviceStatusTextTest {
    private val configured = DeviceControlProtocol.Snapshot(0, true, false, null, null, 0, 0)
    @Test fun configuredSessionDoesNotClaimWifiOrCloudReachability() {
        assertEquals("手机已连接，设备网络状态待确认", configured.statusText())
        assertEquals("设备 Wi-Fi 未就绪，暂时无法发起云端对话", configured.copy(wifiReady = false).statusText())
        assertEquals("设备已连接 Wi-Fi", configured.copy(wifiReady = true).statusText())
        assertEquals("上次对话未完成，请检查网络和语音服务配置", configured.copy(wifiReady = true, runtimeError = -110).statusText())
        assertEquals("请求被拒绝，请核对语音服务凭据或访问权限", configured.copy(wifiReady = true, runtimeError = -13).statusText())
    }
    @Test fun storageWorkIsNotPresentedAsConversation() {
        assertEquals("正在处理记忆设置", configured.copy(busy = true, memoryPending = true).statusText())
        assertEquals("正在处理这次对话", configured.copy(busy = true).statusText())
        assertEquals("手机已连接，语音服务尚未就绪", configured.copy(ready = false).statusText())
    }
}
