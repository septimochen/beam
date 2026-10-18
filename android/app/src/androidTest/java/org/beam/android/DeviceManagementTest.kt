package org.beam.android

import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.view.View
import android.view.ViewGroup
import android.widget.ScrollView
import android.widget.TextView
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.util.UUID
import org.junit.Assert.*
import org.junit.Test

class DeviceManagementTest {
  @Test
  fun editAndRestoreThroughJniPreserveExactCertificates() {
    val context = InstrumentationRegistry.getInstrumentation().targetContext
    val root =
      File(context.cacheDir.canonicalFile, "devices-${UUID.randomUUID()}").apply { mkdirs() }
    try {
      NativeSession(File(root, "phone").path).use { phone ->
        NativeSession(File(root, "laptop").path).use { laptop ->
          NativeSession(File(root, "replacement").path).use { replacement ->
            val original = laptop.call(Command.IDENTITY)
            val next = replacement.call(Command.IDENTITY)
            val cert = File(root, "laptop.pem").apply { writeText(original[2]) }
            phone.call(Command.PAIR, "laptop", cert.path, original[0], "127.0.0.1:4269")
            val backup = phone.call(Command.EXPORT_DEVICES)[0]
            assertFalse(backup.contains("PRIVATE KEY"))
            assertEquals("1", phone.call(Command.PREVIEW_DEVICES, backup)[2])
            phone.call(
              Command.EDIT_DEVICE,
              "laptop",
              "work-mac",
              "[::1]:5000",
              "100.64.0.2:5000",
              "",
              "",
              original[0],
            )
            val edited = phone.call(Command.DEVICE_DETAILS)
            assertEquals(
              listOf("work-mac", "[::1]:5000", "100.64.0.2:5000", original[0], original[2]),
              edited,
            )
            try {
              phone.call(
                Command.EDIT_DEVICE,
                "work-mac",
                "work-mac",
                "",
                "",
                next[2],
                original[0],
                original[0],
              )
              fail("Unverified replacement accepted")
            } catch (_: IllegalStateException) {}
            phone.call(
              Command.EDIT_DEVICE,
              "work-mac",
              "work-mac",
              "127.0.0.1:4269",
              "",
              next[2],
              next[0],
              original[0],
            )
            assertEquals(next[0], phone.call(Command.DEVICE_DETAILS)[3])
            phone.call(Command.UNPAIR, "work-mac")
            assertEquals("1", phone.call(Command.IMPORT_DEVICES, backup)[1])
            assertEquals(original[0], phone.call(Command.DEVICE_DETAILS)[3])
            assertEquals("0", phone.call(Command.IMPORT_DEVICES, backup)[1])
          }
        }
      }
    } finally {
      root.deleteRecursively()
    }
  }

  @Test
  fun devicesScreenProvidesSearchPairingAndBackupActions() {
    val instrumentation = InstrumentationRegistry.getInstrumentation()
    val context = instrumentation.targetContext
    val activity =
      instrumentation.startActivitySync(
        Intent(context, DevicesActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
      ) as DevicesActivity
    fun texts(view: View): List<String> =
      when (view) {
        is TextView -> listOf(view.text.toString(), view.contentDescription?.toString() ?: "")
        is ViewGroup -> (0 until view.childCount).flatMap { texts(view.getChildAt(it)) }
        else -> emptyList()
      }
    try {
      instrumentation.waitForIdleSync()
      instrumentation.runOnMainSync {
        val labels = texts(activity.window.decorView)
        for (expected in
          listOf(
            "Your devices",
            "+ Add device",
            "Refresh",
            "Search devices or addresses",
            "Export backup",
            "Import backup",
            "My identity",
          )) assertTrue("Missing $expected", labels.contains(expected))
        if (InstrumentationRegistry.getArguments().getString("renderDevices") == "true") {
          val container = activity.findViewById<ViewGroup>(android.R.id.content)
          val column = (container.getChildAt(0) as ScrollView).getChildAt(0)
          val bitmap = Bitmap.createBitmap(column.width, column.height, Bitmap.Config.ARGB_8888)
          val canvas = Canvas(bitmap)
          canvas.drawColor(android.graphics.Color.WHITE)
          column.draw(canvas)
          File(context.cacheDir, "devices-ui.png").outputStream().use {
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)
          }
          bitmap.recycle()
        }
      }
    } finally {
      instrumentation.runOnMainSync { activity.finish() }
    }
  }
}
