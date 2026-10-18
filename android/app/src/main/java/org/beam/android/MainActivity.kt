package org.beam.android

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.provider.OpenableColumns
import android.text.InputFilter
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.Spinner
import android.widget.TextView
import androidx.core.content.edit
import java.util.UUID

@SuppressLint("SetTextI18n") // Status strings include native errors and byte counts.
class MainActivity : Activity() {
  private var service: TransferService? = null
  private var bound = false
  private lateinit var devices: Spinner
  private lateinit var review: EditText
  private lateinit var filename: EditText
  private lateinit var status: TextView
  private lateinit var send: Button
  private lateinit var receive: Button
  private lateinit var kinds: RadioGroup
  private lateinit var sendProgress: ProgressBar
  private lateinit var receiveProgress: ProgressBar
  private lateinit var sendProgressLabel: TextView
  private lateinit var receiveProgressLabel: TextView
  private lateinit var receivedText: EditText
  private lateinit var outgoingHeader: LinearLayout
  private lateinit var fileContent: LinearLayout
  private lateinit var receivedContent: LinearLayout
  private lateinit var chooseFile: Button
  private lateinit var retrySaving: Button
  private lateinit var cancelSend: Button
  private val ui by lazy { BeamUi(this) }
  private var uri: Uri? = null
  private var peerNames = emptyList<String>()
  private val kindIds = listOf(R.id.kind_file, R.id.kind_text)
  private var pendingNetwork: (() -> Unit)? = null
  private var pendingReceive = false
  private var requestingPermissions = false
  private var resumed = false
  private var receiveOnLaunchPending = true

  private val connection =
    object : ServiceConnection {
      override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
        service = (binder as TransferService.LocalBinder).service
        service?.clearReceiveResult()
        service?.listener = { update() }
        attempt {
          refreshPeers()
          startPendingReceive()
        }
        update()
      }

