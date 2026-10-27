package org.beam.android

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.text.Editable
import android.text.InputFilter
import android.text.TextWatcher
import android.view.View
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.core.graphics.toColorInt
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.concurrent.Executors

/** Native device management; all trust and backup validation stays in the shared core. */
@SuppressLint("SetTextI18n")
class DevicesActivity : Activity() {
  private var service: TransferService? = null
  private var bound = false
  private lateinit var list: LinearLayout
  private lateinit var summary: TextView
  private lateinit var status: TextView
  private lateinit var search: EditText
  private val io = Executors.newSingleThreadExecutor()
  private val handler = Handler(Looper.getMainLooper())
  private var rows = emptyList<List<String>>()
  private var replacementTarget: List<String>? = null
  private val checkLabels = mutableMapOf<String, TextView>()
  private var checkingDevice = ""
  private var resumeAfterPairing = false
  private var pendingAction: (() -> Unit)? = null
  private var replacementReceiver: ((String) -> Unit)? = null
  private val ui by lazy { BeamUi(this) }
  private val ink
    get() = ui.ink

  private val muted
    get() = ui.muted

  private val surface
    get() = ui.surface

  private fun dp(value: Int) = ui.dp(value)

  private val connection =
    object : ServiceConnection {
      override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
        service = (binder as TransferService.LocalBinder).service
        attempt {
          refresh()
          pendingAction?.also { pendingAction = null }?.invoke()
        }
        handler.post(poll)
      }

