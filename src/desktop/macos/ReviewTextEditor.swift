import AppKit
import SwiftUI

// NSTextView's default file drop inserts a URL into the payload. Route drops
// through the same copy-only selection policy as the rest of the window.
@MainActor
final class ReviewTextView: NSTextView {
  weak var model: DesktopModel?

  @discardableResult
  func selectDrop(_ pasteboard: NSPasteboard) -> Bool {
    model?.selectFiles(FileDrop.urls(pasteboard)) ?? false
  }

  override func draggingEntered(_ sender: any NSDraggingInfo) -> NSDragOperation {
    FileDrop.operation(sender)
  }

  override func draggingUpdated(_ sender: any NSDraggingInfo) -> NSDragOperation {
    FileDrop.operation(sender)
  }

  override func prepareForDragOperation(_ sender: any NSDraggingInfo) -> Bool {
    FileDrop.operation(sender) == .copy
  }

  override func performDragOperation(_ sender: any NSDraggingInfo) -> Bool {
    guard FileDrop.operation(sender) == .copy else { return false }
    return selectDrop(sender.draggingPasteboard)
  }
}

struct ReviewTextEditor: NSViewRepresentable {
  @ObservedObject var model: DesktopModel

  func makeCoordinator() -> Coordinator { Coordinator(model: model) }

  func makeNSView(context: Context) -> NSScrollView {
    let scroll = NSScrollView()
    scroll.hasVerticalScroller = true
    let editor = ReviewTextView(frame: .zero)
    editor.model = model
    editor.delegate = context.coordinator
    editor.isRichText = false
    editor.allowsUndo = true
    editor.isAutomaticQuoteSubstitutionEnabled = false
    editor.isAutomaticDashSubstitutionEnabled = false
    editor.isAutomaticTextReplacementEnabled = false
    editor.isAutomaticSpellingCorrectionEnabled = false
    editor.isAutomaticLinkDetectionEnabled = false
    editor.isVerticallyResizable = true
    editor.isHorizontallyResizable = false
    editor.minSize = .zero
    editor.maxSize = NSSize(
      width: CGFloat.greatestFiniteMagnitude, height: CGFloat.greatestFiniteMagnitude)
    editor.autoresizingMask = [.width]
    editor.textContainer?.containerSize = NSSize(width: 0, height: CGFloat.greatestFiniteMagnitude)
    editor.textContainer?.widthTracksTextView = true
    editor.textContainerInset = NSSize(width: 6, height: 6)
    editor.font = NSFont.monospacedSystemFont(ofSize: NSFont.systemFontSize, weight: .regular)
    editor.setAccessibilityLabel("Content to send")
    editor.registerForDraggedTypes([.fileURL])
    scroll.documentView = editor
    return scroll
  }

  func updateNSView(_ scroll: NSScrollView, context: Context) {
    guard let editor = scroll.documentView as? ReviewTextView else { return }
    if editor.string != model.content { editor.string = model.content }
  }

  @MainActor
  final class Coordinator: NSObject, NSTextViewDelegate {
    let model: DesktopModel
    init(model: DesktopModel) { self.model = model }
    func textDidChange(_ notification: Notification) {
      guard let editor = notification.object as? ReviewTextView else { return }
      model.content = editor.string
    }
  }
}
