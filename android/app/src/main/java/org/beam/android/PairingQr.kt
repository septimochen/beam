package org.beam.android

import com.google.zxing.BinaryBitmap
import com.google.zxing.DecodeHintType
import com.google.zxing.PlanarYUVLuminanceSource
import com.google.zxing.ReaderException
import com.google.zxing.common.HybridBinarizer
import com.google.zxing.qrcode.QRCodeReader

/** Decodes only bounded Beam pairing QR payloads, with no network or trust side effects. */
internal object PairingQr {
  fun decode(data: ByteArray, width: Int, height: Int): String {
    require(
      width in 1..2048 &&
        height in 1..2048 &&
        width.toLong() * height <= 2_097_152 &&
        data.size.toLong() == width.toLong() * height
    )
    val source = PlanarYUVLuminanceSource(data, width, height, 0, 0, width, height, false)
    val bitmap = BinaryBitmap(HybridBinarizer(source))
    val text =
      try {
        QRCodeReader().decode(bitmap, mapOf(DecodeHintType.TRY_HARDER to true)).text
      } catch (_: ReaderException) {
        // The standard pure-symbol path also handles screen captures and
        // aligned symbols whose dense data resembles finder patterns. A false
        // detection can fail format/checksum validation as well as discovery.
        QRCodeReader().decode(bitmap, mapOf(DecodeHintType.PURE_BARCODE to true)).text
      }
    require(text.length <= 2048 && text.startsWith("beam-pair:2:")) {
      "Not a Beam pairing invitation"
    }
    return text
  }
}
