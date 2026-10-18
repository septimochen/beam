package org.beam.android

import android.content.ClipData
import android.content.ClipboardManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.graphics.drawable.AdaptiveIconDrawable
import android.net.Uri
import android.os.IBinder
import android.provider.MediaStore
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.Button
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
    val arguments = InstrumentationRegistry.getArguments()
    if (android.os.Build.VERSION.SDK_INT >= 33 && arguments.getString("notifications") != "denied")
      instrumentation.uiAutomation.grantRuntimePermission(
        app,
        android.Manifest.permission.POST_NOTIFICATIONS,
      )
    if (android.os.Build.VERSION.SDK_INT >= 37 && arguments.getString("localNetwork") == null)
      instrumentation.uiAutomation.grantRuntimePermission(
        app,
        android.Manifest.permission.ACCESS_LOCAL_NETWORK,
      )
  }

  @Test
  fun testReceiveOnLaunch() {
    val context = instrumentation.targetContext
    var connected = CountDownLatch(1)
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
    var bound = true
    val root =
      File(context.cacheDir.canonicalFile, "launch-${UUID.randomUUID()}").apply { mkdirs() }
    val alias = "launch-${UUID.randomUUID().toString().take(8)}"
    val spareAlias = "spare-${UUID.randomUUID().toString().take(8)}"
    var activity: android.app.Activity? = null
    var downloaded: Uri? = null
    try {
      assertTrue(connected.await(10, TimeUnit.SECONDS))
      NativeSession(File(root, "desktop").path).use { desktop ->
        val identity = desktop.call(Command.IDENTITY)
        val certificate = File(root, "desktop.pem").apply { writeText(identity[2]) }
        lateinit var phone: List<String>
        instrumentation.runOnMainSync {
          phone = checkNotNull(core).call(Command.IDENTITY)
          checkNotNull(core).call(Command.PAIR, alias, certificate.path, identity[0], "")
          if (InstrumentationRegistry.getArguments().getString("notifications") == "denied") {
            context
              .getSharedPreferences("permissions", Context.MODE_PRIVATE)
              .edit()
              .putBoolean("notificationsRequested", true)
              .commit()
          }
        }
        val phoneCertificate = File(root, "phone.pem").apply { writeText(phone[2]) }
        desktop.call(Command.PAIR, "phone", phoneCertificate.path, phone[0], "127.0.0.1:4269")
        activity =
          instrumentation.startActivitySync(
            Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
          )
        val response = InstrumentationRegistry.getArguments().getString("localNetwork")
        if (android.os.Build.VERSION.SDK_INT >= 37 && response != null) {
          val button =
            if (response == "deny") "permission_deny_button" else "permission_allow_button"
          val deadline = System.nanoTime() + 10_000_000_000L
          var clicked = false
          while (!clicked && System.nanoTime() < deadline) {
            val nodes =
              instrumentation.uiAutomation.rootInActiveWindow?.findAccessibilityNodeInfosByViewId(
                "com.android.permissioncontroller:id/$button"
              )
            clicked =
              nodes?.firstOrNull()?.performAction(AccessibilityNodeInfo.ACTION_CLICK) == true
            if (!clicked) Thread.sleep(50)
          }
          assertTrue("Local network permission was not requested", clicked)
          if (response == "deny") {
            waitForService {
              checkNotNull(core).status.startsWith("Local network permission is required")
            }
            instrumentation.runOnMainSync { assertFalse(checkNotNull(core).receiving) }
            return
          }
        }
        waitForService { checkNotNull(core).status.startsWith("Receiving from paired devices") }
        desktop.call(Command.CHECK, "phone")
        assertTrue(await(desktop, "2", "2")[2].contains("reachable"))
        var beforeFiles = emptySet<String>()
        instrumentation.runOnMainSync {
          beforeFiles = checkNotNull(core).inbox.listFiles()?.map { it.name }?.toSet() ?: emptySet()
          checkNotNull(activity)
            .getSystemService(ClipboardManager::class.java)
            .setPrimaryClip(ClipData.newPlainText("fixture", "unchanged clipboard"))
        }
        desktop.call(Command.SEND, "phone", "text", "", "receive on launch 🌍")
        await(desktop, "0", "2")
        waitForService {
          checkNotNull(core).receivedText == "receive on launch 🌍"
        }
        instrumentation.runOnMainSync {
          val app = checkNotNull(activity)
          val clipboard = app.getSystemService(ClipboardManager::class.java)
          assertEquals("unchanged clipboard", clipboard.primaryClip?.getItemAt(0)?.text?.toString())
          assertEquals(
            beforeFiles,
            checkNotNull(core).inbox.listFiles()?.map { it.name }?.toSet() ?: emptySet<String>(),
          )
          fun buttons(view: View): List<Button> =
            when (view) {
              is Button -> if (view.text == "Copy to clipboard") listOf(view) else emptyList()
              is ViewGroup -> (0 until view.childCount).flatMap { buttons(view.getChildAt(it)) }
              else -> emptyList()
            }
          buttons(app.window.decorView).last().performClick()
          assertEquals(
            "receive on launch 🌍",
            clipboard.primaryClip?.getItemAt(0)?.text?.toString(),
          )
        }
        val incoming = File(root, "received-${UUID.randomUUID()}.bin")
        val payload = ByteArray(1024 * 1024) { (it % 251).toByte() }
        incoming.writeBytes(payload)
        instrumentation.runOnMainSync { checkNotNull(activity).moveTaskToBack(true) }
        instrumentation.waitForIdleSync()
        desktop.call(Command.SEND, "phone", "file", incoming.path, "")
        await(desktop, "0", "2")
        waitForService {
          checkNotNull(core).receivedFileStatus.contains("Received ${incoming.name}") &&
            checkNotNull(core).receivedFileStatus.contains("Saved to Downloads")
        }
        context.contentResolver
          .query(
            MediaStore.Downloads.EXTERNAL_CONTENT_URI,
            arrayOf(MediaStore.Downloads._ID, MediaStore.Downloads.IS_PENDING),
            "${MediaStore.Downloads.DISPLAY_NAME} = ?",
            arrayOf(incoming.name),
            null,
          )!!
          .use {
            assertTrue("Received file is missing from Downloads", it.moveToFirst())
            downloaded =
              android.content.ContentUris.withAppendedId(
                MediaStore.Downloads.EXTERNAL_CONTENT_URI,
                it.getLong(0),
              )
            assertEquals(0, it.getInt(1))
          }
        assertArrayEquals(
          payload,
          context.contentResolver.openInputStream(downloaded!!)!!.use { it.readBytes() },
        )
        activity =
          instrumentation.startActivitySync(
            Intent(context, MainActivity::class.java)
              .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
          )
        instrumentation.waitForIdleSync()
        instrumentation.runOnMainSync {
          assertFalse(File(checkNotNull(core).inbox, incoming.name).exists())
          assertEquals(100, checkNotNull(core).receivePercent)
          fun resultVisible(view: View): Boolean =
            (view is android.widget.TextView &&
              view.text.toString().contains("Received ${incoming.name}")) ||
              (view is ViewGroup &&
                (0 until view.childCount).any { resultVisible(view.getChildAt(it)) })
          assertTrue(resultVisible(checkNotNull(activity).window.decorView))
          val notifications = context.getSystemService(android.app.NotificationManager::class.java)
          if (notifications.areNotificationsEnabled()) {
            val completed = notifications.activeNotifications.first { it.id == 2 }.notification
            assertEquals(
              "File received",
              completed.extras.getString(android.app.Notification.EXTRA_TITLE),
            )
            assertEquals(
              "Saved to Downloads",
              completed.extras.getString(android.app.Notification.EXTRA_TEXT),
            )
          }
        }
        fun recreate() {
          val monitor = instrumentation.addMonitor(MainActivity::class.java.name, null, false)
          try {
            instrumentation.runOnMainSync { checkNotNull(activity).recreate() }
            activity = checkNotNull(instrumentation.waitForMonitorWithTimeout(monitor, 5000))
            instrumentation.waitForIdleSync()
          } finally {
            instrumentation.removeMonitor(monitor)
          }
        }
        recreate()
        waitForService { checkNotNull(core).receiving }
        context.startService(Intent(context, TransferService::class.java).setAction("stop"))
        waitForService { !checkNotNull(core).receiving }
        context.unbindService(connection)
        bound = false
        recreate()
        connected = CountDownLatch(1)
        context.bindService(
          Intent(context, TransferService::class.java),
          connection,
          Context.BIND_AUTO_CREATE,
        )
        bound = true
        assertTrue(connected.await(10, TimeUnit.SECONDS))
        Thread.sleep(300)
        instrumentation.runOnMainSync { assertFalse(checkNotNull(core).receiving) }
        instrumentation.runOnMainSync {
          val app = checkNotNull(core)
          assertThrows(IllegalArgumentException::class.java) {
            app.saveSettings("../outside", 4269, 30)
          }
          assertThrows(IllegalArgumentException::class.java) {
            app.saveSettings("identity", 4269, 30)
          }
          app.saveSettings("incoming", 4270, 12)
          assertEquals("incoming", app.receiveFolder)
          app.startReceiving()
        }
        waitForService { checkNotNull(core).status.contains("port 4270") }
        desktop.call(Command.ENDPOINT, "phone", "127.0.0.1:4270")
        desktop.call(Command.CHECK, "phone")
        assertTrue(await(desktop, "2", "2")[2].contains("reachable"))
        instrumentation.runOnMainSync { checkNotNull(core).stopReceiving() }
        waitForService { !checkNotNull(core).receiving }
        instrumentation.runOnMainSync { checkNotNull(core).saveSettings("received", 4269, 30) }
        NativeSession(File(root, "spare").path).use { spare ->
          val extra = spare.call(Command.IDENTITY)
          val extraCertificate = File(root, "spare.pem").apply { writeText(extra[2]) }
          instrumentation.runOnMainSync {
            checkNotNull(core).call(Command.PAIR, spareAlias, extraCertificate.path, extra[0], "")
            checkNotNull(core).startReceiving()
          }
          waitForService { checkNotNull(core).status.startsWith("Receiving from paired devices") }
          recreate()
          fun tap(label: String) {
            val deadline = System.nanoTime() + 5_000_000_000L
            while (System.nanoTime() < deadline) {
              val node =
                instrumentation.uiAutomation.rootInActiveWindow
                  ?.findAccessibilityNodeInfosByText(label)
                  ?.firstOrNull { it.isClickable && it.text?.toString().equals(label, true) }
              if (node?.performAction(AccessibilityNodeInfo.ACTION_CLICK) == true) return
              Thread.sleep(50)
            }
            error("Missing unpair control: $label")
          }
          tap("Unpair device")
          tap("Cancel")
          instrumentation.runOnMainSync {
            assertTrue(checkNotNull(core).call(Command.PEERS).chunked(3).any { it[0] == alias })
            assertTrue(checkNotNull(core).receiving)
          }
          tap("Unpair device")
          tap("Unpair")
          waitForService {
            checkNotNull(core).call(Command.PEERS).chunked(3).map { it[0] } == listOf(spareAlias) &&
              checkNotNull(core).receiving
          }
          desktop.call(Command.CHECK, "phone")
          assertTrue(
            "Removed device still authenticated",
            await(desktop, "2", "3")[2].contains("unavailable"),
          )
          instrumentation.runOnMainSync { checkNotNull(core).stopReceiving() }
          waitForService { !checkNotNull(core).receiving }
          instrumentation.runOnMainSync {
            checkNotNull(core).call(Command.PAIR, alias, certificate.path, identity[0], "")
          }
          recreate()
          tap("Unpair device")
          tap("Unpair")
          waitForService {
            checkNotNull(core).call(Command.PEERS).chunked(3).map { it[0] } == listOf(spareAlias)
          }
          Thread.sleep(300)
          instrumentation.runOnMainSync {
            assertFalse("Unpair ignored an explicit Stop", checkNotNull(core).receiving)
            checkNotNull(core).startReceiving()
          }
          waitForService { checkNotNull(core).status.startsWith("Receiving from paired devices") }
          tap("Unpair device")
          tap("Unpair")
          waitForService {
            checkNotNull(core).call(Command.PEERS).isEmpty() && !checkNotNull(core).receiving
          }
          recreate()
          instrumentation.runOnMainSync {
            assertEquals(phone, checkNotNull(core).call(Command.IDENTITY))
            assertFalse(checkNotNull(core).receiving)
          }
        }
      }
    } finally {
      instrumentation.runOnMainSync {
        core?.stopReceiving()
        activity?.finish()
      }
      waitForService { core?.receiving != true }
      instrumentation.runOnMainSync {
        for (name in listOf(alias, spareAlias)) {
          if (core?.call(Command.PEERS)?.chunked(3)?.any { it[0] == name } == true)
            core?.call(Command.UNPAIR, name)
        }
      }
      if (bound) context.unbindService(connection)
      downloaded?.let { context.contentResolver.delete(it, null, null) }
      root.deleteRecursively()
    }
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
          for (kind in listOf("text", "file")) {
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
            assertEquals(
              content,
              if (kind == "file") File(output, result[3]).readText() else result[10],
            )
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
      val events = session.call(Command.EVENTS).chunked(11)
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
    while (session.call(Command.BUSY) != listOf("0", "0", "0")) {
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
