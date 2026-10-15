package org.beam.android

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class SelectionTest {
  @Test
  fun rejectsUnsafeNamesAndSchemes() {
    listOf("../x", ".hidden", "a/b", "a\\b", "a\u0000b", "CON.txt", "COM1", "x.", " x", "世界")
      .forEach {
        assertThrows(IllegalArgumentException::class.java) { Selection.filename(it) }
      }
    assertEquals("photo.jpg", Selection.filename("photo.jpg"))
    assertThrows(IllegalArgumentException::class.java) { Selection.contentUri("file", "") }
    assertThrows(IllegalArgumentException::class.java) {
      Selection.contentUri("https", "example.com")
    }
    Selection.contentUri("content", "documents")
  }

  @Test
  fun boundsBytesRatherThanCharacters() {
    assertThrows(IllegalArgumentException::class.java) { Selection.text("世".repeat(22000)) }
    assertEquals("🌍世界", Selection.text("🌍世界"))
    assertThrows(IllegalArgumentException::class.java) { Selection.text("a\u0000b") }
  }

  @Test
  fun streamsUnknownSizesAndStopsOnLimitsAndCancellation() {
    val bytes = ByteArray(150000) { (it % 251).toByte() }
    val output = ByteArrayOutputStream()
    assertEquals(
      bytes.size.toLong(),
      Selection.copy(ByteArrayInputStream(bytes), output, bytes.size.toLong()) { false },
    )
    org.junit.Assert.assertArrayEquals(bytes, output.toByteArray())
    assertThrows(IllegalArgumentException::class.java) {
      Selection.copy(ByteArrayInputStream(bytes), ByteArrayOutputStream(), 65536) { false }
    }
    assertThrows(IllegalStateException::class.java) {
      Selection.copy(ByteArrayInputStream(bytes), ByteArrayOutputStream(), 65536) { true }
    }
    assertEquals(
      0L,
      Selection.copy(ByteArrayInputStream(byteArrayOf()), ByteArrayOutputStream(), 0) { false },
    )
  }
}
