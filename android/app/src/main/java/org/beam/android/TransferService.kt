package org.beam.android

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.Uri
import android.os.Binder
import android.os.CancellationSignal
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.ParcelFileDescriptor
import androidx.core.content.edit
import java.io.File
import java.io.InputStream
import java.util.UUID
import java.util.concurrent.Executors
import java.util.concurrent.Future

class TransferService : Service() {
  inner class LocalBinder : Binder() {
    val service: TransferService
      get() = this@TransferService
  }

  private val binder = LocalBinder()
  private val main = Handler(Looper.getMainLooper())
  private val executor = Executors.newSingleThreadExecutor()
  private val downloads = Executors.newSingleThreadExecutor()
  private val saving = mutableSetOf<String>()
  private var receiveActive = false
  private var session: NativeSession? = null
  private var preparation: Future<*>? = null
  private var cancellation: CancellationSignal? = null
  @Volatile private var input: InputStream? = null
  private var generation = 0
  private var foreground = false
  private var lastNotification = 0L
  private var staged: File? = null
  private var destroyed = false
  internal var listener: (() -> Unit)? = null
  internal var receiveOnLaunchPending = true
  internal var status = "Choose content and a paired device"
    private set

  internal var sending = false
    private set

  internal var receiving = false
    private set

  internal var checking = false
    private set

  internal var port = 4269
    private set

  internal var timeoutSeconds = 30
    private set

  internal var receiveFolder = "received"
    private set

  internal var receivedText: String? = null
    private set

  internal var sendPercent = 0
    private set

  internal var receivePercent = 0
    private set

  internal var sendStatus = "Send: idle"
    private set

  internal var receiveStatus = "Receive: idle"
    private set

  internal var receivedFileStatus = "Received files save to Downloads"
    private set

  internal var connectivityStatus = ""
    private set

  internal val inbox: File
    get() = File(filesDir, receiveFolder)

  override fun onCreate() {
    super.onCreate()
    getSystemService(NotificationManager::class.java)
      .createNotificationChannel(
        NotificationChannel("transfers", "Beam transfers", NotificationManager.IMPORTANCE_LOW)
      )
    getSystemService(NotificationManager::class.java)
      .createNotificationChannel(
        NotificationChannel(
          "received",
          "Received files and text",
          NotificationManager.IMPORTANCE_DEFAULT,
        )
      )
    try {
      // Canonicalize /data/user/0 aliases before the core checks each ancestor.
      val settings = getSharedPreferences("settings", MODE_PRIVATE)
      port = settings.getInt("port", 4269)
      timeoutSeconds = settings.getInt("timeout", 30)
      receiveFolder = settings.getString("folder", "received") ?: "received"
      receivedFileStatus =
        settings.getString("lastReceivedFile", receivedFileStatus) ?: receivedFileStatus
      validateSettings(receiveFolder, port, timeoutSeconds)
      session =
        NativeSession(File(filesDir.canonicalFile, "identity").path, port, timeoutSeconds * 1000)
      inbox.mkdirs()
      File(filesDir, "outbox").apply {
        deleteRecursively()
        mkdirs()
      }
    } catch (error: Exception) {
      status = error.message ?: "Beam initialization failed"
    }
    main.post(poll)
  }

  internal fun call(command: Int, vararg arguments: String): List<String> =
    checkNotNull(session) { status }.call(command, *arguments)

  internal fun report(message: String) {
    status = message
    listener?.invoke()
  }

  private fun openApp(): PendingIntent =
    PendingIntent.getActivity(
      this,
      0,
      Intent(this, MainActivity::class.java),
      PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
    )

