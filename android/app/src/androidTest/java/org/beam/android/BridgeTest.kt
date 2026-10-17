package org.beam.android

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.graphics.drawable.AdaptiveIconDrawable
import android.net.Uri
import android.os.IBinder
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test

class BridgeTest {
  private val instrumentation
    get() = InstrumentationRegistry.getInstrumentation()

  @Before
  fun setUp() {
    val app = instrumentation.targetContext.packageName
    if (android.os.Build.VERSION.SDK_INT >= 33)
      instrumentation.uiAutomation.grantRuntimePermission(
        app,
        android.Manifest.permission.POST_NOTIFICATIONS,
      )
    if (android.os.Build.VERSION.SDK_INT >= 37)
      instrumentation.uiAutomation.grantRuntimePermission(
        app,
        android.Manifest.permission.ACCESS_LOCAL_NETWORK,
      )
  }

  @Test
  fun testPackagedLauncherIcon() {
    val context = instrumentation.targetContext
    val icon = context.packageManager.getApplicationIcon(context.packageName)
    assertTrue(icon is AdaptiveIconDrawable)
    icon as AdaptiveIconDrawable
    assertNotNull(icon.foreground)
    assertNotNull(icon.background)
    if (android.os.Build.VERSION.SDK_INT >= 33) assertNotNull(icon.monochrome)
    assertNotNull(context.getDrawable(R.drawable.ic_beam_monochrome))
  }

  @Test
  fun testForegroundProviderSendAndStop() {
    val context = instrumentation.targetContext
    val activity =
      instrumentation.startActivitySync(
        Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
      )
    val connected = CountDownLatch(1)
    var core: TransferService? = null
    val connection =
      object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
          core = (binder as TransferService.LocalBinder).service
          connected.countDown()
        }

