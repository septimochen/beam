import AppKit
import SwiftUI
import UserNotifications

@MainActor
enum FileDrop {
  static func urls(_ pasteboard: NSPasteboard) -> [URL] {
    guard pasteboard.pasteboardItems?.count == 1 else { return [] }
    return pasteboard.readObjects(forClasses: [NSURL.self]) as? [URL] ?? []
  }

  static func operation(_ sender: any NSDraggingInfo) -> NSDragOperation {
    operation(
      pasteboard: sender.draggingPasteboard, sourceOperations: sender.draggingSourceOperationMask)
  }

  static func operation(pasteboard: NSPasteboard, sourceOperations: NSDragOperation)
    -> NSDragOperation
  {
    guard sourceOperations.contains(.copy),
      (try? BeamDesktopBridge.selectedFile(urls: urls(pasteboard))) != nil
    else { return [] }
    return .copy
  }
}

@MainActor
final class FileDropHostingView: NSHostingView<BeamView> {
  let model: DesktopModel

  init(model: DesktopModel) {
    self.model = model
    super.init(rootView: BeamView(model: model))
    registerForDraggedTypes([.fileURL])
  }

  required init(rootView: BeamView) {
    self.model = rootView.model
    super.init(rootView: rootView)
    registerForDraggedTypes([.fileURL])
  }

  @available(*, unavailable)
  required init?(coder: NSCoder) { fatalError("Use init(model:)") }

  func droppedURLs(_ pasteboard: NSPasteboard) -> [URL] {
    FileDrop.urls(pasteboard)
  }

  override func draggingEntered(_ sender: any NSDraggingInfo) -> NSDragOperation {
    FileDrop.operation(sender)
  }

  override func draggingUpdated(_ sender: any NSDraggingInfo) -> NSDragOperation {
    FileDrop.operation(sender)
  }

  override func performDragOperation(_ sender: any NSDraggingInfo) -> Bool {
    guard FileDrop.operation(sender) == .copy else { return false }
    return model.selectFiles(droppedURLs(sender.draggingPasteboard))
  }
}

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate,
  UNUserNotificationCenterDelegate
{
  let model: DesktopModel
  private(set) var window: NSWindow?
  private var statusItem: NSStatusItem?
  private var timer: Timer?

  init(model: DesktopModel) { self.model = model }

  func applicationDidFinishLaunching(_ notification: Notification) {
    let window = NSWindow(
      contentRect: NSRect(x: 0, y: 0, width: 660, height: 760),
      styleMask: [.titled, .closable, .miniaturizable, .resizable],
      backing: .buffered, defer: false)
    window.title = "Beam"
    window.delegate = self
    window.isReleasedWhenClosed = false
    window.contentView = FileDropHostingView(model: model)
    window.center()
    self.window = window
    installApplicationMenu()
    installStatusItem()
    UNUserNotificationCenter.current().delegate = self
    model.onNotification = { [weak self] message in self?.notify(message) }
    timer = Timer.scheduledTimer(
      timeInterval: 0.1, target: self, selector: #selector(poll), userInfo: nil, repeats: true)
    if let timer { RunLoop.main.add(timer, forMode: .common) }
    showWindow()
  }

  private func installApplicationMenu() {
    let menu = NSMenu()
    let application = NSMenuItem()
    let commands = NSMenu()
    commands.addItem(withTitle: "Open Beam", action: #selector(showWindow), keyEquivalent: "0")
      .target = self
    commands.addItem(.separator())
    commands.addItem(
      withTitle: "Quit Beam", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
    application.submenu = commands
    menu.addItem(application)
    let edit = NSMenuItem()
    edit.title = "Edit"
    let editing = NSMenu(title: "Edit")
    editing.addItem(withTitle: "Undo", action: Selector(("undo:")), keyEquivalent: "z")
    let redo = editing.addItem(withTitle: "Redo", action: Selector(("redo:")), keyEquivalent: "z")
    redo.keyEquivalentModifierMask = [.command, .shift]
    editing.addItem(.separator())
    editing.addItem(withTitle: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
    editing.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
    editing.addItem(withTitle: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
    editing.addItem(
      withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
    edit.submenu = editing
    menu.addItem(edit)
    let windowMenu = NSMenu(title: "Window")
    windowMenu.addItem(
      withTitle: "Close Window", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
    windowMenu.addItem(
      withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
    let windowItem = NSMenuItem()
    windowItem.title = "Window"
    windowItem.submenu = windowMenu
    menu.addItem(windowItem)
    NSApplication.shared.windowsMenu = windowMenu
    NSApplication.shared.mainMenu = menu
  }

  private func installStatusItem() {
    let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
    let image =
      NSImage(named: "BeamTemplate")
      ?? NSImage(systemSymbolName: "arrow.up.arrow.down.circle", accessibilityDescription: "Beam")
    image?.isTemplate = true
    image?.size = NSSize(width: 18, height: 18)
    image?.accessibilityDescription = "Beam"
    item.button?.image = image
    item.button?.toolTip = "Beam"
    let menu = NSMenu()
    menu.addItem(withTitle: "Open Beam", action: #selector(showWindow), keyEquivalent: "").target =
      self
    menu.addItem(
      withTitle: "Load clipboard text…", action: #selector(loadClipboard), keyEquivalent: ""
    ).target = self
    menu.addItem(withTitle: "Stop receiving", action: #selector(stopReceiving), keyEquivalent: "")
      .target = self
    menu.addItem(.separator())
    menu.addItem(
      withTitle: "Quit Beam", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
    item.menu = menu
    statusItem = item
  }

  @objc func showWindow() {
    window?.deminiaturize(nil)
    window?.makeKeyAndOrderFront(nil)
    NSApplication.shared.activate(ignoringOtherApps: true)
  }

  @objc private func loadClipboard() {
    showWindow()
    model.loadClipboard()
  }

  @objc private func stopReceiving() { model.stopReceiving() }
  @objc private func poll() { model.poll() }

  func windowShouldClose(_ sender: NSWindow) -> Bool {
    sender.orderOut(nil)
    return false
  }

  func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }

  func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool
  {
    showWindow()
    return false
  }

  func applicationWillTerminate(_ notification: Notification) {
    timer?.invalidate()
    timer = nil
    model.onNotification = nil
    model.shutdown()
    if let statusItem { NSStatusBar.system.removeStatusItem(statusItem) }
    statusItem = nil
  }

  private func notify(_ message: String) {
    let center = UNUserNotificationCenter.current()
    center.requestAuthorization(options: [.alert, .sound]) { allowed, _ in
      guard allowed else { return }
      let content = UNMutableNotificationContent()
      content.title = "Beam"
      content.body = message
      let request = UNNotificationRequest(
        identifier: UUID().uuidString, content: content, trigger: nil)
      UNUserNotificationCenter.current().add(request)
    }
  }

  nonisolated func userNotificationCenter(
    _ center: UNUserNotificationCenter, willPresent notification: UNNotification,
    withCompletionHandler completionHandler: @escaping (UNNotificationPresentationOptions) -> Void
  ) {
    completionHandler([.banner])
  }
}