  private fun receivedNotification(success: Boolean, text: Boolean = false) {
    try {
      getSystemService(NotificationManager::class.java)
        .notify(
          2,
          Notification.Builder(this, "received")
            .setSmallIcon(R.drawable.ic_beam_monochrome)
            .setContentTitle(
              if (success) if (text) "Text received" else "File received" else "File needs saving"
            )
            .setContentText(
              if (success) if (text) "Open Beam to view received text" else "Saved to Downloads"
              else "Open Beam to recover the verified file"
            )
            .setContentIntent(openApp())
            .setAutoCancel(true)
            .setVisibility(Notification.VISIBILITY_PRIVATE)
            .build(),
        )
    } catch (_: SecurityException) {
      // Notification permission is optional; the in-app result remains visible.
    }
  }

  private fun notification(): Notification {
    val stop =
      PendingIntent.getService(
        this,
        1,
        Intent(this, TransferService::class.java).setAction("stop"),
        PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
      )
    val builder =
      Notification.Builder(this, "transfers")
        .setSmallIcon(R.drawable.ic_beam_monochrome)
        .setContentTitle("Beam")
        .setContentText(
          when {
            saving.isNotEmpty() -> "Saving verified file to Downloads…"
            receiveActive -> receiveStatus
            sending -> sendStatus
            receiving -> "Ready to receive · files save to Downloads"
            else -> "Checking peer connectivity"
          }
        )
        .setContentIntent(openApp())
        .setOngoing(true)
        .setOnlyAlertOnce(true)
        .addAction(Notification.Action.Builder(null, "Stop", stop).build())
    if (saving.isNotEmpty()) builder.setProgress(0, 0, true)
    else if (receiveActive || sending)
      builder.setProgress(100, if (receiveActive) receivePercent else sendPercent, false)
    return builder.build()
  }

  private fun beginForeground() {
    startForegroundService(Intent(this, TransferService::class.java))
    startForeground(1, notification(), ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
    foreground = true
  }

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    if (intent?.action == "stop") {
      cancelSend()
      stopReceiving()
    }
    return START_NOT_STICKY
  }

  internal fun send(peer: String, kind: String, uri: Uri?, name: String, content: String) {
    check(!sending) { "A send is already running" }
    require(peer.isNotEmpty()) { "Choose a paired device" }
    if (kind != "file") {
      beginForeground()
      try {
        call(Command.SEND, peer, kind, "", Selection.text(content))
        sending = true
      } catch (error: Exception) {
        finishIdle()
        throw error
      }
      return
    }
    requireNotNull(uri) { "Choose a file first" }
    Selection.contentUri(uri.scheme, uri.authority)
    Selection.filename(name)
    beginForeground()
    sending = true
    status = "Preparing selected file…"
    val ticket = ++generation
    val signal = CancellationSignal()
    cancellation = signal
    preparation = executor.submit {
      val directory = File(File(filesDir, "outbox"), UUID.randomUUID().toString())
      try {
        check(directory.mkdirs()) { "Cannot create private staging directory" }
        val file = File(directory, name)
        val descriptor =
          contentResolver.openFileDescriptor(uri, "r", signal) ?: error("Cannot read selected file")
        ParcelFileDescriptor.AutoCloseInputStream(descriptor).use { source ->
          input = source
          file.outputStream().use { output ->
            Selection.copy(source, output, Selection.MAX_FILE) { signal.isCanceled }
          }
        }
        main.post {
          if (destroyed || ticket != generation) directory.deleteRecursively()
          else {
            preparation = null
            cancellation = null
            staged = directory
            try {
              call(Command.SEND, peer, "file", file.canonicalPath, "")
            } catch (error: Exception) {
              sending = false
              status = error.message ?: "Send failed"
              cleanupStaged()
            }
          }
        }
      } catch (error: Exception) {
        directory.deleteRecursively()
        main.post {
          if (!destroyed && ticket == generation) {
            preparation = null
            cancellation = null
            sending = false
            status = error.message ?: "Cannot prepare file"
          }
        }
      } finally {
        input = null
      }
    }
  }

  internal fun startReceiving() {
    check(!receiving) { "Receiver is already running" }
    receiveOnLaunchPending = false
    beginForeground()
    try {
      call(Command.RECEIVE, inbox.canonicalPath)
      receiving = true
      getSystemService(NotificationManager::class.java).notify(1, notification())
      status = "Starting receiver…"
      receiveStatus = status
    } catch (error: Exception) {
      finishIdle()
      throw error
    }
  }