      override fun onServiceDisconnected(name: ComponentName?) {
        service = null
      }
    }
  private val poll =
    object : Runnable {
      override fun run() {
        val core = service
        if (core != null && checkingDevice.isNotEmpty()) {
          checkLabels[checkingDevice]?.text = core.connectivityStatus
        }
        handler.postDelayed(this, 300)
      }
    }

  override fun onCreate(savedInstanceState: Bundle?) {
    ui.applyTheme()
    super.onCreate(savedInstanceState)
    resumeAfterPairing = savedInstanceState?.getBoolean("resumePairing") ?: false
    replacementTarget = savedInstanceState?.getStringArrayList("replacementTarget")
    val column = ui.screen()
    val navigation = row()
    navigation.addView(button("‹ Back") { finish() }, weight())
    navigation.addView(button("My identity") { identity() }, weight())
    column.addView(navigation)
    column.addView(label("Your devices", 30f, true))
    summary = label("Loading paired devices…", 13f)
    column.addView(summary)
    val actions = row()
    actions.addView(button("+ Add device", true) { addDevice() }, weight())
    actions.addView(
      button("Refresh") {
        refresh()
        status.text = "Device list refreshed"
      },
      weight(),
    )
    column.addView(actions)
    search =
      EditText(this).apply {
        hint = "Search devices or addresses"
        contentDescription = hint
        setSingleLine()
        setTextColor(ink)
        setHintTextColor(muted)
        setPadding(dp(16), dp(12), dp(16), dp(12))
        background = rounded(surface)
        addTextChangedListener(
          object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}

            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
              renderDevices()
            }

            override fun afterTextChanged(s: Editable?) {}
          }
        )
      }
    column.addView(
      search,
      LinearLayout.LayoutParams(-1, -2).apply {
        topMargin = dp(16)
        bottomMargin = dp(8)
      },
    )
    status = ui.message()
    column.addView(status)
    list = column()
    column.addView(list)
    val backup = card()
    backup.addView(label("Backup & restore", 18f, true))
    backup.addView(
      label(
        "Your private key stays on this device.",
        14f,
      )
    )
    val backups = row()
    backups.addView(
      button("Export backup") {
        val contents = core().call(Command.EXPORT_DEVICES)[0]
        File(cacheDir, "devices-backup-export").writeText(contents)
        createDocument("beam-devices.beam-backup", "application/octet-stream", EXPORT_BACKUP)
      },
      weight(),
    )
    backups.addView(button("Import backup") { openDocument(IMPORT_BACKUP) }, weight())
    backup.addView(backups)
    column.addView(backup)
    column.addView(button("Pair with a public certificate…") { manualPair() })
  }

  override fun onSaveInstanceState(outState: Bundle) {
    outState.putBoolean("resumePairing", resumeAfterPairing)
    replacementTarget?.let { outState.putStringArrayList("replacementTarget", ArrayList(it)) }
    super.onSaveInstanceState(outState)
  }

  private fun whenConnected(action: () -> Unit) {
    if (service == null) pendingAction = action else attempt(action)
  }

  private fun mutate(command: Int, vararg arguments: String): List<String> {
    try {
      return core().mutateDevices(command, *arguments)
    } finally {
      afterMutation()
    }
  }

  override fun onStart() {
    super.onStart()
    bound =
      bindService(Intent(this, TransferService::class.java), connection, Context.BIND_AUTO_CREATE)
  }

  override fun onStop() {
    handler.removeCallbacks(poll)
    service = null
    if (bound) unbindService(connection)
    bound = false
    super.onStop()
  }

  override fun onDestroy() {
    io.shutdownNow()
    super.onDestroy()
  }

  private fun core() = checkNotNull(service) { "Beam is starting. Try again in a moment." }

  private fun column() = ui.column()

  private fun row() = ui.actionRow()

  private fun weight() = ui.weight()

  private fun rounded(color: Int) = ui.rounded(color)

  private fun card() = ui.card()

  private fun label(value: String, size: Float = 14f, bold: Boolean = false) =
    ui.label(value, size, bold)

  private fun button(title: String, primary: Boolean = false, action: () -> Unit) =
    ui.button(title, primary) { attempt(action) }

  private fun attempt(action: () -> Unit) {
    try {
      action()
    } catch (error: Exception) {
      status.text = error.message ?: "Device operation failed"
      AlertDialog.Builder(this)
        .setTitle("Couldn’t complete that")
        .setMessage(error.message)
        .setPositiveButton("OK", null)
        .show()
    }
  }

  private fun refresh() {
    rows = core().call(Command.DEVICE_DETAILS).chunked(5)
    summary.text = "${rows.size} paired"
    renderDevices()
  }

  private fun renderDevices() {
    if (!::list.isInitialized) return
    list.removeAllViews()
    checkLabels.clear()
    val query = search.text.toString()
    val visible = rows.filter { device ->
      query.isBlank() || device.take(3).any { it.contains(query, true) }
    }
    if (visible.isEmpty()) {
      val empty = card()
      empty.addView(
        label(
          if (rows.isEmpty()) "Make your first connection" else "No matching devices",
          20f,
          true,
        )
      )
      empty.addView(
        label(
          if (rows.isEmpty()) "Tap Add device to connect." else "Try another name or IP address."
        )
      )
      list.addView(empty)
    }
    for (device in visible) {
      val card = card()
      card.addView(label(device[0], 21f, true))
      card.addView(
        label(device[1].ifEmpty { "No direct address saved" }).apply { setTextIsSelectable(true) }
      )
      if (device[2].isNotEmpty())
        card.addView(label("Fallback · ${device[2]}").apply { setTextIsSelectable(true) })
      val connection = label("Connection not checked", 13f)
      connection.accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
      checkLabels[device[0]] = connection
      card.addView(connection)
      val actions = row()
      actions.addView(button("Edit") { editDevice(device) }, weight())
      actions.addView(
        button("Check") {
          require(
            Build.VERSION.SDK_INT < 37 ||
              checkSelfPermission(Manifest.permission.ACCESS_LOCAL_NETWORK) ==
                PackageManager.PERMISSION_GRANTED
          ) {
            "Grant local network permission in Beam’s app settings, then check again."
          }
          core().checkConnectivity(device[0])
          checkingDevice = device[0]
          connection.text = "Checking authenticated connection…"
        },
        weight(),
      )
      actions.addView(
        button("More") {
          AlertDialog.Builder(this)
            .setTitle(device[0])
            .setItems(arrayOf("View certificate", "Unpair device…")) { _, which ->
              if (which == 0) certificate(device) else confirmUnpair(device[0])
            }
            .show()
        },
        weight(),
      )
      card.addView(actions)
      list.addView(card)
    }
  }

  private fun afterMutation() {
    checkingDevice = ""
    refresh()
    val core = core()
    if (
      core.receiveOnLaunchPending &&
        rows.isNotEmpty() &&
        !core.receiving &&
        (Build.VERSION.SDK_INT < 37 ||
          checkSelfPermission(Manifest.permission.ACCESS_LOCAL_NETWORK) ==
            PackageManager.PERMISSION_GRANTED)
    ) {
      core.startReceiving()
    }
  }

  @Suppress("DEPRECATION")
  private fun addDevice() {
    if (
      Build.VERSION.SDK_INT >= 37 &&
        checkSelfPermission(Manifest.permission.ACCESS_LOCAL_NETWORK) !=
          PackageManager.PERMISSION_GRANTED
    ) {
      requestPermissions(arrayOf(Manifest.permission.ACCESS_LOCAL_NETWORK), 30)
      return
    }
    check(!core().sending && !core().checking) { "Wait for the active transfer before pairing" }
    resumeAfterPairing = core().receiving || core().receiveOnLaunchPending
    core().stopReceiving()
    startActivityForResult(Intent(this, PairingActivity::class.java), PAIR_DEVICE)
  }

  override fun onRequestPermissionsResult(
    requestCode: Int,
    permissions: Array<out String>,
    grantResults: IntArray,
  ) {
    super.onRequestPermissionsResult(requestCode, permissions, grantResults)
    if (requestCode != 30) return
    if (grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED)
      whenConnected { addDevice() }
    else status.text = "Local network access is required to pair devices."
  }

  private fun identity() {
    val identity = core().call(Command.IDENTITY)
    val view =
      label(
          "SHA-256 fingerprint\n\n${identity[0]}\n\nCompare all 64 characters when manually pairing. Export shares only your public certificate."
        )
        .apply {
          setPadding(dp(24), dp(16), dp(24), dp(8))
          setTextIsSelectable(true)
        }
    AlertDialog.Builder(this)
      .setTitle("This device’s identity")
      .setView(view)
      .setNegativeButton("Done", null)
      .setPositiveButton("Export certificate") { _, _ ->
        attempt {
          File(cacheDir, "device-certificate-export").writeText(identity[2])
          createDocument("beam-android.pem", "application/x-pem-file", EXPORT_CERTIFICATE)
        }
      }
      .show()
  }

  private fun certificate(device: List<String>) {
    val view =
      label("SHA-256\n${device[3]}\n\n${device[4]}").apply {
        typeface = Typeface.MONOSPACE
        setTextIsSelectable(true)
        setPadding(dp(24), dp(12), dp(24), dp(12))
      }
    AlertDialog.Builder(this)
      .setTitle("${device[0]} · Certificate")
      .setView(ScrollView(this).apply { addView(view) })
      .setPositiveButton("Done", null)
      .show()
  }

  private fun field(
    form: LinearLayout,
    title: String,
    value: String = "",
    limit: Int = 256,
    multiline: Boolean = false,
  ): EditText {
    form.addView(label(title, 13f, true))
    return EditText(this).apply {
      contentDescription = title
      setText(value)
      setSingleLine(!multiline)
      setTextColor(ink)
      filters = arrayOf(InputFilter.LengthFilter(limit))
      if (multiline) {
        maxLines = 5
        minLines = 3
        textSize = 12f
      }
      form.addView(this)
    }
  }

  private fun dialogForm() = column().apply { setPadding(dp(24), dp(4), dp(24), dp(12)) }

  private fun editDevice(device: List<String>, replacement: String = "") {
    val form = dialogForm()
    val name = field(form, "Device name", device[0], 63)
    form.addView(label("1–63 lowercase letters, numbers, - or _; start with a letter.", 12f))
    val endpoint = field(form, "IP address & port", device[1])
    form.addView(
      label(
        "Use IP:port or [IPv6]:port. Leave empty to remove. Saving stops active transfers and checks.",
        12f,
      )
    )
    val fallback = field(form, "Fallback address (optional)", device[2])
    form.addView(label("Current SHA-256\n${device[3]}", 12f).apply { setTextIsSelectable(true) })
    val replace =
      CheckBox(this).apply {
        text = "Replace certificate"
        isChecked = replacement.isNotEmpty()
        setTextColor(ink)
      }
    form.addView(replace)
    val certForm = column()
    certForm.addView(
      label(
        "This changes which identity you trust. Verify the full new fingerprint on the other device.",
        13f,
      )
    )
    certForm.addView(
      button("Choose public certificate…") {
        replacementTarget = device
        openDocument(REPLACE_CERTIFICATE)
      }
    )
    val pem =
      field(
        certForm,
        "Replacement public certificate PEM",
        replacement,
        Selection.MAX_CERTIFICATE,
        true,
      )
    val fingerprint = field(certForm, "Verified SHA-256 (all 64 characters)", "", 64)
    val chooseCertificate = certForm.getChildAt(1) as Button
    chooseCertificate.setOnClickListener {
      replacementTarget = device
      replacementReceiver = { value ->
        pem.setText(value)
        replace.isChecked = true
      }
      openDocument(REPLACE_CERTIFICATE)
    }
    certForm.visibility = if (replace.isChecked) View.VISIBLE else View.GONE
    replace.setOnCheckedChangeListener { _, checked ->
      certForm.visibility = if (checked) View.VISIBLE else View.GONE
    }
    form.addView(certForm)
    val error = label("").apply { setTextColor("#BD3645".toColorInt()) }
    form.addView(error)
    val dialog =
      AlertDialog.Builder(this)
        .setTitle("Edit ${device[0]}")
        .setView(ScrollView(this).apply { addView(form) })
        .setNegativeButton("Cancel", null)
        .setPositiveButton("Save changes", null)
        .create()
    dialog.setOnShowListener {
      dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
        try {
          if (replace.isChecked)
            require(pem.text.isNotBlank() && fingerprint.text.length == 64) {
              "Choose a certificate and verify all 64 fingerprint characters."
            }
          core()
            .mutateDevices(
              Command.EDIT_DEVICE,
              device[0],
              name.text.toString(),
              endpoint.text.toString(),
              fallback.text.toString(),
              if (replace.isChecked) pem.text.toString() else "",
              if (replace.isChecked) fingerprint.text.toString() else "",
              device[3],
            )
          status.text = "Saved ${name.text}"
          dialog.dismiss()
        } catch (failure: Exception) {
          error.text = failure.message
        }
      }
    }
    dialog.show()
  }

  private fun confirmUnpair(name: String) {
    AlertDialog.Builder(this)
      .setTitle("Unpair $name?")
      .setMessage(
        "Active transfers and connectivity checks will stop. This removes trust on this device only. Pair again to reconnect."
      )
      .setNegativeButton("Cancel", null)
      .setPositiveButton("Unpair") { _, _ ->
        attempt {
          mutate(Command.UNPAIR, name)
          status.text = "Unpaired $name on this device."
        }
      }
      .show()
  }

  private fun manualPair(pemValue: String = "") {
    val form = dialogForm()
    form.addView(
      label(
        "Exchange public certificates and compare the full fingerprint independently. Pair on both devices.",
        14f,
      )
    )
    val name = field(form, "Device name", "", 63)
    val endpoint = field(form, "IP address & port (optional)")
    val fingerprint = field(form, "Verified SHA-256", "", 64)
    val pem = field(form, "Public certificate PEM", pemValue, Selection.MAX_CERTIFICATE, true)
    val error = label("")
    form.addView(error)
    val dialog =
      AlertDialog.Builder(this)
        .setTitle("Pair with certificate")
        .setView(ScrollView(this).apply { addView(form) })
        .setNegativeButton("Cancel", null)
        .setNeutralButton("Import certificate") { _, _ -> openDocument(PAIR_CERTIFICATE) }
        .setPositiveButton("Pair device", null)
        .create()
    dialog.setOnShowListener {
      dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
        try {
          require(
            pem.text.toString().toByteArray(Charsets.UTF_8).size <= Selection.MAX_CERTIFICATE
          ) {
            "Certificate exceeds 16 KiB"
          }
          val file = File.createTempFile("peer-", ".pem", cacheDir)
          try {
            file.writeText(pem.text.toString())
            core()
              .mutateDevices(
                Command.PAIR,
                name.text.toString(),
                file.canonicalPath,
                fingerprint.text.toString(),
                endpoint.text.toString(),
              )
          } finally {
            file.delete()
          }
          status.text = "Paired ${name.text}. Approve this Android device on the other device too."
          dialog.dismiss()
        } catch (failure: Exception) {
          error.text = failure.message
        }
      }
    }
    dialog.show()
  }

  private fun previewBackup(contents: String) {
    val preview = core().call(Command.PREVIEW_DEVICES, contents)
    val local = core().call(Command.IDENTITY)[0]
    val form = dialogForm()
    form.addView(label("${preview[1]} to add · ${preview[2]} already paired", 18f, true))
    form.addView(
      label("Only restore a backup you trust. Existing devices keep their current settings.")
    )
    if (preview[0] != local)
      form.addView(
        label(
          "This backup came from another identity. Pair this device on the other devices before transferring."
        )
      )
    for (device in preview.drop(3).chunked(4)) {
      form.addView(label(device[0], 16f, true))
      form.addView(label(device[1].ifEmpty { "No direct address" }))
      if (device[2].isNotEmpty()) form.addView(label("Fallback: ${device[2]}"))
      form.addView(
        label(device[3], 12f).apply {
          typeface = Typeface.MONOSPACE
          setTextIsSelectable(true)
        }
      )
    }
    val error = label("")
    form.addView(error)
    val dialog =
      AlertDialog.Builder(this)
        .setTitle("Restore your devices")
        .setView(ScrollView(this).apply { addView(form) })
        .setNegativeButton("Cancel", null)
        .setPositiveButton("Restore devices", null)
        .create()
    dialog.setOnShowListener {
      dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
        try {
          val result = mutate(Command.IMPORT_DEVICES, contents)
          status.text = "Restored ${result[1]} devices. Kept ${result[2]} existing devices."
          dialog.dismiss()
        } catch (failure: Exception) {
          error.text = failure.message
        }
      }
    }
    dialog.show()
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
    if (requestCode == PAIR_DEVICE) {
      whenConnected {
        if (resumeAfterPairing) core().receiveOnLaunchPending = true
        afterMutation()
      }
      return
    }
    if (resultCode != RESULT_OK) return
    val uri = data?.data ?: return
    val target = replacementTarget
    io.submit {
      try {
        if (requestCode == EXPORT_BACKUP || requestCode == EXPORT_CERTIFICATE) {
          val file =
            File(
              cacheDir,
              if (requestCode == EXPORT_BACKUP) "devices-backup-export"
              else "device-certificate-export",
            )
          try {
            contentResolver.openOutputStream(uri, "w").use { output ->
              file.inputStream().use { source ->
                Selection.copy(source, checkNotNull(output), MAX_BACKUP.toLong()) {
                  Thread.currentThread().isInterrupted
                }
              }
            }
          } finally {
            file.delete()
          }
          runOnUiThread { if (!isDestroyed) status.text = "Saved selected document" }
        } else {
          val bytes = ByteArrayOutputStream()
          val limit = if (requestCode == IMPORT_BACKUP) MAX_BACKUP else Selection.MAX_CERTIFICATE
          contentResolver.openInputStream(uri).use { source ->
            Selection.copy(checkNotNull(source), bytes, limit.toLong()) {
              Thread.currentThread().isInterrupted
            }
          }
          val contents = bytes.toString(Charsets.UTF_8.name())
          runOnUiThread {
            if (!isDestroyed)
              whenConnected {
                when (requestCode) {
                  IMPORT_BACKUP -> previewBackup(contents)
                  REPLACE_CERTIFICATE -> {
                    checkNotNull(target)
                    val receiver = replacementReceiver
                    replacementReceiver = null
                    if (receiver != null) receiver(contents) else editDevice(target, contents)
                  }
                  PAIR_CERTIFICATE -> manualPair(contents)
                }
              }
          }
        }
      } catch (error: Exception) {
        runOnUiThread {
          if (!isDestroyed) status.text = error.message ?: "Document operation failed"
        }
      }
    }
  }

  companion object {
    private const val EXPORT_BACKUP = 201
    private const val IMPORT_BACKUP = 202
    private const val EXPORT_CERTIFICATE = 203
    private const val REPLACE_CERTIFICATE = 204
    private const val PAIR_CERTIFICATE = 205
    private const val PAIR_DEVICE = 206
    private const val MAX_BACKUP = 2 * 1024 * 1024
  }
}
