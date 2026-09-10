// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class OtaHttpRangeTest {
    private val prefix = "/u/test-nonce/"
    private val sizes = mapOf(
        "manifest.json" to 512L,
        "catalog.json" to 1024L,
        "catalog.sig" to 64L,
        "images/ap/ap.bin" to 8192L,
        "images/cp/cp.bin" to 4096L
    )

    private fun request(path: String, headers: String = ""): ByteArray =
        "GET $path HTTP/1.1\r\n$headers\r\n".toByteArray(Charsets.US_ASCII)

    private fun reject(header: ByteArray) {
        try {
            OtaHttpRange.parse(header, prefix, sizes)
            fail("request should be rejected")
        } catch (_: IllegalArgumentException) {
        }
    }

    @Test fun fullRequestSelectsWhitelistedEntry() {
        val result = OtaHttpRange.parse(request("${prefix}catalog.json"), prefix, sizes)
        assertEquals("catalog.json", result.entry)
        assertEquals(0L, result.start)
        assertEquals(1024L, result.count)
        assertEquals(200, result.status)
        assertTrue(String(result.responseHeader, Charsets.US_ASCII).contains("Content-Length: 1024\r\n"))
    }

    @Test fun explicitAndOpenEndedRangesAreBounded() {
        val closed = OtaHttpRange.parse(
            request("${prefix}images/ap/ap.bin", "Range: bytes=10-29\r\n"), prefix, sizes)
        assertEquals(10L, closed.start)
        assertEquals(20L, closed.count)
        assertEquals(206, closed.status)
        assertTrue(String(closed.responseHeader, Charsets.US_ASCII).contains(
            "Content-Range: bytes 10-29/8192\r\n"))

        val openEnded = OtaHttpRange.parse(
            request("${prefix}catalog.sig", "Range: bytes=60-\r\n"), prefix, sizes)
        assertEquals(60L, openEnded.start)
        assertEquals(4L, openEnded.count)
    }

    @Test fun rejectsRangesPathsAndBodies() {
        reject(request("${prefix}catalog.json", "Range: bytes=-1\r\n"))
        reject(request("${prefix}catalog.json", "Range: bytes=10-9\r\n"))
        reject(request("${prefix}catalog.json", "Range: bytes=0-1024\r\n"))
        reject(request("${prefix}catalog.json", "Range: bytes=1024-\r\n"))
        reject(request("${prefix}catalog.json", "Range: bytes=999999999999999999999-\r\n"))
        reject(request("${prefix}catalog.json", "Range: bytes=0-1,3-4\r\n"))
        reject(request("${prefix}catalog.json", "Range: bytes=0-1\r\nRange: bytes=2-3\r\n"))
        reject(request("${prefix}../catalog.json"))
        reject(request("${prefix}catalog.json?x=1"))
        reject(request("${prefix}catalog%2ejson"))
        reject(request("${prefix}catalog.json", "Content-Length: 1\r\n"))
        reject(request("${prefix}catalog.json", "Transfer-Encoding: chunked\r\n"))
        reject(request("${prefix}catalog.json") + "body".toByteArray(Charsets.US_ASCII))
    }

    @Test fun rejectsMalformedAndOversizedHeaders() {
        reject("POST ${prefix}catalog.json HTTP/1.1\r\n\r\n".toByteArray())
        reject("GET ${prefix}catalog.json HTTP/2\r\n\r\n".toByteArray())
        reject("GET ${prefix}catalog.json HTTP/1.1\n\n".toByteArray())
        reject(request("${prefix}missing.bin"))
        reject(ByteArray(8193) { 'A'.code.toByte() })
        reject(request("${prefix}catalog.json").copyOf().also { it[it.lastIndex] = 'x'.code.toByte() })
    }
}
