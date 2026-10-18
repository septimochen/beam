package org.beam.android

import android.net.Uri
import android.provider.MediaStore
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.io.IOException
import java.util.UUID
import org.junit.Assert.*
import org.junit.Test

class ReceivedDownloadsTest {
  @Test
  fun savesVerifiedCopiesWithoutReplacingDownloads() {
    val context = InstrumentationRegistry.getInstrumentation().targetContext
    val resolver = context.contentResolver
    val source = File(context.cacheDir, "download-${UUID.randomUUID()}.bin")
    val saved = mutableListOf<Uri>()
    try {
      val first = ByteArray(150000) { (it % 251).toByte() }
      source.writeBytes(first)
      val progress = mutableListOf<Int>()
      val one = ReceivedDownloads(resolver).save(source) { progress += it }
      saved += one.uri
      source.writeText("second copy")
      val two = ReceivedDownloads(resolver).save(source)
      saved += two.uri
      assertNotEquals(one.uri, two.uri)
      assertNotEquals(one.name, two.name)
      assertArrayEquals(first, resolver.openInputStream(one.uri)!!.use { it.readBytes() })
      assertEquals(
        "second copy",
        resolver.openInputStream(two.uri)!!.use { it.reader().readText() },
      )
      assertEquals(100, progress.last())
      assertTrue(progress.zipWithNext().all { it.first <= it.second })
      for (uri in saved) {
        resolver
          .query(
            uri,
            arrayOf(MediaStore.Downloads.IS_PENDING, MediaStore.Downloads.RELATIVE_PATH),
            null,
            null,
            null,
          )!!
          .use {
            assertTrue(it.moveToFirst())
            assertEquals(0, it.getInt(0))
            assertEquals("Download/", it.getString(1))
          }
      }
    } finally {
      saved.forEach { resolver.delete(it, null, null) }
      source.delete()
    }
  }

  @Test
  fun failedCopyRemovesPendingDownloadAndKeepsRecoveryFile() {
    val context = InstrumentationRegistry.getInstrumentation().targetContext
    val resolver = context.contentResolver
    val source = File(context.cacheDir, "failed-download-${UUID.randomUUID()}.bin")
    try {
      source.writeBytes(ByteArray(150000) { 42 })
      assertThrows(IOException::class.java) {
        ReceivedDownloads(resolver).save(source) { throw IOException("Storage failure fixture") }
      }
      assertEquals(150000L, source.length())
      resolver
        .query(
          MediaStore.Downloads.EXTERNAL_CONTENT_URI,
          arrayOf(MediaStore.Downloads._ID),
          "${MediaStore.Downloads.DISPLAY_NAME} = ?",
          arrayOf(source.name),
          null,
        )!!
        .use { assertFalse("Failed download was retained", it.moveToFirst()) }
    } finally {
      source.delete()
    }
  }

  @Test
  fun savesEmptyFiles() {
    val context = InstrumentationRegistry.getInstrumentation().targetContext
    val source = File(context.cacheDir, "empty-download-${UUID.randomUUID()}.bin")
    var saved: Uri? = null
    try {
      source.writeBytes(byteArrayOf())
      saved = ReceivedDownloads(context.contentResolver).save(source).uri
      assertEquals(0, context.contentResolver.openInputStream(saved)!!.use { it.readBytes().size })
    } finally {
      saved?.let { context.contentResolver.delete(it, null, null) }
      source.delete()
    }
  }
}