  internal fun checkConnectivity(peer: String) {
    check(!checking) { "A connectivity check is already running" }
    beginForeground()
    try {
      call(Command.CHECK, peer)
      checking = true
      connectivityStatus = "Checking $peer…"
    } catch (error: Exception) {
      finishIdle()
      throw error
    }
  }

  private fun validateSettings(folder: String, port: Int, timeout: Int) {
    Selection.filename(folder)
    require(folder != "identity" && folder != "outbox") { "Choose a separate receive folder" }
    require(port in 1..65535 && timeout in 1..86400) { "Invalid port or timeout" }
  }

  @android.annotation.SuppressLint("UseKtx") // Preserve the checked synchronous commit result.
  internal fun saveSettings(folder: String, port: Int, timeout: Int) {
    check(!sending && !receiving && !checking && saving.isEmpty()) {
      "Stop transfers and connectivity checks before editing settings"
    }
    validateSettings(folder, port, timeout)
    val destination = File(filesDir, folder)
    check(destination.isDirectory || destination.mkdir()) { "Cannot create receive folder" }
    val replacement =
      NativeSession(File(filesDir.canonicalFile, "identity").path, port, timeout * 1000)
    if (
      !getSharedPreferences("settings", MODE_PRIVATE)
        .edit()
        .putString("folder", folder)
        .putInt("port", port)
        .putInt("timeout", timeout)
        .commit()
    ) {
      replacement.close()
      error("Cannot save settings")
    }
    session?.close()
    session = replacement
    receiveFolder = folder
    this.port = port
    timeoutSeconds = timeout
    report("Settings saved. Start receiving to apply them now.")
  }

  internal fun cancelSend() {
    ++generation
    cancellation?.cancel()
    try {
      input?.close()
    } catch (_: Exception) {
      /* Cancellation also closes the provider FD. */
    }
    preparation?.cancel(true)
    preparation = null
    cancellation = null
    session?.call(Command.CANCEL)
    // The native worker retains ownership until it reports idle.
    status = "Cancelling send…"
  }

  internal fun stopReceiving() {
    receiveOnLaunchPending = false
    session?.call(Command.STOP)
    status = "Stopping receiver…"
  }

  internal fun unpair(peer: String) {
    require(call(Command.PEERS).chunked(3).any { it[0] == peer }) { "Device is no longer paired" }
    mutateDevices(Command.UNPAIR, peer)
    report("Unpaired $peer on this device. Pair again to reconnect.")
  }

  internal fun mutateDevices(command: Int, vararg arguments: String): List<String> {
    require(
      command in listOf(Command.EDIT_DEVICE, Command.IMPORT_DEVICES, Command.PAIR, Command.UNPAIR)
    )
    val resume = receiving || receiveOnLaunchPending
    cancelSend()
    try {
      return call(command, *arguments)
    } finally {
      val busy = call(Command.BUSY)
      sending = busy[0] == "1"
      receiving = busy[1] == "1"
      checking = busy[2] == "1"
      if (!sending) cleanupStaged()
      receiveOnLaunchPending = resume
      connectivityStatus = ""
      finishIdle()
    }
  }

  private fun cleanupStaged() {
    staged?.deleteRecursively()
    staged = null
  }

  private fun saveReceivedFile(name: String) {
    Selection.filename(name)
    if (!saving.add(name)) return
    val source = File(inbox, name)
    receivedFileStatus = "Saving $name to Downloads…"
    downloads.submit {
      var message: String
      var success = false
      try {
        val saved =
          ReceivedDownloads(contentResolver).save(source) { percent ->
            main.post {
              if (!destroyed) {
                receivedFileStatus = "Saving $name to Downloads: $percent%"
                listener?.invoke()
              }
            }
          }
        success = true
        message = "Received ${saved.name} · Saved to Downloads"
        if (!source.delete()) message += " · Temporary copy could not be removed"
      } catch (error: Exception) {
        message =
          "Could not save $name to Downloads: ${error.message}. Use Recover an unsaved file."
      }
      main.post {
        saving.remove(name)
        if (!destroyed) {
          receivedFileStatus = message
          getSharedPreferences("settings", MODE_PRIVATE).edit {
            putString("lastReceivedFile", message)
          }
          status = message
          receivedNotification(success)
          listener?.invoke()
          finishIdle()
        }
      }
    }
  }

