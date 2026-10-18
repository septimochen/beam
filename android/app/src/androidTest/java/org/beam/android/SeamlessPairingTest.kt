package org.beam.android

import android.Manifest
import android.os.Build
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.util.UUID
import org.junit.Assert.*
import org.junit.Test

class SeamlessPairingTest {
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
        assertTrue(labels.contains("Use invitation"))
        assertTrue(labels.contains("Pair nearby"))
      }
    } finally {
      instrumentation.runOnMainSync { activity.finish() }
    }
  }
}
