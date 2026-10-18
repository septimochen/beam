package org.beam.android

import android.Manifest
import android.annotation.SuppressLint
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputFilter
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.Spinner
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.graphics.createBitmap
import androidx.core.graphics.scale
import androidx.core.graphics.set
import androidx.core.view.isVisible
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

@SuppressLint("SetTextI18n")
class PairingActivity : ComponentActivity() {
  private var session: NativeSession? = null
  private lateinit var discovery: PairingDiscovery
  private val main = Handler(Looper.getMainLooper())
  private val cameraExecutor = Executors.newSingleThreadExecutor()
  private var provider: ProcessCameraProvider? = null
  private var cameraDialog: AlertDialog? = null
  private var running = false
  private lateinit var status: TextView
  private lateinit var pinLabel: TextView
  private lateinit var errorLabel: TextView
  private lateinit var decline: Button
  private lateinit var qr: ImageView
  private lateinit var invitation: EditText
  private lateinit var alias: EditText
  private lateinit var approve: Button
  private lateinit var nearby: Spinner
  private lateinit var code: EditText
  private lateinit var approvalCard: LinearLayout
  private lateinit var connectionCard: LinearLayout
  private lateinit var invitationCard: LinearLayout
  private lateinit var localCard: LinearLayout
  private lateinit var pastedInvitation: LinearLayout
  private lateinit var newInvitation: Button
  private lateinit var forgetApproval: Button
  private lateinit var done: Button
  private val ui by lazy { BeamUi(this) }
  private var devices = emptyList<Pair<String, String>>()
  private var lastInvitation = ""
  private var lastState = ""
  private var snapshot = emptyList<String>()
  private val name
    get() = Build.MODEL.filter { it.code in 32..126 }.take(63).ifBlank { "Beam Android" }

  override fun onCreate(savedInstanceState: Bundle?) {
    ui.applyTheme()
    super.onCreate(savedInstanceState)
    val column = ui.screen()
    column.addView(button("‹ Back") { finish() })
    column.addView(ui.label("Add device", 30f, true))
    column.addView(ui.label("Open Add device on your other device.", 14f))
    status = ui.message()
    column.addView(status)
    errorLabel = ui.message().apply { setTextColor(ui.accent) }
    column.addView(errorLabel)

    approvalCard = ui.card().apply { visibility = View.GONE }
    approvalCard.addView(ui.label("Confirm device", 20f, true))
    alias = input("Device name", 63)
    approvalCard.addView(alias)
    val approvalActions = ui.actionRow()
    approve =
      button("Approve device", true) { call(Command.PAIRING_APPROVE, alias.text.toString()) }
    decline = button("Decline") { call(Command.PAIRING_REJECT) }
    approvalActions.addView(approve, ui.weight())
    approvalActions.addView(decline, ui.weight())
    approvalCard.addView(approvalActions)
    column.addView(approvalCard)

    connectionCard = ui.card()
    connectionCard.addView(button("Scan QR code", true) { scan() })
    connectionCard.addView(ui.divider())
    connectionCard.addView(ui.label("Nearby devices", 18f, true))
    nearby =
      Spinner(this).apply {
        contentDescription = "Nearby device"
        minimumHeight = ui.dp(48)
        backgroundTintList = android.content.res.ColorStateList.valueOf(ui.accent)
        adapter =
          ArrayAdapter(context, android.R.layout.simple_spinner_dropdown_item, listOf("Searching…"))
      }
    connectionCard.addView(nearby)
    code =
      input("Six-digit PIN", 6).apply {
        inputType = android.text.InputType.TYPE_CLASS_NUMBER
      }
    connectionCard.addView(code)
    val nearbyActions = ui.actionRow()
    nearbyActions.addView(button("Refresh") { discovery.browse() }, ui.weight())
    nearbyActions.addView(
      button("Pair nearby") {
        val endpoint =
          devices.getOrNull(nearby.selectedItemPosition)?.second
            ?: error("Choose a nearby device first")
        call(Command.PAIRING_PIN, endpoint, code.text.toString())
      },
      ui.weight(),
    )
    connectionCard.addView(nearbyActions)
    column.addView(connectionCard)

    localCard = ui.card()
    localCard.addView(ui.label("This device’s PIN", 18f, true))
    pinLabel = ui.label("…", 32f, true).apply { typeface = android.graphics.Typeface.MONOSPACE }
    localCard.addView(pinLabel)
    qr =
      ImageView(this).apply {
        contentDescription = "Pairing QR code"
        visibility = View.GONE
        adjustViewBounds = true
      }
    localCard.addView(qr, LinearLayout.LayoutParams(-1, ui.dp(280)))
    val invitationActions = ui.actionRow()
    invitationActions.addView(
      button("Show QR code") {
        qr.isVisible = !qr.isVisible
        (invitationActions.getChildAt(0) as Button).text =
          if (qr.isVisible) "Hide QR code" else "Show QR code"
      },
      ui.weight(),
    )
    invitationActions.addView(
      button("Copy invitation") {
        val text = snapshot.getOrNull(2).orEmpty()
        check(text.isNotEmpty()) { "Open a new invitation first" }
        getSystemService(ClipboardManager::class.java)
          .setPrimaryClip(ClipData.newPlainText("Beam pairing invitation", text))
      },
      ui.weight(),
    )
    localCard.addView(invitationActions)
    column.addView(localCard)

    invitationCard = ui.card()
    invitationCard.addView(
      button("Paste invitation") { pastedInvitation.isVisible = !pastedInvitation.isVisible }
    )
    pastedInvitation = ui.column().apply { visibility = View.GONE }
    invitation = input("Beam invitation", 2048)
    pastedInvitation.addView(invitation)
    pastedInvitation.addView(
      button("Use invitation") { call(Command.PAIRING_JOIN, invitation.text.toString().trim()) }
    )
    invitationCard.addView(pastedInvitation)
    column.addView(invitationCard)
    newInvitation =
      button("New invitation") { call(Command.PAIRING_HOST, name) }.apply { visibility = View.GONE }
    column.addView(newInvitation)
    forgetApproval =
      button("Forget pending approval") { call(Command.PAIRING_FORGET) }
        .apply { visibility = View.GONE }
    column.addView(forgetApproval)
    done = button("Done", true) { finish() }.apply { visibility = View.GONE }
    column.addView(done)
    discovery =
      PairingDiscovery(this) { values ->
        devices = values
        nearby.adapter =
          ArrayAdapter(
            this,
            android.R.layout.simple_spinner_dropdown_item,
            values.map { it.first }.ifEmpty { listOf("No nearby devices") },
          )
      }
    val settings = getSharedPreferences("settings", MODE_PRIVATE)
    session =
      NativeSession(
        File(filesDir.canonicalFile, "identity").path,
        settings.getInt("port", 4269),
        settings.getInt("timeout", 30) * 1000,
      )
  }