      override fun onServiceDisconnected(name: ComponentName?) {
        service = null
      }
    }

  override fun onCreate(savedInstanceState: Bundle?) {
    ui.applyTheme()
    super.onCreate(savedInstanceState)
    buildScreen()
    if (savedInstanceState != null) {
      // Keep Stop through activity recreation; a new process gets a fresh
      // automatic attempt only when the user opens the app again.
      if (savedInstanceState.getString("processSession") == processSession) {
        pendingReceive = savedInstanceState.getBoolean("pendingReceive")
        requestingPermissions = savedInstanceState.getBoolean("requestingPermissions")
        receiveOnLaunchPending = savedInstanceState.getBoolean("receiveOnLaunchPending", true)
      }
      uri = savedInstanceState.getString("uri")?.let(Uri::parse)
      filename.setText(savedInstanceState.getString("filename", ""))
      review.setText(savedInstanceState.getString("text", ""))
      kinds.check(savedInstanceState.getInt("kind", R.id.kind_file))
    } else acceptShare(intent)
    updateContent()
  }

  private fun button(title: String, primary: Boolean = false, action: () -> Unit) =
    ui.button(title, primary) { attempt(action) }

  private fun buildScreen() {
    val column = ui.screen()
    column.addView(ui.label("Beam", 30f, true))
    val navigation = ui.actionRow()
    navigation.addView(button("Manage devices") { manage() }, ui.weight())
    navigation.addView(button("Settings") { settings() }, ui.weight())
    column.addView(navigation)
    status = ui.message(13f)
    column.addView(status)

    val outgoing = ui.card().apply { id = R.id.send_card }
    column.addView(outgoing)
    outgoing.addView(ui.label("Send", 22f, true))
    devices =
      Spinner(this).apply {
        contentDescription = "Paired device"
        minimumHeight = ui.dp(48)
        backgroundTintList = ColorStateList.valueOf(ui.accent)
        outgoing.addView(this, LinearLayout.LayoutParams(-1, -2))
      }
    kinds =
      RadioGroup(this).apply {
        contentDescription = "Content type"
        orientation = RadioGroup.HORIZONTAL
        listOf("File", "Text").forEachIndexed { index, title ->
          addView(
            RadioButton(context).apply {
              id = kindIds[index]
              text = title
              textSize = 14f
              gravity = android.view.Gravity.CENTER
              buttonDrawable = null
              minHeight = ui.dp(48)
              setTextColor(
                ColorStateList(
                  arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf()),
                  intArrayOf(ui.surface, ui.accent),
                )
              )
              background = ui.rounded(ui.surface)
              backgroundTintList =
                ColorStateList(
                  arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf()),
                  intArrayOf(ui.accent, ui.surface),
                )
            },
            ui.weight(),
          )
        }
        check(R.id.kind_file)
        outgoing.addView(this)
      }
    fileContent =
      ui.column().apply {
        layoutParams =
          LinearLayout.LayoutParams(-1, -2).apply {
            topMargin = ui.dp(8)
            bottomMargin = ui.dp(8)
          }
      }
    outgoing.addView(fileContent)
    chooseFile = button("Choose file") { openDocument(PICK_FILE) }
    fileContent.addView(chooseFile)
    filename =
      EditText(this).apply {
        hint = "Filename"
        contentDescription = "Outgoing filename"
        setSingleLine()
        filters = arrayOf(InputFilter.LengthFilter(255))
        ui.styleInput(this)
        visibility = View.GONE
        fileContent.addView(this)
      }
    outgoingHeader = copyRow("Text to send", outgoing) { review.text.toString() }
    review =
      EditText(this).apply {
        hint = "Write or paste text"
        contentDescription = "Review outgoing text"
        minLines = 3
        maxLines = 8
        inputType =
          android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE
        filters = arrayOf(InputFilter.LengthFilter(Selection.MAX_TEXT))
        ui.styleInput(this)
        outgoing.addView(this)
      }
    kinds.setOnCheckedChangeListener { _, _ -> updateContent() }
    val sendActions = ui.actionRow()
    send =
      button("Send", true) {
        val peer =
          peerNames.getOrNull(devices.selectedItemPosition)
            ?: error("Pair and choose a device first")
        val kind = listOf("file", "text")[kindIds.indexOf(kinds.checkedRadioButtonId)]
        val selectedUri = uri
        val selectedName = filename.text.toString()
        val selectedText = review.text.toString()
        withNetwork {
          checkNotNull(service) { "Beam is starting" }
            .send(peer, kind, selectedUri, selectedName, selectedText)
        }
        update()
      }
    send.isEnabled = false
    cancelSend = button("Cancel send") { service?.cancelSend() }.apply { visibility = View.GONE }
    sendActions.addView(send, ui.weight())
    sendActions.addView(cancelSend, ui.weight())
    outgoing.addView(sendActions)
    sendProgress = ui.progress("Outgoing transfer progress").apply { visibility = View.GONE }
    outgoing.addView(sendProgress)
    sendProgressLabel = ui.message(13f)
    outgoing.addView(sendProgressLabel)

    val incoming = ui.card().apply { id = R.id.receive_card }
    column.addView(incoming)
    incoming.addView(ui.label("Receive", 22f, true))
    receiveProgressLabel = ui.message(13f)
    incoming.addView(receiveProgressLabel)
    receiveProgress = ui.progress("Incoming transfer progress").apply { visibility = View.GONE }
    incoming.addView(receiveProgress)
    val receiveActions = ui.actionRow()
    receive =
      button("Start receiving") {
        val core = checkNotNull(service) { "Beam is starting" }
        if (core.receiving) {
          receiveOnLaunchPending = false
          core.stopReceiving()
        } else requestReceiving()
        update()
      }
    receiveActions.addView(receive, ui.weight())
    receiveActions.addView(
      button("Downloads") {
        startActivity(Intent(android.app.DownloadManager.ACTION_VIEW_DOWNLOADS))
      },
      ui.weight(),
    )
    incoming.addView(receiveActions)
    retrySaving = button("Retry saving") { checkNotNull(service).retrySavingReceivedFiles() }
    retrySaving.visibility = View.GONE
    incoming.addView(retrySaving)
    receivedContent = ui.column().apply { visibility = View.GONE }
    incoming.addView(receivedContent)
    receivedContent.addView(ui.divider())
    copyRow("Received text", receivedContent) { receivedText.text.toString() }
    receivedText =
      EditText(this).apply {
        contentDescription = "Received text"
        minLines = 3
        maxLines = 8
        keyListener = null
        setTextIsSelectable(true)
        ui.styleInput(this)
        receivedContent.addView(this)
      }
  }

  private fun copyRow(
    title: String,
    parent: LinearLayout,
    content: () -> String,
  ): LinearLayout {
    val row = ui.row()
    row.addView(ui.label(title, 14f, true), ui.weight())
    row.addView(
      button("Copy") {
        getSystemService(ClipboardManager::class.java)
          .setPrimaryClip(ClipData.newPlainText("Beam text", content()))
      },
      ui.weight(),
    )
    parent.addView(row)
    (row.getChildAt(1) as Button).contentDescription = "Copy $title to clipboard"
    return row
  }

  override fun onResume() {
    super.onResume()
    resumed = true
    service?.clearReceiveResult()
    attempt {
      startPendingReceive()
      receiveOnLaunch()
    }
  }

  override fun onPause() {
    resumed = false
    super.onPause()
  }

  override fun onStart() {
    super.onStart()
    bound =
      bindService(Intent(this, TransferService::class.java), connection, Context.BIND_AUTO_CREATE)
  }

  override fun onStop() {
    service?.listener = null
    service = null
    if (bound) unbindService(connection)
    bound = false
    super.onStop()
  }

  override fun onSaveInstanceState(outState: Bundle) {
    outState.putString("uri", uri?.toString())
    outState.putString("filename", filename.text.toString())
    outState.putString("text", review.text.toString())
    outState.putInt("kind", kinds.checkedRadioButtonId)
    outState.putBoolean("pendingReceive", pendingReceive)
    outState.putBoolean("requestingPermissions", requestingPermissions)
    outState.putString("processSession", processSession)
    outState.putBoolean(
      "receiveOnLaunchPending",
      receiveOnLaunchPending && service?.receiveOnLaunchPending != false,
    )
    super.onSaveInstanceState(outState)
  }

  private fun attempt(action: () -> Unit) {
    try {
      action()
    } catch (error: Exception) {
      report(error.message ?: "Beam operation failed")
    }
  }

  private fun hasLocalNetworkPermission(): Boolean =
    Build.VERSION.SDK_INT < 37 ||
      checkSelfPermission(Manifest.permission.ACCESS_LOCAL_NETWORK) ==
        PackageManager.PERMISSION_GRANTED

  private fun withNetwork(action: () -> Unit) {
    check(!requestingPermissions) { "Respond to the permission request first" }
    val permissions = mutableListOf<String>()
    if (!hasLocalNetworkPermission()) permissions += Manifest.permission.ACCESS_LOCAL_NETWORK
    val preferences = getSharedPreferences("permissions", MODE_PRIVATE)
    if (
      Build.VERSION.SDK_INT >= 33 &&
        !preferences.getBoolean("notificationsRequested", false) &&
        checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) !=
          PackageManager.PERMISSION_GRANTED
    ) {
      permissions += Manifest.permission.POST_NOTIFICATIONS
      preferences.edit { putBoolean("notificationsRequested", true) }
    }
    if (permissions.isNotEmpty()) {
      pendingNetwork = action
      requestingPermissions = true
      requestPermissions(permissions.toTypedArray(), 11)
    } else action()
  }

  private fun receiveOnLaunch() {
    val core = service ?: return
    // A share intent only selects outgoing content. Automatic reception is a
    // consequence of opening Beam, never of input supplied by another app.
    if (
      !resumed ||
        intent.action == Intent.ACTION_SEND ||
        !receiveOnLaunchPending ||
        !core.receiveOnLaunchPending
    )
      return
    if (core.receiving) {
      core.receiveOnLaunchPending = false
      return
    }
    if (pendingReceive || requestingPermissions) return
    if (peerNames.isEmpty()) {
      report("Pair a device in Manage devices to start receiving automatically")
      return
    }
    requestReceiving()
  }

  private fun requestReceiving() {
    pendingReceive = true
    withNetwork { startPendingReceive() }
  }

  private fun startPendingReceive() {
    val core = service ?: return
    if (!pendingReceive || !resumed || requestingPermissions || !hasLocalNetworkPermission()) return
    pendingReceive = false
    receiveOnLaunchPending = false
    core.receiveOnLaunchPending = false
    if (!core.receiving) core.startReceiving()
    update()
  }

  override fun onRequestPermissionsResult(
    requestCode: Int,
    permissions: Array<out String>,
    grantResults: IntArray,
  ) {
    super.onRequestPermissionsResult(requestCode, permissions, grantResults)
    if (requestCode == 11) {
      requestingPermissions = false
      val action = pendingNetwork
      pendingNetwork = null
      if (hasLocalNetworkPermission()) {
        attempt {
          action?.invoke()
          startPendingReceive()
        }
      } else {
        pendingReceive = false
        receiveOnLaunchPending = false
        service?.receiveOnLaunchPending = false
        report(
          "Local network permission is required to reach paired devices. Grant it in app settings, then retry receiving."
        )
      }
    }
  }

  private fun report(message: String) {
    if (service != null) service?.report(message) else status.text = message
  }

  private fun update() {
    service?.let { core ->
      if (!core.receiveOnLaunchPending) receiveOnLaunchPending = false
      status.text =
        core.status
          .takeUnless {
            it == "Choose content and a paired device" ||
              it == core.sendStatus ||
              it == core.receiveStatus ||
              it == core.receivedFileStatus ||
              it.startsWith("Pair a device in Manage devices")
          }
          .orEmpty()
      send.isEnabled = !core.sending && peerNames.isNotEmpty()
      cancelSend.visibility = if (core.sending) View.VISIBLE else View.GONE
      receive.text = if (core.receiving) "Stop receiving" else "Start receiving"
      receive.isEnabled = !pendingReceive && !requestingPermissions
      sendProgress.progress = core.sendPercent
      sendProgress.visibility = if (core.sending) View.VISIBLE else View.GONE
      receiveProgress.progress = core.receivePercent
      receiveProgress.visibility = if (core.receivingContent) View.VISIBLE else View.GONE
      sendProgressLabel.text = core.sendStatus.takeUnless { it == "Send: idle" }.orEmpty()
      receiveProgressLabel.text =
        when {
          core.receivedFileStatus.isNotEmpty() -> core.receivedFileStatus
          core.receivingContent || core.receiveFailed -> core.receiveStatus
          core.hasUnsavedFiles -> "File waiting to save"
          core.receiving -> "Ready to receive"
          peerNames.isEmpty() -> "Pair a device in Manage devices"
          else -> "Paused"
        }
      retrySaving.visibility = if (core.hasUnsavedFiles) View.VISIBLE else View.GONE
      retrySaving.isEnabled = !core.receivingContent
      receivedContent.visibility = if (core.receivedText != null) View.VISIBLE else View.GONE
      core.receivedText?.let { if (receivedText.text.toString() != it) receivedText.setText(it) }
    }
  }

  private fun updateContent() {
    val file = kinds.checkedRadioButtonId == R.id.kind_file
    fileContent.visibility = if (file) View.VISIBLE else View.GONE
    filename.visibility = if (uri == null) View.GONE else View.VISIBLE
    chooseFile.text = if (uri == null) "Choose file" else "Change file"
    review.visibility = if (file) View.GONE else View.VISIBLE
    outgoingHeader.visibility = if (file) View.GONE else View.VISIBLE
  }

  private fun refreshPeers() {
    val previous = peerNames.getOrNull(devices.selectedItemPosition)
    peerNames = checkNotNull(service).call(Command.PEERS).chunked(3).map { it[0] }
    devices.adapter =
      ArrayAdapter(
        this,
        android.R.layout.simple_spinner_dropdown_item,
        if (peerNames.isEmpty()) listOf("No paired devices") else peerNames,
      )
    devices.setSelection(peerNames.indexOf(previous).coerceAtLeast(0))
    update()
    receiveOnLaunch()
  }

  @Suppress("DEPRECATION")
  private fun acceptShare(shared: Intent?) {
    if (shared?.action != Intent.ACTION_SEND) return
    attempt {
      require((shared.clipData?.itemCount ?: 0) <= 1) { "Choose exactly one shared item" }
      val stream = shared.getParcelableExtra<Uri>(Intent.EXTRA_STREAM)
      if (stream != null) selectFile(stream)
      else {
        val text =
          shared.getCharSequenceExtra(Intent.EXTRA_TEXT)?.toString()
            ?: error("No supported shared content")
        review.setText(Selection.text(text))
        kinds.check(R.id.kind_text)
      }
    }
  }

  private fun selectFile(value: Uri) {
    Selection.contentUri(value.scheme, value.authority)
    var name = "shared-file"
    contentResolver.query(value, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
      cursor ->
      if (cursor.moveToFirst()) name = cursor.getString(0) ?: name
    }
    uri = value
    filename.setText(name.take(255))
    updateContent()
    kinds.check(R.id.kind_file)
  }

  @Suppress("DEPRECATION")
  private fun openDocument(request: Int) {
    startActivityForResult(
      Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
        type = "*/*"
        addCategory(Intent.CATEGORY_OPENABLE)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
      },
      request,
    )
  }

  @Suppress("DEPRECATION")
  private fun manage() {
    startActivityForResult(Intent(this, DevicesActivity::class.java), 141)
  }

  private fun settings() {
    val core = checkNotNull(service)
    val form = ui.column().apply { setPadding(ui.dp(24), 0, ui.dp(24), 0) }
    fun field(label: String, value: String) =
      EditText(this).apply {
        hint = label
        contentDescription = label
        setSingleLine()
        setText(value)
        ui.styleInput(this)
        form.addView(ui.label(label, 13f))
        form.addView(this)
      }
    form.addView(ui.label("Files save to Downloads"))
    val port = field("Listen port", core.port.toString())
    val timeout = field("Timeout (seconds)", core.timeoutSeconds.toString())
    val dialog =
      AlertDialog.Builder(this)
        .setTitle("Settings")
        .setView(form)
        .setMessage("Stop transfers before editing network settings.")
        .setNegativeButton("Cancel", null)
        .setPositiveButton("Save", null)
        .create()
    dialog.setOnShowListener {
      dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
        attempt {
          core.saveSettings(
            core.receiveFolder,
            port.text.toString().toInt(),
            timeout.text.toString().toInt(),
          )
          dialog.dismiss()
        }
      }
    }
    dialog.show()
  }

  @Deprecated("Platform activity result callback")
  override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
    super.onActivityResult(requestCode, resultCode, data)
    if (requestCode == 141) {
      attempt {
        refreshPeers()
        startPendingReceive()
      }
      return
    }
    if (requestCode != PICK_FILE || resultCode != RESULT_OK) return
    val value = data?.data ?: return
    attempt {
      contentResolver.takePersistableUriPermission(value, Intent.FLAG_GRANT_READ_URI_PERMISSION)
      selectFile(value)
    }
  }

  companion object {
    private val processSession = UUID.randomUUID().toString()
    private const val PICK_FILE = 1
  }
}
