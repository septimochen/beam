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
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.provider.OpenableColumns
import android.text.InputFilter
import android.view.Gravity
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TextView
import androidx.core.content.edit
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.UUID
import java.util.concurrent.Executors

@SuppressLint("SetTextI18n") // Status strings include native errors and byte counts.
class MainActivity : Activity() {
  private var service: TransferService? = null
  private var bound = false
  private lateinit var devices: Spinner
  private lateinit var review: EditText
  private lateinit var filename: EditText
  private lateinit var selected: TextView
  private lateinit var status: TextView
  private lateinit var send: Button
  private lateinit var receive: Button
  private lateinit var kinds: RadioGroup
  private lateinit var sendProgress: ProgressBar
  private lateinit var receiveProgress: ProgressBar
  private lateinit var sendProgressLabel: TextView
  private lateinit var receiveProgressLabel: TextView
  private lateinit var receivedFileStatus: TextView
  private lateinit var connectivity: TextView
  private lateinit var checkConnectivity: Button
  private lateinit var unpair: Button
  private lateinit var receivedText: EditText
  private lateinit var outgoingHeader: LinearLayout
  private var uri: Uri? = null
  private var peerNames = emptyList<String>()
  private var export: ByteArray? = null
  private var exportFile: File? = null
  private val io = Executors.newSingleThreadExecutor()
  private val kindIds = listOf(R.id.kind_file, R.id.kind_text)
  private var pendingNetwork: (() -> Unit)? = null
  private var pendingReceive = false
  private var requestingPermissions = false
  private var resumed = false
  private var receiveOnLaunchPending = true
  private var resumeAfterPairing = false