  private fun input(hint: String, max: Int) =
    EditText(this).apply {
      this.hint = hint
      contentDescription = hint
      filters = arrayOf(InputFilter.LengthFilter(max))
      setSingleLine()
      ui.styleInput(this)
    }

  private fun button(title: String, primary: Boolean = false, action: () -> Unit) =
    ui.button(title, primary) {
      errorLabel.text = ""
      attempt(action)
    }

  override fun onStart() {
    super.onStart()
    running = true
    attempt {
      check(
        Build.VERSION.SDK_INT < 37 ||
          checkSelfPermission(Manifest.permission.ACCESS_LOCAL_NETWORK) ==
            PackageManager.PERMISSION_GRANTED
      ) {
        "Allow local network access in Beam first"
      }
      call(Command.PAIRING_HOST, name)
      discovery.browse()
    }
    main.post(poll)
  }

  override fun onStop() {
    running = false
    main.removeCallbacks(poll)
    discovery.close()
    cameraDialog?.dismiss()
    provider?.unbindAll()
    runCatching { session?.call(Command.PAIRING_CANCEL) }
    super.onStop()
  }

  override fun onDestroy() {
    session?.close()
    session = null
    cameraExecutor.shutdownNow()
    super.onDestroy()
  }

  private fun call(command: Int, vararg arguments: String) {
    checkNotNull(session).call(command, *arguments)
  }

  private fun attempt(action: () -> Unit) {
    try {
      action()
    } catch (error: Exception) {
      errorLabel.text = error.message ?: "Pairing failed"
    }
  }

