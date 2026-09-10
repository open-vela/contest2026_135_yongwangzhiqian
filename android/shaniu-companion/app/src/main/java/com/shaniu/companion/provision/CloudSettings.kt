// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.net.URI
import java.nio.ByteBuffer

/** Generic secret CCF1 record; caller wipes key and output after authenticated delivery.
 * No client credential is included and no key is retained by this encoder.
 */
object CloudSettings {
    const val MIMO_TOKEN_PLAN_URL = "https://token-plan-cn.xiaomimimo.com/v1"
    const val MIMO_STANDARD_URL = "https://api.xiaomimimo.com/v1"
    enum class Dialect(val wire: Int) { OPENAI_CHAT_AUDIO(1), MIMO(2) }

    fun encode(baseUrl: String, key: CharArray, dialect: Dialect,
               asrModel: String, chatModel: String, ttsModel: String): ByteArray {
        val uri = URI(baseUrl)
        require(uri.scheme == "https" && uri.rawUserInfo == null &&
            uri.rawQuery == null && uri.rawFragment == null)
        val host = requireNotNull(uri.host)
        require(host.length in 1..127 && host.split('.').all {
            it.length in 1..63 && Regex("[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?").matches(it)
        })
        val port = if (uri.port == -1) 443 else uri.port
        require(port in 1..65535)
        val path = uri.rawPath.ifEmpty { "/" }
        require(path.length in 1..127 && path.startsWith('/') &&
            Regex("[a-zA-Z0-9/_.-]+").matches(path) && !path.contains(".."))
        val models = listOf(asrModel, chatModel, ttsModel)
        require(models.all { it.length in 1..127 && Regex("[a-zA-Z0-9/_.:-]+").matches(it) })
        require(key.size in 1..4096 && key.all { it.code in 33..126 })
        val publicFields = listOf(host, path) + models
        val lengths = listOf(host.length, path.length, key.size) + models.map { it.length }
        val output = ByteBuffer.allocate(24 + lengths.sum())
        output.putInt(0x43434631).put(dialect.wire.toByte()).put(0).putShort(port.toShort())
        lengths.forEach { output.putShort(it.toShort()) }
        output.putInt(0)
        output.put(publicFields[0].toByteArray(Charsets.US_ASCII))
        output.put(publicFields[1].toByteArray(Charsets.US_ASCII))
        key.forEach { output.put(it.code.toByte()) }
        models.forEach { output.put(it.toByteArray(Charsets.US_ASCII)) }
        return output.array()
    }
}
