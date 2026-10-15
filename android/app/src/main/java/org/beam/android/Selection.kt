package org.beam.android

import java.io.InputStream
import java.io.OutputStream

// Keep provider metadata, URI parsing, and byte limits independent of the UI.
internal object Selection {
  const val MAX_TEXT = 65536
  const val MAX_CERTIFICATE = 16384
  const val MAX_FILE = 4L * 1024 * 1024 * 1024

  fun filename(name: String): String {
    require(
      name.isNotEmpty() &&
        name.length <= 255 &&
        !name.startsWith('.') &&
        !name.startsWith(' ') &&
        !name.endsWith('.') &&
        !name.endsWith(' ')
    ) {
      "Choose a file with a supported ASCII filename"
    }
    require(name.all { it.code in 32..126 && it !in "/\\:<>\"|?*" }) {
      "Choose a file with a supported ASCII filename"
    }
    val base = name.substringBefore('.').uppercase(java.util.Locale.ROOT)
    require(
      !base.endsWith(' ') &&
        base !in setOf("CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$") &&
        !base.matches(Regex("(COM|LPT)[1-9]"))
    ) {
      "Reserved filename"
    }
    return name
  }

  fun contentUri(scheme: String?, authority: String?) {
    require(scheme == "content" && !authority.isNullOrEmpty()) {
      "Choose one content-provider file"
    }
  }

  fun text(value: String): String {
    require(value.toByteArray(Charsets.UTF_8).size <= MAX_TEXT) { "Text exceeds 64 KiB" }
    require(value.none { it == '\u0000' }) { "Text contains NUL" }
    return value
  }

  fun copy(
    input: InputStream,
    output: OutputStream,
    maximum: Long,
    cancelled: () -> Boolean,
  ): Long {
    val buffer = ByteArray(65536)
    var total = 0L
    while (true) {
      check(!cancelled()) { "Selection cancelled" }
      val count = input.read(buffer)
      if (count < 0) return total
      require(count.toLong() <= maximum - total) { "Selected content exceeds the size limit" }
      output.write(buffer, 0, count)
      total += count
    }
  }
}
