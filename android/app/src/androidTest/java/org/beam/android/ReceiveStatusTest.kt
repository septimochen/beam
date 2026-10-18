package org.beam.android

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import android.provider.MediaStore
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Test

class ReceiveStatusTest {
  @Test
  fun oldResultsAreDiscardedAndUnsavedFilesCanBeRetried() {
    val instrumentation = InstrumentationRegistry.getInstrumentation()
    val context = instrumentation.targetContext
    val preferences = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
    context.stopService(Intent(context, TransferService::class.java))
    instrumentation.waitForIdleSync()
    preferences
      .edit()
      .putString("lastReceivedFile", "Received old.bin · Saved to Downloads")
      .commit()
    val folder = File(context.filesDir, preferences.getString("folder", "received")!!)
    folder.mkdirs()
    val name = "retry-${UUID.randomUUID()}.bin"
    val source = File(folder, name).apply { writeBytes(byteArrayOf(1, 2, 3)) }
    val ready = CountDownLatch(1)
    var core: TransferService? = null
    val connection =
      object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
          core = (binder as TransferService.LocalBinder).service
          ready.countDown()
        }

        override fun onServiceDisconnected(name: ComponentName?) {}
      }
    var activity: MainActivity? = null
    var bound = false
    fun descendants(view: View): List<View> =
      listOf(view) +
        if (view is ViewGroup)
          (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) }
        else emptyList()
    fun await(condition: () -> Boolean) {
      val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(12)
      while (System.nanoTime() < deadline) {
        var matched = false
        instrumentation.runOnMainSync { matched = condition() }
        if (matched) return
        Thread.sleep(30)
      }
      error("Receive status did not settle")
    }
    try {
      activity =
        instrumentation.startActivitySync(
          Intent(context, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            .setAction(Intent.ACTION_SEND)
            .setType("text/plain")
            .putExtra(Intent.EXTRA_TEXT, "Review only")
        ) as MainActivity
      bound =
        context.bindService(
          Intent(context, TransferService::class.java),
          connection,
          Context.BIND_AUTO_CREATE,
        )
      assertTrue(ready.await(10, TimeUnit.SECONDS))
      instrumentation.waitForIdleSync()
      instrumentation.runOnMainSync {
        assertFalse(preferences.contains("lastReceivedFile"))
        assertEquals("", checkNotNull(core).receivedFileStatus)
        assertTrue(checkNotNull(core).hasUnsavedFiles)
        val retry =
          descendants(checkNotNull(activity).window.decorView).filterIsInstance<Button>().single {
            it.text == "Retry saving"
          }
        assertTrue(retry.isShown)
        retry.performClick()
      }
      await { checkNotNull(core).receivedFileStatus == "Saved $name to Downloads" }
      instrumentation.runOnMainSync {
        assertFalse(source.exists())
        assertFalse(checkNotNull(core).hasUnsavedFiles)
        assertFalse(preferences.contains("lastReceivedFile"))
      }
      await { checkNotNull(core).receivedFileStatus.isEmpty() }
      instrumentation.runOnMainSync {
        assertFalse(checkNotNull(core).status.contains(name))
        val retry =
          descendants(checkNotNull(activity).window.decorView).filterIsInstance<Button>().single {
            it.text == "Retry saving"
          }
        assertFalse(retry.isShown)
      }
    } finally {
      instrumentation.runOnMainSync { activity?.finish() }
      if (bound) context.unbindService(connection)
      context.stopService(Intent(context, TransferService::class.java))
      source.delete()
      context.contentResolver.delete(
        MediaStore.Downloads.EXTERNAL_CONTENT_URI,
        "${MediaStore.Downloads.DISPLAY_NAME} = ?",
        arrayOf(name),
      )
      preferences.edit().remove("lastReceivedFile").commit()
    }
  }
}
