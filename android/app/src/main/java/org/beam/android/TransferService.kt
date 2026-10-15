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
  private var session: NativeSession? = null
  private var preparation: Future<*>? = null
  private var cancellation: CancellationSignal? = null
  @Volatile private var input: InputStream? = null
  private var generation = 0
  private var foreground = false
  private var staged: File? = null
  private var destroyed = false
  internal var listener: (() -> Unit)? = null
  internal var status = "Choose content and a paired device"
    private set

  internal var sending = false
    private set

  internal var receiving = false
    private set

  internal val inbox: File
    get() = File(filesDir, "received")

  override fun onCreate() {
    super.onCreate()
    getSystemService(NotificationManager::class.java)
      .createNotificationChannel(
        NotificationChannel("transfers", "Beam transfers", NotificationManager.IMPORTANCE_LOW)
      )
    try {
      // Canonicalize /data/user/0 aliases before the core checks each ancestor.
      session = NativeSession(File(filesDir.canonicalFile, "identity").path)
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

  private fun notification(): Notification {
    val open =
      PendingIntent.getActivity(
        this,
        0,
        Intent(this, MainActivity::class.java),
        PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
      )
    val stop =
      PendingIntent.getService(
        this,
        1,
        Intent(this, TransferService::class.java).setAction("stop"),
        PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
      )
    return Notification.Builder(this, "transfers")
      .setSmallIcon(android.R.drawable.ic_menu_share)
      .setContentTitle("Beam")
      .setContentText(
        if (receiving) "Receiving from paired devices on port 4269"
        else "Sending to a paired device"
      )
      .setContentIntent(open)
      .setOngoing(true)
      .addAction(Notification.Action.Builder(null, "Stop", stop).build())
      .build()
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
    beginForeground()
    try {
      call(Command.RECEIVE, inbox.canonicalPath)
      receiving = true
      getSystemService(NotificationManager::class.java).notify(1, notification())
      status = "Starting receiver…"
    } catch (error: Exception) {
      finishIdle()
      throw error
    }
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
    session?.call(Command.STOP)
    status = "Stopping receiver…"
  }

  private fun cleanupStaged() {
    staged?.deleteRecursively()
    staged = null
  }

  private fun finishIdle() {
    if (!sending && !receiving && foreground) {
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
            native.call(Command.EVENTS).chunked(8).forEach { event ->
              status =
                when (event[1]) {
                  "0" -> "Receiving from paired devices on port 4269"
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
                    "${stages[event[5].toInt()]}: ${event[6]} / ${event[7]} bytes"
                  }
                  "2" ->
                    "Verified ${if (event[0] == "0") "sent" else "received"}: ${event[3]} (${event[4]} bytes)"
                  "3",
                  "4" -> event[2]
                  else -> "Receiver stopped"
                }
            }
            val wasReceiving = receiving
            val busy = native.call(Command.BUSY)
            sending = preparation != null || busy[0] == "1"
            receiving = busy[1] == "1"
            if (foreground && receiving != wasReceiving && (sending || receiving))
              getSystemService(NotificationManager::class.java).notify(1, notification())
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
    cleanupStaged()
    super.onDestroy()
  }
}
