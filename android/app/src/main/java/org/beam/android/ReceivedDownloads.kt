package org.beam.android

import android.content.ContentResolver
import android.content.ContentValues
import android.net.Uri
import android.os.Environment
import android.provider.MediaStore
import java.io.File

internal data class ReceivedDownload(val uri: Uri, val name: String)

// The native receiver verifies in private staging. Publish only complete copies
// through scoped storage; MediaStore allocates a new name on collisions.
internal class ReceivedDownloads(private val resolver: ContentResolver) {
  fun save(source: File, progress: (Int) -> Unit = {}): ReceivedDownload {
    Selection.filename(source.name)
    require(source.isFile && source.length() <= Selection.MAX_FILE) {
      "Received file is unavailable or exceeds 4 GiB"
    }
    val size = source.length()
    val values =
      ContentValues().apply {
        put(MediaStore.Downloads.DISPLAY_NAME, source.name)
        put(MediaStore.Downloads.MIME_TYPE, "application/octet-stream")
        put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS)
        put(MediaStore.Downloads.IS_PENDING, 1)
      }
    val uri =
      checkNotNull(resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)) {
        "Cannot create a file in Downloads"
      }
    try {
      source.inputStream().use { input ->
        checkNotNull(resolver.openOutputStream(uri, "w")) { "Cannot write to Downloads" }
          .use { output ->
            val buffer = ByteArray(65536)
            var copied = 0L
            var percent = -1
            while (true) {
              check(!Thread.currentThread().isInterrupted) { "Saving to Downloads was interrupted" }
              val count = input.read(buffer)
              if (count < 0) break
              check(count.toLong() <= size - copied) { "Received file changed while saving" }
              output.write(buffer, 0, count)
              copied += count
              val next = if (size == 0L) 100 else (100L * copied / size).toInt()
              if (next != percent) {
                progress(next)
                percent = next
              }
            }
            check(copied == size) { "Received file changed while saving" }
          }
      }
      check(
        resolver.update(
          uri,
          ContentValues().apply { put(MediaStore.Downloads.IS_PENDING, 0) },
          null,
          null,
        ) == 1
      ) {
        "Cannot finish saving to Downloads"
      }
    } catch (error: Exception) {
      try {
        check(resolver.delete(uri, null, null) == 1) { "Cannot remove incomplete Downloads copy" }
      } catch (cleanup: Exception) {
        error.addSuppressed(cleanup)
      }
      throw error
    }
    // Publication lets MediaStore finalize its collision-safe display name.
    // A metadata query failure must not delete a successfully published file.
    val name =
      runCatching {
        resolver.query(uri, arrayOf(MediaStore.Downloads.DISPLAY_NAME), null, null, null)?.use {
          if (it.moveToFirst()) it.getString(0) else null
        }
      }
        .getOrNull() ?: source.name
    return ReceivedDownload(uri, name)
  }
}
