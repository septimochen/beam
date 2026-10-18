package org.beam.android

import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.ColorDrawable
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.RadioGroup
import android.widget.ScrollView
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.*
import org.junit.Test

class MainLayoutTest {
  @Test
  fun shareReviewKeepsContentControlsInTheirSections() {
    val instrumentation = InstrumentationRegistry.getInstrumentation()
    val context = instrumentation.targetContext
    val activity =
      instrumentation.startActivitySync(
        Intent(context, MainActivity::class.java)
          .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
          .setAction(Intent.ACTION_SEND)
          .setType("text/plain")
          .putExtra(Intent.EXTRA_TEXT, "Review this text before sending 🌍")
      ) as MainActivity
    fun descendants(view: View): List<View> =
      listOf(view) +
        if (view is ViewGroup)
          (0 until view.childCount).flatMap { descendants(view.getChildAt(it)) }
        else emptyList()
    fun render(name: String) {
      if (InstrumentationRegistry.getArguments().getString("renderShare") != "true") return
      val container = activity.findViewById<ViewGroup>(android.R.id.content)
      val scroll = container.getChildAt(0) as ScrollView
      val column = scroll.getChildAt(0)
      val bitmap = Bitmap.createBitmap(column.width, column.height, Bitmap.Config.ARGB_8888)
      val canvas = Canvas(bitmap)
      canvas.drawColor((scroll.background as ColorDrawable).color)
      column.draw(canvas)
      File(context.cacheDir, name).outputStream().use {
        bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)
      }
      bitmap.recycle()
    }
    try {
      instrumentation.waitForIdleSync()
      instrumentation.runOnMainSync {
        val outgoing = activity.findViewById<ViewGroup>(R.id.send_card)
        val incoming = activity.findViewById<ViewGroup>(R.id.receive_card)
        assertTrue("Send should precede Receive, as on desktop", outgoing.top < incoming.top)
        val controls = descendants(outgoing)
        val chooser = controls.filterIsInstance<Button>().single { it.text == "Choose file" }
        val review =
          controls.filterIsInstance<EditText>().single {
            it.contentDescription == "Review outgoing text"
          }
        assertFalse("File picker should be hidden for shared text", chooser.isShown)
        assertTrue(review.isShown)
        assertEquals("Review this text before sending 🌍", review.text.toString())
        assertFalse(
          "Empty received text should stay hidden",
          descendants(incoming).any { it.contentDescription == "Received text" && it.isShown },
        )
        val mainButtons =
          descendants(activity.window.decorView).filterIsInstance<Button>().map {
            it.text.toString()
          }
        assertFalse(mainButtons.any { it.contains("Add device") || it.contains("Recover") })
        assertFalse(mainButtons.contains("Unpair device"))
        render("share-text-ui.png")
        controls.filterIsInstance<RadioGroup>().single().check(R.id.kind_file)
        assertTrue("Switching to File should show the picker", chooser.isShown)
        assertFalse(review.isShown)
      }
      instrumentation.waitForIdleSync()
      instrumentation.runOnMainSync {
        for (button in descendants(activity.window.decorView).filterIsInstance<Button>()) {
          if (!button.isShown) continue
          val layout = button.layout ?: continue
          for (line in 0 until layout.lineCount - 1) {
            val end = layout.getLineEnd(line)
            val text = button.text
            assertTrue(
              "Button label splits a word: $text",
              end == 0 ||
                end >= text.length ||
                text[end - 1].isWhitespace() ||
                text[end].isWhitespace(),
            )
          }
        }
        render("share-file-ui.png")
      }
    } finally {
      instrumentation.runOnMainSync { activity.finish() }
    }
  }
}