  private val connection =
    object : ServiceConnection {
      override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
        service = (binder as TransferService.LocalBinder).service
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
    super.onCreate(savedInstanceState)
    val scroll = ScrollView(this)
    val column =
      LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        val padding = (20 * resources.displayMetrics.density).toInt()
        setPadding(padding, padding, padding, padding)
      }
    // Target 36 draws edge-to-edge. Keep controls outside system bars/IME.
    scroll.setOnApplyWindowInsetsListener { view, insets ->
      if (Build.VERSION.SDK_INT >= 30) {
        val bars =
          insets.getInsets(
            android.view.WindowInsets.Type.systemBars() or android.view.WindowInsets.Type.ime()
          )
        view.setPadding(bars.left, bars.top, bars.right, bars.bottom)
      } else {
        @Suppress("DEPRECATION")
        view.setPadding(
          insets.systemWindowInsetLeft,
          insets.systemWindowInsetTop,
          insets.systemWindowInsetRight,
          insets.systemWindowInsetBottom,
        )
      }
      insets
    }
    scroll.addView(column)
    setContentView(scroll)
    column.addView(
      Button(this).apply {
        text = "Add device"
        setOnClickListener {
          attempt {
            withNetwork {
              val core = checkNotNull(service) { "Beam is starting" }
              check(!core.sending && !core.checking) {
                "Wait for the active transfer before pairing"
              }
              resumeAfterPairing = core.receiving || core.receiveOnLaunchPending
              core.stopReceiving()
              startActivityForResult(Intent(this@MainActivity, PairingActivity::class.java), 140)
            }
          }
        }
      }
    )
    if (Build.VERSION.SDK_INT >= 30) {
      val appearance =
        android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or
          android.view.WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS
      window.insetsController?.setSystemBarsAppearance(appearance, appearance)
    } else {
      @Suppress("DEPRECATION")
      window.decorView.systemUiVisibility =
        View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR or View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR
    }
    fun label(text: String, size: Float = 16f): TextView =
      TextView(this).apply {
        this.text = text
        textSize = size
        setPadding(0, 16, 0, 8)
        column.addView(this)
      }
    fun button(text: String, action: () -> Unit): Button =
      Button(this).apply {
        this.text = text
        column.addView(this)
        setOnClickListener { attempt(action) }
      }
    label("Beam", 32f)
    label("Share directly with your paired devices. Review your selection, then Send.")
    label("Receive")
    label("Files from paired devices save to Downloads automatically.")
    receiveProgress =
      ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).also {
        it.max = 100
        it.contentDescription = "Incoming transfer progress"
        column.addView(it)
      }
    receiveProgressLabel = label("Receive: idle")
    receivedFileStatus = label("Received files save to Downloads")
    receivedFileStatus.accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
    button("Show Downloads") {
      startActivity(Intent(android.app.DownloadManager.ACTION_VIEW_DOWNLOADS))
    }
    receive =
      button("Start receiving") {
        val core = checkNotNull(service) { "Beam is starting" }
        if (core.receiving) {
          receiveOnLaunchPending = false
          core.stopReceiving()
        } else requestReceiving()
        update()
      }
    label("Paired device")
    devices = Spinner(this).also { column.addView(it) }
    unpair =
      button("Unpair device") {
        confirmUnpair(
          peerNames.getOrNull(devices.selectedItemPosition) ?: error("Choose a paired device")
        )
      }
    button("Manage devices and identity") { manage() }
    button("Settings") { settings() }
    checkConnectivity =
      button("Check connectivity") {
        val peer =
          peerNames.getOrNull(devices.selectedItemPosition) ?: error("Choose a paired device")
        withNetwork { checkNotNull(service).checkConnectivity(peer) }
      }
    connectivity = label("")
    label("Content")
    kinds =
      RadioGroup(this).apply {
        orientation = RadioGroup.HORIZONTAL
        listOf("File", "Text").forEachIndexed { index, title ->
          addView(
            RadioButton(context).apply {
              id = kindIds[index]
              text = title
            }
          )
        }
        check(R.id.kind_file)
        column.addView(this)
      }
    button("Choose file") { openDocument(PICK_FILE) }
    selected = label("No file selected")
    filename =
      EditText(this).apply {
        hint = "Filename"
        contentDescription = "Outgoing filename"
        setSingleLine()
        filters = arrayOf(InputFilter.LengthFilter(255))
        column.addView(this)
      }
    fun copyRow(title: String, content: () -> String): LinearLayout {
      val row = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL }
      row.addView(TextView(this).apply { text = title }, LinearLayout.LayoutParams(0, -2, 1f))
      row.addView(
        Button(this).apply {
          text = "Copy to clipboard"
          setOnClickListener {
            getSystemService(ClipboardManager::class.java)
              .setPrimaryClip(ClipData.newPlainText("Beam text", content()))
          }
        }
      )
      column.addView(row)
      return row
    }
    outgoingHeader = copyRow("Text to send") { review.text.toString() }
    review =
      EditText(this).apply {
        hint = "Text to send (including URLs)"
        contentDescription = "Review outgoing text"
        minLines = 3
        maxLines = 8
        inputType =
          android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE
        filters = arrayOf(InputFilter.LengthFilter(Selection.MAX_TEXT))
        column.addView(this)
      }
    kinds.setOnCheckedChangeListener { _, _ -> updateContent() }
    send =
      button("Send") {
        val peer =
          peerNames.getOrNull(devices.selectedItemPosition)
            ?: error("Pair and choose a device first")
        val kind = listOf("file", "text")[kindIds.indexOf(kinds.checkedRadioButtonId)]
        val core = checkNotNull(service) { "Beam is starting" }
        val selectedUri = uri
        val selectedName = filename.text.toString()
        val selectedText = review.text.toString()
        withNetwork {
          checkNotNull(service) { "Beam is starting" }
            .send(peer, kind, selectedUri, selectedName, selectedText)
        }
        update()
      }
    button("Cancel send") { service?.cancelSend() }
    sendProgress =
      ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).also {
        it.max = 100
        column.addView(it)
      }
    sendProgressLabel = label("Send: idle")
    copyRow("Received text") { receivedText.text.toString() }
    receivedText =
      EditText(this).apply {
        contentDescription = "Received text"
        minLines = 3
        maxLines = 8
        keyListener = null
        setTextIsSelectable(true)
        column.addView(this)
      }
    button("Recover an unsaved file…") { receivedFiles() }
    status = label("Starting Beam…")
    status.accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
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
      export = savedInstanceState.getByteArray("export")
      exportFile =
        savedInstanceState.getString("exportName")?.let { name ->
          File(
            File(
              filesDir,
              getSharedPreferences("settings", MODE_PRIVATE).getString("folder", "received")
                ?: "received",
            ),
            Selection.filename(name),
          )
        }
      selected.text = if (uri == null) "No file selected" else "Selected file: ${filename.text}"
    } else acceptShare(intent)
    updateContent()
  }

  override fun onResume() {
    super.onResume()
    resumed = true
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

  override fun onDestroy() {
    io.shutdownNow()
    super.onDestroy()
  }

  override fun onSaveInstanceState(outState: Bundle) {
    outState.putString("uri", uri?.toString())
    outState.putString("filename", filename.text.toString())
    outState.putString("text", review.text.toString())
    outState.putInt("kind", kinds.checkedRadioButtonId)
    outState.putByteArray("export", export)
    outState.putString("exportName", exportFile?.name)
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
      report("Pair a device in Manage devices and identity to start receiving automatically")
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
      status.text = core.status
      send.isEnabled = !core.sending && peerNames.isNotEmpty()
      receive.text = if (core.receiving) "Stop receiving" else "Start receiving"
      receive.isEnabled = !pendingReceive && !requestingPermissions
      checkConnectivity.isEnabled =
        peerNames.isNotEmpty() && !core.checking && !requestingPermissions
      unpair.isEnabled = peerNames.isNotEmpty()
      connectivity.text = core.connectivityStatus
      sendProgress.progress = core.sendPercent
      receiveProgress.progress = core.receivePercent
      sendProgressLabel.text = core.sendStatus
      receiveProgressLabel.text = core.receiveStatus
      if (receivedFileStatus.text.toString() != core.receivedFileStatus)
        receivedFileStatus.text = core.receivedFileStatus
      core.receivedText?.let { if (receivedText.text.toString() != it) receivedText.setText(it) }
    }
  }

  private fun updateContent() {
    val file = kinds.checkedRadioButtonId == R.id.kind_file
    filename.visibility = if (file) View.VISIBLE else View.GONE
    selected.visibility = if (file) View.VISIBLE else View.GONE
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
      report("Review the selected content and choose a paired device, then Send")
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
    selected.text = "Selected file: $name"
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

  private fun manage() {
    AlertDialog.Builder(this)
      .setTitle("Devices and identity")
      .setItems(
        arrayOf(
          "Show / export my public certificate",
          "Pair a device",
          "Update device address",
          "Unpair device",
        )
      ) { _, which ->
        attempt {
          when (which) {
            0 -> identity()
            1 -> pairDialog()
            2 -> choosePeer(false)
            3 -> choosePeer(true)
          }
        }
      }
      .show()
  }

  private fun settings() {
    val core = checkNotNull(service)
    val form =
      LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(32, 0, 32, 0)
      }
    fun field(label: String, value: String) =
      EditText(this).apply {
        hint = label
        contentDescription = label
        setSingleLine()
        setText(value)
        form.addView(TextView(context).apply { text = label })
        form.addView(this)
      }
    form.addView(TextView(this).apply { text = "Received files: Downloads" })
    val port = field("Listen port", core.port.toString())
    val timeout = field("Timeout (seconds)", core.timeoutSeconds.toString())
    val dialog =
      AlertDialog.Builder(this)
        .setTitle("Settings")
        .setView(form)
        .setMessage(
          "Stop reception and active transfers before changing network settings. Received files save to Downloads automatically."
        )
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

  private fun identity() {
    val identity = checkNotNull(service).call(Command.IDENTITY)
    val text =
      TextView(this).apply {
        setPadding(32, 24, 32, 24)
        setTextIsSelectable(true)
        this.text =
          "My SHA-256 fingerprint\n${identity[0]}\n\nVerify full fingerprints independently and pair on both devices. Export shares only the public certificate."
      }
    AlertDialog.Builder(this)
      .setTitle("Beam identity")
      .setView(text)
      .setNegativeButton("Close", null)
      .setPositiveButton("Export public certificate") { _, _ ->
        export = identity[2].toByteArray(Charsets.UTF_8)
        createDocument("beam-android.pem", "application/x-pem-file", EXPORT_CERT)
      }
      .show()
  }

  private fun pairDialog(certificate: String = "") {
    val form =
      LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(32, 0, 32, 0)
      }
    fun field(hint: String, limit: Int, multiline: Boolean = false) =
      EditText(this).apply {
        this.hint = hint
        contentDescription = hint
        setSingleLine(!multiline)
        filters = arrayOf(InputFilter.LengthFilter(limit))
        form.addView(this)
      }
    val alias = field("Local alias (e.g. laptop)", 63)
    val address = field("Numeric IP:4269 (optional)", 256)
    val fingerprint = field("Independently verified full SHA-256", 64)
    val pem =
      field("Peer public certificate PEM", Selection.MAX_CERTIFICATE, true).apply {
        setText(certificate)
        maxLines = 5
      }
    val dialog =
      AlertDialog.Builder(this)
        .setTitle("Pair a device")
        .setView(form)
        .setMessage(
          "Compare all 64 fingerprint characters on the other device through an independent channel. Then pair Android on that device too."
        )
        .setNegativeButton("Cancel", null)
        .setNeutralButton("Import certificate") { _, _ -> openDocument(IMPORT_CERT) }
        .setPositiveButton("Pair", null)
        .create()
    dialog.setOnShowListener {
      dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
        attempt {
          val file = File.createTempFile("peer-", ".pem", cacheDir)
          try {
            require(
              pem.text.toString().toByteArray(Charsets.UTF_8).size <= Selection.MAX_CERTIFICATE
            ) {
              "Certificate exceeds 16 KiB"
            }
            file.writeText(pem.text.toString())
            checkNotNull(service)
              .call(
                Command.PAIR,
                alias.text.toString(),
                file.canonicalPath,
                fingerprint.text.toString(),
                address.text.toString(),
              )
            refreshPeers()
            report("Paired ${alias.text}; approve Android on that device too")
            dialog.dismiss()
          } finally {
            file.delete()
          }
        }
      }
    }
    dialog.show()
  }

  private fun choosePeer(remove: Boolean) {
    require(peerNames.isNotEmpty()) { "No paired devices" }
    AlertDialog.Builder(this)
      .setTitle(if (remove) "Unpair device" else "Update address")
      .setItems(peerNames.toTypedArray()) { _, index ->
        val peer = peerNames[index]
        if (remove) confirmUnpair(peer)
        else {
          val field =
            EditText(this).apply {
              hint = "192.168.1.20:4269 or [IPv6]:4269"
              setSingleLine()
            }
          AlertDialog.Builder(this)
            .setTitle("Address for $peer")
            .setView(field)
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Save") { _, _ ->
              attempt {
                checkNotNull(service).call(Command.ENDPOINT, peer, field.text.toString())
                refreshPeers()
              }
            }
            .show()
        }
      }
      .show()
  }

  private fun confirmUnpair(peer: String) {
    AlertDialog.Builder(this)
      .setTitle("Unpair $peer?")
      .setMessage(
        "Active transfers and connectivity checks will stop. This removes trust on this device only. Pair again to reconnect."
      )
      .setNegativeButton("Cancel", null)
      .setPositiveButton("Unpair") { _, _ ->
        attempt {
          val core = checkNotNull(service)
          pendingReceive = false
          core.unpair(peer)
          receiveOnLaunchPending = core.receiveOnLaunchPending
          refreshPeers()
        }
      }
      .show()
  }

  private fun receivedFiles() {
    val root = checkNotNull(service).inbox
    val files =
      root.listFiles()?.filter { it.isFile && !it.name.startsWith('.') }?.sortedBy { it.name }
        ?: emptyList()
    require(files.isNotEmpty()) { "No received files yet" }
    AlertDialog.Builder(this)
      .setTitle("Recover a received file")
      .setItems(files.map { "${it.name} (${it.length()} bytes)" }.toTypedArray()) { _, index ->
        exportFile = files[index]
        createDocument(files[index].name, "application/octet-stream", EXPORT_FILE)
      }
      .show()
  }

  @Suppress("DEPRECATION")
  private fun createDocument(name: String, mime: String, request: Int) {
    startActivityForResult(
      Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
        type = mime
        addCategory(Intent.CATEGORY_OPENABLE)
        putExtra(Intent.EXTRA_TITLE, name)
      },
      request,
    )
  }

  @Deprecated("Platform activity result callback")
  override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
    super.onActivityResult(requestCode, resultCode, data)
    if (requestCode == 140) {
      if (resumeAfterPairing) {
        receiveOnLaunchPending = true
        pendingReceive = true
        service?.receiveOnLaunchPending = true
      }
      attempt {
        refreshPeers()
        startPendingReceive()
      }
      resumeAfterPairing = false
      return
    }
    if (resultCode != RESULT_OK) return
    val value = data?.data ?: return
    attempt {
      if (requestCode == PICK_FILE) {
        contentResolver.takePersistableUriPermission(value, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        selectFile(value)
        return@attempt
      }
      val publicCertificate = export
      val received = exportFile
      export = null
      exportFile = null
      io.submit {
        try {
          if (requestCode == IMPORT_CERT) {
            val buffer = ByteArrayOutputStream()
            contentResolver.openInputStream(value).use { source ->
              Selection.copy(checkNotNull(source), buffer, Selection.MAX_CERTIFICATE.toLong()) {
                Thread.currentThread().isInterrupted
              }
            }
            runOnUiThread { if (!isDestroyed) pairDialog(buffer.toString(Charsets.UTF_8.name())) }
          } else {
            contentResolver.openOutputStream(value, "w").use { output ->
              checkNotNull(output) { "Cannot open destination document" }
              if (requestCode == EXPORT_CERT) output.write(checkNotNull(publicCertificate))
              else
                checkNotNull(received).inputStream().use { source ->
                  Selection.copy(source, output, Selection.MAX_FILE) {
                    Thread.currentThread().isInterrupted
                  }
                }
            }
            runOnUiThread { if (!isDestroyed) report("Saved selected document") }
          }
        } catch (error: Exception) {
          runOnUiThread { if (!isDestroyed) report(error.message ?: "Document operation failed") }
        }
      }
    }
  }

  companion object {
    private val processSession = UUID.randomUUID().toString()
    private const val PICK_FILE = 1
    private const val IMPORT_CERT = 2
    private const val EXPORT_CERT = 3
    private const val EXPORT_FILE = 4
  }
}