  private val poll =
    object : Runnable {
      override fun run() {
        if (!running) return
        attempt {
          snapshot = checkNotNull(session).call(Command.PAIRING_SNAPSHOT)
          if (snapshot.size >= 8) {
            val state = snapshot[0]
            status.text =
              when (state) {
                "hosting" -> ""
                "awaiting approval" -> "Pair with ${snapshot[4]}?"
                "paired" -> "Device added"
                else -> snapshot[1]
              }
            pinLabel.text = snapshot[3]
            approvalCard.isVisible = state == "awaiting approval"
            connectionCard.isVisible = state in listOf("hosting", "incomplete")
            localCard.isVisible = state == "hosting"
            invitationCard.isVisible = connectionCard.isVisible
            newInvitation.isVisible =
              state in listOf("idle", "expired", "rejected", "incomplete", "failed", "cancelled")
            forgetApproval.isVisible = state == "incomplete"
            done.isVisible = state == "paired"
            if (state != lastState) {
              lastState = state
              if (state == "awaiting approval") alias.setText(snapshot[5])
              if (state == "paired") setResult(RESULT_OK)
            }
            discovery.advertise(if (state == "hosting") snapshot[6].toInt() else 0, name)
            if (snapshot[2] != lastInvitation) {
              lastInvitation = snapshot[2]
              val rows = snapshot[7].lineSequence().filter { it.isNotEmpty() }.toList()
              if (rows.isEmpty()) qr.setImageDrawable(null)
              else {
                val size = rows.size + 8
                val bitmap = createBitmap(size, size, Bitmap.Config.ARGB_8888)
                bitmap.eraseColor(Color.WHITE)
                rows.forEachIndexed { y, row ->
                  row.forEachIndexed { x, cell ->
                    if (cell == '1') bitmap[x + 4, y + 4] = Color.BLACK
                  }
                }
                qr.setImageBitmap(bitmap.scale(size * 4, size * 4, false))
              }
            }
          }
        }
        main.postDelayed(this, 100)
      }
    }

  private fun scan() {
    if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
      requestPermissions(arrayOf(Manifest.permission.CAMERA), 22)
      return
    }
    startCamera()
  }

  @SuppressLint("MissingPermission") // Reached only after the explicit permission check/result.
  private fun startCamera() {
    val preview =
      PreviewView(this).apply { minimumHeight = (320 * resources.displayMetrics.density).toInt() }
    val scanning = AtomicBoolean(true)
    cameraDialog =
      AlertDialog.Builder(this)
        .setTitle("Scan Beam pairing QR")
        .setView(preview)
        .setNegativeButton("Cancel", null)
        .create()
        .also { dialog ->
          dialog.setOnDismissListener {
            scanning.set(false)
            provider?.unbindAll()
          }
          dialog.show()
        }
    val future = ProcessCameraProvider.getInstance(this)
    future.addListener(
      {
        attempt {
          if (!running || !scanning.get()) return@attempt
          val camera = future.get()
          provider = camera
          val display =
            Preview.Builder().build().also { it.surfaceProvider = preview.surfaceProvider }
          val analysis =
            ImageAnalysis.Builder()
              .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
              .build()
          analysis.setAnalyzer(cameraExecutor) { image ->
            try {
              if (scanning.get() && image.width * image.height <= 2_097_152) {
                val plane = image.planes[0]
                val buffer = plane.buffer
                val data = ByteArray(image.width * image.height)
                for (y in 0 until image.height) for (x in 0 until image.width) data[
                  y * image.width + x] = buffer.get(y * plane.rowStride + x * plane.pixelStride)
                val text = PairingQr.decode(data, image.width, image.height)
                if (
                  text.length <= 2048 &&
                    text.startsWith("beam-pair:1:") &&
                    scanning.compareAndSet(true, false)
                )
                  main.post {
                    if (running) {
                      cameraDialog?.dismiss()
                      invitation.setText(text)
                      attempt { call(Command.PAIRING_JOIN, text) }
                    }
                  }
              }
            } catch (_: Exception) {
              /* No valid Beam QR in this frame. */
            } finally {
              image.close()
            }
          }
          camera.unbindAll()
          camera.bindToLifecycle(this, CameraSelector.DEFAULT_BACK_CAMERA, display, analysis)
        }
      },
      mainExecutor,
    )
  }

  override fun onRequestPermissionsResult(
    code: Int,
    permissions: Array<String>,
    results: IntArray,
  ) {
    super.onRequestPermissionsResult(code, permissions, results)
    if (code == 22) {
      if (results.firstOrNull() == PackageManager.PERMISSION_GRANTED) attempt { startCamera() }
      else errorLabel.text = "Camera access denied. Use a PIN or paste an invitation."
    }
  }
}
