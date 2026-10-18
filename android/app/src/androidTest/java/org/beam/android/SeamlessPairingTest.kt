package org.beam.android

import android.Manifest
import android.os.Build
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.util.UUID
import org.junit.Assert.*
import org.junit.Test

class SeamlessPairingTest {
  @Test
  fun pairingKeepsPastedInvitationsCollapsed() {
    val instrumentation = InstrumentationRegistry.getInstrumentation()
    val context = instrumentation.targetContext
    if (Build.VERSION.SDK_INT >= 37)
      instrumentation.uiAutomation.grantRuntimePermission(
        context.packageName,
        Manifest.permission.ACCESS_LOCAL_NETWORK,
      )
    val activity =
      instrumentation.startActivitySync(
        android.content
          .Intent(context, PairingActivity::class.java)
          .addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK)
      ) as PairingActivity
    fun descendants(view: android.view.View): List<android.view.View> =
      listOf(view) +
        if (view is android.view.ViewGroup)
          (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) }
        else emptyList()
    try {
      val deadline = System.nanoTime() + 5_000_000_000L
      var hosting = false
      while (!hosting && System.nanoTime() < deadline) {
        instrumentation.runOnMainSync {
          hosting =
            descendants(activity.window.decorView).filterIsInstance<android.widget.TextView>().any {
              it.text.matches(Regex("[0-9]{6}"))
            }
        }
        if (!hosting) Thread.sleep(30)
      }
      assertTrue("Pairing invitation did not open", hosting)
      instrumentation.waitForIdleSync()
      instrumentation.runOnMainSync {
        val views = descendants(activity.window.decorView)
        val invitation =
          views.filterIsInstance<android.widget.EditText>().single {
            it.contentDescription == "Beam invitation"
          }
        assertFalse(invitation.isShown)
        if (InstrumentationRegistry.getArguments().getString("renderPairing") == "true") {
          val container = activity.findViewById<android.view.ViewGroup>(android.R.id.content)
          val scroll = container.getChildAt(0) as android.widget.ScrollView
          val column = scroll.getChildAt(0)
          val bitmap =
            android.graphics.Bitmap.createBitmap(
              column.width,
              column.height,
              android.graphics.Bitmap.Config.ARGB_8888,
            )
          val canvas = android.graphics.Canvas(bitmap)
          canvas.drawColor((scroll.background as android.graphics.drawable.ColorDrawable).color)
          column.draw(canvas)
          File(context.cacheDir, "pairing-ui.png").outputStream().use {
            bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
          }
          bitmap.recycle()
        }
        views
          .filterIsInstance<android.widget.Button>()
          .single { it.text == "Paste invitation" }
          .performClick()
        assertTrue(invitation.isShown)
      }
    } finally {
      instrumentation.runOnMainSync { activity.finish() }
    }
  }

  private fun await(session: NativeSession, state: String): List<String> {
    val deadline = System.nanoTime() + 15_000_000_000L
    while (System.nanoTime() < deadline) {
      val value = session.call(Command.PAIRING_SNAPSHOT)
      if (value[0] == state) return value
      check(value[0] !in listOf("failed", "expired", "incomplete")) { value[1] }
      Thread.sleep(20)
    }
    error("Pairing did not reach $state")
  }

  @Test
  fun qrFromNativeEncoderPairsThroughJniAndPersistsFullPins() {
    val instrumentation = InstrumentationRegistry.getInstrumentation()
    val context = instrumentation.targetContext
    if (Build.VERSION.SDK_INT >= 37)
      instrumentation.uiAutomation.grantRuntimePermission(
        context.packageName,
        Manifest.permission.ACCESS_LOCAL_NETWORK,
      )
    val root =
      File(context.cacheDir.canonicalFile, "seamless-${UUID.randomUUID()}").apply { mkdirs() }
    try {
      NativeSession(File(root, "desktop").path).use { desktop ->
        NativeSession(File(root, "phone").path).use { phone ->
          desktop.call(Command.PAIRING_HOST, "desktop")
          var invitation = await(desktop, "hosting")
          repeat(12) { index ->
            val rows = invitation[7].lineSequence().filter { it.isNotEmpty() }.toList()
            val size = (rows.size + 8) * 4
            val image =
              ByteArray(size * size) { index ->
                val x = index % size / 4 - 4
                val y = index / size / 4 - 4
                if (y in rows.indices && x in rows[y].indices && rows[y][x] == '1') 0
                else 255.toByte()
              }
            assertEquals(invitation[2], PairingQr.decode(image, size, size))
            if (index < 11) {
              desktop.call(Command.PAIRING_HOST, "desktop")
              invitation = await(desktop, "hosting")
            }
          }
          phone.call(Command.PAIRING_HOST, "phone")
          phone.call(Command.PAIRING_JOIN, invitation[2])
          val desktopApproval = await(desktop, "awaiting approval")
          val phoneApproval = await(phone, "awaiting approval")
          assertTrue(desktop.call(Command.PEERS).isEmpty())
          desktop.call(Command.PAIRING_APPROVE, desktopApproval[5])
          phone.call(Command.PAIRING_APPROVE, phoneApproval[5])
          await(desktop, "paired")
          await(phone, "paired")
          assertEquals(phone.call(Command.IDENTITY)[0], desktop.call(Command.PEERS)[2])
          assertEquals(desktop.call(Command.IDENTITY)[0], phone.call(Command.PEERS)[2])
        }
      }
      NativeSession(File(root, "phone").path).use { reopened ->
        assertEquals("desktop", reopened.call(Command.PEERS)[0])
      }
    } finally {
      root.deleteRecursively()
    }
  }

  @Test
  fun cameraDenialKeepsPasteAndPinAvailableAcrossPolling() {
    val instrumentation = InstrumentationRegistry.getInstrumentation()
    val context = instrumentation.targetContext
    if (Build.VERSION.SDK_INT >= 37)
      instrumentation.uiAutomation.grantRuntimePermission(
        context.packageName,
        Manifest.permission.ACCESS_LOCAL_NETWORK,
      )
    val activity =
      instrumentation.startActivitySync(
        android.content
          .Intent(context, PairingActivity::class.java)
          .addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK)
      ) as PairingActivity
    fun texts(view: android.view.View): List<String> =
      when (view) {
        is android.widget.TextView -> listOf(view.text.toString())
        is android.view.ViewGroup ->
          (0 until view.childCount).flatMap { texts(view.getChildAt(it)) }
        else -> emptyList()
      }
    try {
      instrumentation.runOnMainSync {
        activity.onRequestPermissionsResult(
          22,
          arrayOf(Manifest.permission.CAMERA),
          intArrayOf(android.content.pm.PackageManager.PERMISSION_DENIED),
        )
      }
      Thread.sleep(250)
      instrumentation.runOnMainSync {
        val labels = texts(activity.window.decorView)
        assertTrue(labels.any { it.contains("Camera access denied") })
        fun expand(view: android.view.View) {
          if (view is android.widget.Button && view.text == "Paste invitation") view.performClick()
          else if (view is android.view.ViewGroup)
            (0 until view.childCount).forEach { expand(view.getChildAt(it)) }
        }
        expand(activity.window.decorView)
        assertTrue(labels.contains("Use invitation"))
        assertTrue(labels.contains("Pair nearby"))
      }
    } finally {
      instrumentation.runOnMainSync { activity.finish() }
    }
  }
}