  private fun finishIdle() {
    if (!sending && !receiving && !checking && saving.isEmpty() && foreground) {
      stopForeground(STOP_FOREGROUND_REMOVE)
      foreground = false
      stopSelf()
    }
  }

  private val poll =
    object : Runnable {
      override fun run() {
        try {
          session?.let { native ->
            native.call(Command.EVENTS).chunked(11).forEach { event ->
              if (event[0] == "2") {
                connectivityStatus = event[2]
                return@forEach
              }
              status =
                when (event[1]) {
                  "0" -> {
                    // Listening restarts after each transfer. Keep its result visible.
                    if (receiveStatus == "Starting receiver…")
                      receiveStatus = "Receiving from paired devices on port $port"
                    receiveStatus
                  }
                  "1" -> {
                    val stages =
                      listOf(
                        "Hashing",
                        "Connecting",
                        "Waiting",
                        "Transferring",
                        "Verifying",
                        "Complete",
                      )
                    val percent =
                      if (event[7].toLong() == 0L) 0
                      else (100.0 * event[6].toDouble() / event[7].toDouble()).toInt()
                    if (event[0] == "0") sendPercent = percent
                    else {
                      receivePercent = percent
                      receiveActive = event[5] != "5"
                    }
                    "${if (event[0] == "1") "Receive · " else ""}${stages[event[5].toInt()]}: $percent% · " +
                      String.format(
                        java.util.Locale.ROOT,
                        "%.1f MiB/s",
                        event[8].toDouble() / 1048576,
                      )
                  }
                  "2" ->
                    "Verified ${if (event[0] == "0") "sent" else "received"}: ${event[3]} (${event[4]} bytes)"
                  "3",
                  "4" -> event[2]
                  "5" -> if (status == "Stopping receiver…") "Receiver stopped" else status
                  else -> status
                }
              if (event[1] == "2") {
                if (event[0] == "0") sendPercent = 100 else receivePercent = 100
                if (event[0] == "1") {
                  receiveActive = false
                  if (event[9] == "2") {
                    receivedText = event[10]
                    receivedNotification(true, text = true)
                  } else saveReceivedFile(event[3])
                }
              }
              if (event[0] == "1" && event[1] in listOf("3", "5")) receiveActive = false
              if (event[0] == "0") sendStatus = status else receiveStatus = status
            }
            val wasReceiving = receiving
            val busy = native.call(Command.BUSY)
            sending = preparation != null || busy[0] == "1"
            receiving = busy[1] == "1"
            checking = busy[2] == "1"
            val now = android.os.SystemClock.elapsedRealtime()
            if (
              foreground &&
                (sending || receiving || checking) &&
                (receiving != wasReceiving || now - lastNotification >= 250)
            ) {
              getSystemService(NotificationManager::class.java).notify(1, notification())
              lastNotification = now
            }
            if (!sending) cleanupStaged()
          }
          finishIdle()
          listener?.invoke()
        } catch (error: Exception) {
          status = error.message ?: "Beam operation failed"
        }
        main.postDelayed(this, 100)
      }
    }

  override fun onBind(intent: Intent?): IBinder = binder

  override fun onDestroy() {
    destroyed = true
    listener = null
    main.removeCallbacksAndMessages(null)
    cancelSend()
    session?.close() // Cancels and joins both native workers before deleting their mailbox.
    session = null
    executor.shutdownNow()
    downloads.shutdownNow()
    cleanupStaged()
    super.onDestroy()
  }
}
