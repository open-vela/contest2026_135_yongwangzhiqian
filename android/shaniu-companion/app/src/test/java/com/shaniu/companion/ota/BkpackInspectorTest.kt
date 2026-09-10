// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

class BkpackInspectorTest {
    private fun hex(b: ByteArray) = b.joinToString("") { "%02x".format(it) }
    private fun sha(b: ByteArray) = hex(MessageDigest.getInstance("SHA-256").digest(b))
    private fun pack(change: (MutableMap<String, ByteArray>) -> Unit = {}): File {
        val ap = "ap".toByteArray(); val cp = "cp".toByteArray()
        val files = linkedMapOf("manifest.json" to "{}".toByteArray(), "catalog.sig" to byteArrayOf(1), "images/ap/ap.bin" to ap, "images/cp/cp.bin" to cp)
        files["catalog.json"] = """{"format":"bk7258.ota/2","version":"1.2.3+4","security_counter":4,"target":{"physical_board":"aidk_ai_toy"},"layout":{"sha256":"${"0".repeat(64)}"},"ap":{"uri":"images/ap/ap.bin","size":2,"sha256":"${sha(ap)}"},"cp":{"uri":"images/cp/cp.bin","size":2,"sha256":"${sha(cp)}"}}""".toByteArray()
        change(files)
        return File.createTempFile("bkpack", ".zip").also { f -> ZipOutputStream(f.outputStream()).use { z -> files.forEach { (n,b) -> z.putNextEntry(ZipEntry(n)); z.write(b); z.closeEntry() } } }
    }
    private fun reject(change: (MutableMap<String, ByteArray>) -> Unit) {
        val f = pack(change); try { assertThrows(IllegalArgumentException::class.java) { BkpackInspector.inspect(f) } } finally { f.delete() }
    }
    @Test fun validPackageReportsIntegrityMetadata() {
        val f = pack(); try {
            val m = BkpackInspector.inspect(f)
            val catalog = ZipFile(f).use { z -> z.getInputStream(z.getEntry("catalog.json")).readBytes() }
            assertEquals("aidk_ai_toy", m.board); assertEquals(4L, m.securityCounter); assertEquals(2L, m.ap.size); assertEquals(sha(catalog), m.catalogSha256)
        } finally { f.delete() }
    }
    @Test fun rejectsMismatchedImageTraversalAndTruncation() {
        reject { it["images/ap/ap.bin"] = "bad".toByteArray() }
        reject { it["../images/ap/ap.bin"] = it.remove("images/ap/ap.bin")!! }
        val f = File.createTempFile("bkpack", ".zip"); try { f.writeBytes(byteArrayOf(1,2)); assertThrows(Exception::class.java) { BkpackInspector.inspect(f) } } finally { f.delete() }
    }
    @Test fun rejectsInvalidCounterExtraMissingAndOversizedMetadata() {
        reject { it["catalog.json"] = String(it["catalog.json"]!!).replace("\"security_counter\":4", "\"security_counter\":0").toByteArray() }
        reject { it["extra"] = byteArrayOf(1) }; reject { it.remove("catalog.sig") }
        reject { it["manifest.json"] = ByteArray(64 * 1024 + 1) { 'x'.code.toByte() } }
    }

    @Test fun rejectsAmbiguousAndNonIntegralCounters() {
        reject {
            val catalog = String(it["catalog.json"]!!)
            it["catalog.json"] = catalog.replace(
                "\"format\":\"bk7258.ota/2\"",
                "\"format\":\"bk7258.ota/2\",\"format\":\"bk7258.ota/2\"").toByteArray()
        }
        reject {
            it["catalog.json"] = String(it["catalog.json"]!!).replace(
                "\"security_counter\":4", "\"security_counter\":4.5").toByteArray()
        }
        reject {
            it["catalog.json"] = String(it["catalog.json"]!!).replace(
                "\"security_counter\":4", "\"security_counter\":\"4\"").toByteArray()
        }
        reject {
            it["catalog.json"] = String(it["catalog.json"]!!).replace(
                "\"security_counter\":4", "\"security_counter\":4294967296").toByteArray()
        }
    }

    @Test fun rejectsTooDeepCatalogAndInvalidHashes() {
        reject {
            val catalog = String(it["catalog.json"]!!)
            it["catalog.json"] = catalog.dropLast(1).plus(
                ",\"extra\":{\"a\":{\"b\":{\"c\":{\"d\":{}}}}}}").toByteArray()
        }
        reject {
            it["catalog.json"] = String(it["catalog.json"]!!).replace(
                "\"layout\":{\"sha256\":\"${"0".repeat(64)}\"}",
                "\"layout\":{\"sha256\":\"${"g".repeat(64)}\"}").toByteArray()
        }
        reject {
            val catalog = String(it["catalog.json"]!!)
            val imageHash = sha("ap".toByteArray())
            it["catalog.json"] = catalog.replace(imageHash, "${"g".repeat(64)}").toByteArray()
        }
    }
}
