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
import java.io.IOException
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
    val otherFolder = File(context.filesDir, "inbox-${UUID.randomUUID()}")
    val previousPort = preferences.getInt("port", 4269)
    val previousTimeout = preferences.getInt("timeout", 30)
    val previousFolder = preferences.getString("folder", null)
    val hadPort = preferences.contains("port")
    val hadTimeout = preferences.contains("timeout")
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
      assertThrows(IOException::class.java) {
        ReceivedDownloads(context.contentResolver).save(source) { throw IOException("Copy failed") }
      }
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
        val service = checkNotNull(core)
        assertFalse(preferences.contains("lastReceivedFile"))
        assertEquals("", service.receivedFileStatus)
        assertTrue(service.hasUnsavedFiles)
        val error =
          assertThrows(IllegalStateException::class.java) {
            service.saveSettings(otherFolder.name, previousPort, previousTimeout)
          }
        assertTrue(error.message.orEmpty().contains("Retry saving"))
        assertFalse(otherFolder.exists())
        assertTrue(source.isFile)
        val newTimeout = if (previousTimeout == 30) 31 else 30
        service.saveSettings(service.receiveFolder, previousPort, newTimeout)
        assertEquals(newTimeout, service.timeoutSeconds)
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
        val service = checkNotNull(core)
        assertFalse(service.status.contains(name))
        val retry =
          descendants(checkNotNull(activity).window.decorView).filterIsInstance<Button>().single {
            it.text == "Retry saving"
          }
        assertFalse(retry.isShown)
        service.saveSettings(otherFolder.name, previousPort, previousTimeout)
        assertEquals(otherFolder.name, service.receiveFolder)
      }
    } finally {
      instrumentation.runOnMainSync { activity?.finish() }
      if (bound) context.unbindService(connection)
      context.stopService(Intent(context, TransferService::class.java))
      source.delete()
      otherFolder.delete()
      context.contentResolver.delete(
        MediaStore.Downloads.EXTERNAL_CONTENT_URI,
        "${MediaStore.Downloads.DISPLAY_NAME} = ?",
        arrayOf(name),
      )
      preferences
        .edit()
        .apply {
          remove("lastReceivedFile")
          if (hadPort) putInt("port", previousPort) else remove("port")
          if (hadTimeout) putInt("timeout", previousTimeout) else remove("timeout")
          putString("folder", previousFolder)
        }
        .commit()
    }
  }
}
