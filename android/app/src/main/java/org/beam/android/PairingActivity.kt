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
import android.widget.ScrollView
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
  private var devices = emptyList<Pair<String, String>>()
  private var lastInvitation = ""
  private var lastState = ""
  private var snapshot = emptyList<String>()
  private val name
    get() = Build.MODEL.filter { it.code in 32..126 }.take(63).ifBlank { "Beam Android" }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    val column =
      LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(24, 64, 24, 64)
      }
    setContentView(ScrollView(this).apply { addView(column) })
    fun label(text: String) =
      TextView(this).also {
        it.text = text
        column.addView(it)
      }
    fun input(hint: String, max: Int) =
      EditText(this).also {
        it.hint = hint
        it.filters = arrayOf(InputFilter.LengthFilter(max))
        it.setSingleLine()
        column.addView(it)
      }
    fun button(text: String, action: () -> Unit) =
      Button(this).also {
        it.text = text
        it.setOnClickListener {
          errorLabel.text = ""
          attempt(action)
        }
        column.addView(it)
      }
    label("Add device")
    label(
      "Open Add device on both devices on the same local network. Pairing closes after three minutes or when you leave this screen."
    )
    status = label("Starting pairing…")
    errorLabel = label("")
    pinLabel = label("")
    qr =
      ImageView(this).also {
        it.contentDescription = "Pairing QR code"
        column.addView(
          it,
          LinearLayout.LayoutParams(-1, (300 * resources.displayMetrics.density).toInt()),
        )
      }
    button("Show QR code") {
      qr.isVisible = !qr.isVisible
    }
    qr.visibility = View.GONE
    button("Copy invitation") {
      val text = snapshot.getOrNull(2).orEmpty()
      check(text.isNotEmpty()) { "Open a new invitation first" }
      getSystemService(ClipboardManager::class.java)
        .setPrimaryClip(ClipData.newPlainText("Beam pairing invitation", text))
    }
    button("Scan QR code") { scan() }
    invitation = input("Paste a Beam invitation", 2048)
    button("Use invitation") { call(Command.PAIRING_JOIN, invitation.text.toString().trim()) }
    nearby =
      Spinner(this).also {
        it.contentDescription = "Nearby device"
        column.addView(it)
      }
    button("Find nearby devices") { discovery.browse() }
    code =
      input("Six-digit PIN shown on the other device", 6).apply {
        inputType = android.text.InputType.TYPE_CLASS_NUMBER
      }
    button("Pair nearby") {
      val endpoint =
        devices.getOrNull(nearby.selectedItemPosition)?.second
          ?: error("Choose a nearby device first")
      call(Command.PAIRING_PIN, endpoint, code.text.toString())
    }
    alias = input("Local device alias", 63)
    approve = button("Approve device") { call(Command.PAIRING_APPROVE, alias.text.toString()) }
    decline = button("Decline pairing") { call(Command.PAIRING_REJECT) }
    button("New invitation") { call(Command.PAIRING_HOST, name) }
    button("Forget pending approval") { call(Command.PAIRING_FORGET) }
    button("Done") { finish() }
    discovery =
      PairingDiscovery(this) { values ->
        devices = values
        nearby.adapter =
          ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, values.map { it.first })
      }
    val settings = getSharedPreferences("settings", MODE_PRIVATE)
    session =
      NativeSession(
        File(filesDir.canonicalFile, "identity").path,
        settings.getInt("port", 4269),
        settings.getInt("timeout", 30) * 1000,
      )
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
              "$state: ${snapshot[1]}" +
                if (state == "awaiting approval") "\nPair with ${snapshot[4]}?" else ""
            pinLabel.text = if (state == "hosting") "PIN: ${snapshot[3]}" else ""
            approve.visibility = if (state == "awaiting approval") View.VISIBLE else View.GONE
            alias.visibility = approve.visibility
            decline.visibility = approve.visibility
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
      else errorLabel.text = "Camera access denied. Paste an invitation or use nearby PIN pairing."
    }
  }
}
