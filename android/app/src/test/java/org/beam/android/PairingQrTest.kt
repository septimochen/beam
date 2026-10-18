package org.beam.android

import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.qrcode.QRCodeWriter
import com.google.zxing.qrcode.decoder.ErrorCorrectionLevel
import java.util.Random
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class PairingQrTest {
  private fun image(text: String): ByteArray {
    val matrix =
      QRCodeWriter()
        .encode(
          text,
          BarcodeFormat.QR_CODE,
          400,
          400,
          mapOf(EncodeHintType.ERROR_CORRECTION to ErrorCorrectionLevel.M),
        )
    return ByteArray(400 * 400) { index ->
      if (matrix[index % 400, index / 400]) 0 else 255.toByte()
    }
  }

  @Test
  fun decodesOfflineAndPreservesInvitation() {
    val text = "beam-pair:1:ABC123"
    assertEquals(text, PairingQr.decode(image(text), 400, 400))
  }

  @Test
  fun decodesDenseAlignedInvitations() {
    val random = Random(0)
    val alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
    repeat(64) {
      val text =
        "beam-pair:1:" +
          (0 until 350).map { alphabet[random.nextInt(alphabet.length)] }.joinToString("")
      assertEquals(text, PairingQr.decode(image(text), 400, 400))
    }
  }

  @Test
  fun rejectsUnrelatedQrAndInvalidImageBounds() {
    assertThrows(IllegalArgumentException::class.java) {
      PairingQr.decode(image("https://example.com"), 400, 400)
    }
    assertThrows(IllegalArgumentException::class.java) {
      PairingQr.decode(ByteArray(1), Int.MAX_VALUE, Int.MAX_VALUE)
    }
  }
}
