package org.beam.android

import android.app.Activity
import android.content.res.ColorStateList
import android.content.res.Configuration
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.os.Build
import android.view.Gravity
import android.view.View
import android.view.WindowInsets
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import androidx.core.graphics.toColorInt
import androidx.core.view.WindowInsetsControllerCompat
import androidx.core.view.isEmpty

/** Shared native presentation for the Share and Devices screens. */
internal class BeamUi(private val activity: Activity) {
  val dark =
    activity.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK ==
      Configuration.UI_MODE_NIGHT_YES
  val ink = color("#192235", "#F2F3FA")
  val muted = color("#58667D", "#ADB6CC")
  val surface = color("#FFFFFF", "#202531")
  val accent = color("#4B57CB", "#A5B4FC")
  private val backdrop = color("#F3F5FA", "#141822")
  private val border = color("#E1E5EF", "#343C50")
  private val field = color("#F3F5FA", "#191E2A")

  private fun color(light: String, night: String) = (if (dark) night else light).toColorInt()

  fun dp(value: Int) = (value * activity.resources.displayMetrics.density).toInt()

  fun applyTheme() {
    activity.setTheme(
      if (dark) android.R.style.Theme_Material_NoActionBar
      else android.R.style.Theme_Material_Light_NoActionBar
    )
  }

  fun screen(): LinearLayout {
    val column = column().apply { setPadding(dp(20), dp(12), dp(20), dp(24)) }
    val scroll =
      ScrollView(activity).apply {
        isFillViewport = true
        setBackgroundColor(backdrop)
        setOnApplyWindowInsetsListener { view, insets ->
          if (Build.VERSION.SDK_INT >= 30) {
            val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.ime())
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
        addOnLayoutChangeListener { view, _, _, _, _, _, _, _, _ ->
          val available = view.width - view.paddingLeft - view.paddingRight
          val side = maxOf(dp(20), (available - dp(720)) / 2)
          if (column.paddingLeft != side) column.setPadding(side, dp(12), side, dp(24))
        }
        addView(column)
      }
    activity.setContentView(scroll)
    WindowInsetsControllerCompat(activity.window, scroll).apply {
      isAppearanceLightStatusBars = !dark
      isAppearanceLightNavigationBars = !dark
    }
    return column
  }

  fun column() = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }

  fun row() =
    LinearLayout(activity).apply {
      orientation = LinearLayout.HORIZONTAL
      gravity = Gravity.CENTER_VERTICAL
    }

  /** Stack actions when larger text would split a word across lines. */
  fun actionRow() =
    row().apply {
      addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
        if (isEmpty() || width == 0) return@addOnLayoutChangeListener
        val available = (width - dp(6) * childCount) / childCount
        val stack =
          (0 until childCount).any { index ->
            val button = getChildAt(index) as Button
            val wordWidth = button.text.split(' ').maxOf { button.paint.measureText(it) }
            wordWidth + button.paddingLeft + button.paddingRight > available
          }
        val direction = if (stack) LinearLayout.VERTICAL else LinearLayout.HORIZONTAL
        if (orientation != direction) {
          orientation = direction
          for (index in 0 until childCount) {
            getChildAt(index).layoutParams =
              if (stack)
                LinearLayout.LayoutParams(-1, -2).apply { topMargin = if (index > 0) dp(6) else 0 }
              else weight()
          }
        }
      }
    }

  fun weight() = LinearLayout.LayoutParams(0, -2, 1f).apply { marginEnd = dp(6) }

  fun rounded(color: Int) =
    GradientDrawable().apply {
      setColor(color)
      cornerRadius = dp(20).toFloat()
      setStroke(dp(1), border)
    }

  fun card() =
    column().apply {
      background = rounded(surface)
      setPadding(dp(18), dp(14), dp(18), dp(14))
      layoutParams = LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(12) }
    }

  fun label(value: String, size: Float = 14f, bold: Boolean = false) =
    TextView(activity).apply {
      text = value
      textSize = size
      setTextColor(if (bold) ink else muted)
      if (bold) setTypeface(typeface, Typeface.BOLD)
      setPadding(0, dp(4), 0, dp(8))
      if (bold) isAccessibilityHeading = true
    }

  fun button(title: String, primary: Boolean = false, action: () -> Unit) =
    Button(activity).apply {
      text = title
      isAllCaps = false
      textSize = 14f
      minHeight = dp(48)
      minimumWidth = 0
      setPadding(dp(12), dp(8), dp(12), dp(8))
      val foreground = if (primary) surface else accent
      setTextColor(
        ColorStateList(
          arrayOf(intArrayOf(-android.R.attr.state_enabled), intArrayOf()),
          intArrayOf(muted, foreground),
        )
      )
      val fill =
        rounded(if (primary) accent else surface).apply {
          setColor(
            ColorStateList(
              arrayOf(intArrayOf(-android.R.attr.state_enabled), intArrayOf()),
              intArrayOf(field, if (primary) accent else surface),
            )
          )
          cornerRadius = dp(14).toFloat()
        }
      background = RippleDrawable(ColorStateList.valueOf(border), fill, null)
      setOnClickListener { action() }
    }

  fun styleInput(input: EditText) {
    input.apply {
      textSize = 16f
      setTextColor(ink)
      setHintTextColor(muted)
      setPadding(dp(14), dp(12), dp(14), dp(12))
      minimumHeight = dp(48)
      background = rounded(field)
      layoutParams = LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(8) }
    }
  }

  fun progress(description: String) =
    ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal).apply {
      max = 100
      contentDescription = description
      progressTintList = ColorStateList.valueOf(accent)
      progressBackgroundTintList = ColorStateList.valueOf(border)
      layoutParams = LinearLayout.LayoutParams(-1, dp(8)).apply { topMargin = dp(12) }
    }

  fun divider() =
    View(activity).apply {
      setBackgroundColor(border)
      layoutParams =
        LinearLayout.LayoutParams(-1, dp(1)).apply {
          topMargin = dp(12)
          bottomMargin = dp(12)
        }
      importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
    }
}