        override fun onServiceDisconnected(name: ComponentName?) {}
      }
    context.bindService(
      Intent(context, TransferService::class.java),
      connection,
      Context.BIND_AUTO_CREATE,
    )
    val root =
      File(context.cacheDir.canonicalFile, "provider-${UUID.randomUUID()}").apply { mkdirs() }
    val alias = "fixture-${UUID.randomUUID().toString().take(8)}"
    try {
      assertTrue(connected.await(10, TimeUnit.SECONDS))
      var emptyTrust = false
      instrumentation.runOnMainSync {
        emptyTrust = checkNotNull(core).call(Command.PEERS).isEmpty()
        if (emptyTrust) checkNotNull(core).startReceiving()
      }
      if (emptyTrust)
        waitForService {
          !checkNotNull(core).receiving && checkNotNull(core).status.contains("no trusted peers")
        }
      NativeSession(File(root, "desktop").path).use { desktop ->
        lateinit var identity: List<String>
        instrumentation.runOnMainSync { identity = checkNotNull(core).call(Command.IDENTITY) }
        val phoneCert = File(root, "phone.pem").apply { writeText(identity[2]) }
        val desktopIdentity = desktop.call(Command.IDENTITY)
        val desktopCert = File(root, "desktop.pem").apply { writeText(desktopIdentity[2]) }
        desktop.call(Command.PAIR, "phone", phoneCert.path, identity[0], "")
        val received = File(root, "received").apply { mkdirs() }
        desktop.call(Command.RECEIVE, received.path)
        await(desktop, "1", "0")
        instrumentation.runOnMainSync {
          checkNotNull(core)
            .call(Command.PAIR, alias, desktopCert.path, desktopIdentity[0], "127.0.0.1:4269")
          checkNotNull(core)
            .send(
              alias,
              "file",
              Uri.parse("content://org.beam.android.test.fixture/sample"),
              "provider.txt",
              "",
            )
        }
        val result = await(desktop, "1", "2")
        assertEquals(FixtureProvider.CONTENT, File(received, result[3]).readText())
        val deadline = System.nanoTime() + 5_000_000_000L
        var busy = true
        while (busy && System.nanoTime() < deadline) {
          instrumentation.runOnMainSync { busy = checkNotNull(core).sending }
          Thread.sleep(20)
        }
        assertFalse(busy)
        desktop.call(Command.STOP)
        idle(desktop)
        instrumentation.runOnMainSync {
          assertTrue(File(context.filesDir, "outbox").listFiles()?.isEmpty() != false)
          checkNotNull(core).startReceiving()
        }
        waitForService { checkNotNull(core).status.startsWith("Receiving from paired devices") }
        instrumentation.runOnMainSync {
          checkNotNull(core).stopReceiving()
        }
        waitForService { !checkNotNull(core).receiving }
        instrumentation.runOnMainSync { checkNotNull(core).call(Command.UNPAIR, alias) }
      }
    } finally {
      instrumentation.runOnMainSync {
        core?.cancelSend()
        core?.stopReceiving()
        activity.finish()
      }
      context.unbindService(connection)
      root.deleteRecursively()
    }
  }

  @Test
  fun testPairedJniTransfersPreserveUtf8AndIdentity() {
    val root =
      File(instrumentation.targetContext.cacheDir.canonicalFile, "jni-${UUID.randomUUID()}")
    check(root.mkdirs())
    try {
      NativeSession(File(root, "phone").path).use { phone ->
        NativeSession(File(root, "desktop").path).use { desktop ->
          val phoneIdentity = phone.call(Command.IDENTITY)
          val desktopIdentity = desktop.call(Command.IDENTITY)
          val phoneCert = File(root, "phone.pem").apply { writeText(phoneIdentity[2]) }
          val desktopCert = File(root, "desktop.pem").apply { writeText(desktopIdentity[2]) }
          phone.call(
            Command.PAIR,
            "desktop",
            desktopCert.path,
            desktopIdentity[0],
            "127.0.0.1:4269",
          )
          desktop.call(Command.PAIR, "phone", phoneCert.path, phoneIdentity[0], "127.0.0.1:4269")
          val output = File(root, "received").apply { mkdirs() }
          for (kind in listOf("text", "url", "file")) {
            val content = if (kind == "url") "https://example.com/a%20b" else "Hello 🌍\n世界"
            val source = File(root, "sample.txt").apply { writeText(content) }
            phone.call(Command.RECEIVE, output.path)
            await(phone, "1", "0")
            desktop.call(
              Command.SEND,
              "phone",
              kind,
              if (kind == "file") source.path else "",
              if (kind == "file") "" else content,
            )
            val result = await(phone, "1", "2")
            await(desktop, "0", "2")
            assertEquals(content, File(output, result[3]).readText())
            phone.call(Command.STOP)
            idle(phone)
            idle(desktop)
            phone.call(Command.EVENTS)
          }
          phone.call(Command.UNPAIR, "desktop")
          assertTrue(phone.call(Command.PEERS).isEmpty())
          NativeSession(File(root, "phone").path).use { reopened ->
            assertEquals(phoneIdentity, reopened.call(Command.IDENTITY))
          }
        }
      }
    } finally {
      root.deleteRecursively()
    }
  }

  @Test
  fun testShareTargetOnlyReviewsText() {
    val intent =
      Intent(instrumentation.targetContext, MainActivity::class.java).apply {
        action = Intent.ACTION_SEND
        type = "text/plain"
        putExtra(Intent.EXTRA_TEXT, "Review 🌍 before sending")
        addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
      }
    val activity = instrumentation.startActivitySync(intent)
    instrumentation.waitForIdleSync()
    try {
      instrumentation.runOnMainSync {
        fun find(view: View): Boolean {
          if (view is EditText && view.text.toString() == "Review 🌍 before sending") return true
          return view is ViewGroup && (0 until view.childCount).any { find(view.getChildAt(it)) }
        }
        assertTrue(find(activity.window.decorView))
        val outbox = File(activity.filesDir, "outbox")
        assertTrue(outbox.listFiles()?.isEmpty() != false)
      }
    } finally {
      instrumentation.runOnMainSync { activity.finish() }
    }
  }

  private fun await(session: NativeSession, direction: String, kind: String): List<String> {
    val deadline = System.nanoTime() + 15_000_000_000L
    while (System.nanoTime() < deadline) {
      val events = session.call(Command.EVENTS).chunked(8)
      events
        .firstOrNull { it[0] == direction && it[1] == kind }
        ?.let {
          return it
        }
      events.firstOrNull { it[1] == "3" }?.let { error(it[2]) }
      Thread.sleep(10)
    }
    error("JNI transfer event timed out")
  }

  private fun idle(session: NativeSession) {
    val deadline = System.nanoTime() + 5_000_000_000L
    while (session.call(Command.BUSY) != listOf("0", "0")) {
      check(System.nanoTime() < deadline) { "Native workers did not stop" }
      Thread.sleep(10)
    }
  }

  private fun waitForService(condition: () -> Boolean) {
    val deadline = System.nanoTime() + 5_000_000_000L
    while (System.nanoTime() < deadline) {
      var ready = false
      instrumentation.runOnMainSync { ready = condition() }
      if (ready) return
      Thread.sleep(20)
    }
    error("Foreground service state timed out")
  }
}
