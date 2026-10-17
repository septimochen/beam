import AppKit
import Combine

enum ContentSelection: String, CaseIterable, Identifiable {
  case file = "File"
  case text = "Text"
  var id: Self { self }
  var bridgeKind: BeamContentKind {
    switch self {
    case .file: return .file
    case .text: return .text
    }
  }
}

@MainActor
final class DesktopModel: ObservableObject {
  @Published var peers: [String] = []
  @Published var peer = ""
  @Published var kind: ContentSelection = .file
  @Published var file = ""
  @Published var content = ""
  @Published var output: String
  @Published var sending = false
  @Published var receiving = false
  @Published var sendStatus = "Choose a paired device and content."
  @Published var receiveStatus = "Stopped."
  @Published var receivedText: String?
  @Published var checking = false
  @Published var connectivityStatus = ""
  @Published var lastResult = "No completed transfers yet."
  @Published var sendProgress: Double? = 0
  @Published var receiveProgress: Double? = 0
  var onNotification: ((String) -> Void)?
  let bridge: BeamDesktopBridge
  let port: Int
  private let defaults: UserDefaults
  private var receiveStartPending = true
  private var useDefaultDirectory: Bool

  init(
    bridge: BeamDesktopBridge, port: Int, output: String = "", defaults: UserDefaults = .standard
  ) {
    self.bridge = bridge
    self.port = port
    self.defaults = defaults
    let remembered = defaults.string(forKey: "receiveDirectory") ?? ""
    useDefaultDirectory = output.isEmpty && remembered.isEmpty
    self.output =
      !output.isEmpty
      ? output
      : !remembered.isEmpty
        ? remembered
        : FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Downloads").path
    refreshPeers()
  }

  func refreshPeers() {
    do {
      peers = try bridge.peers()
      if !peers.contains(peer) { peer = peers.first ?? "" }
      if peers.isEmpty {
        sendStatus = "No paired devices. Pair using the beam CLI, then refresh."
      }
    } catch {
      peers = []
      peer = ""
      sendStatus = "Pairing setup needed: \(error.localizedDescription)"
    }
    restartReceiving()
  }

  private func startReceivingWhenReady() {
    guard receiveStartPending, !bridge.receiving else { return }
    receiveStartPending = false
    guard !peers.isEmpty else {
      receiveStatus = "Set up identity and pair a device with the beam CLI, then refresh devices."
      return
    }
    startReceiving()
  }

  func selectReceiveFolder(_ directory: String) {
    guard !directory.isEmpty else { return }
    output = directory
    defaults.set(directory, forKey: "receiveDirectory")
    useDefaultDirectory = false
    restartReceiving()
  }

  private func restartReceiving() {
    receiveStartPending = true
    if bridge.receiving {
      bridge.stopReceiving()
      receiveStatus = "Updating receive settings…"
    } else {
      startReceivingWhenReady()
    }
  }

  // Called only by an explicit Load action, never during startup or polling.
  func loadClipboard(_ pasteboard: NSPasteboard = .general) {
    guard let snapshot = pasteboard.string(forType: .string) else {
      sendStatus = "Clipboard has no text; choose a file for other content."
      return
    }
    do {
      try BeamDesktopBridge.validate(content: snapshot, kind: .text)
      content = snapshot
      kind = .text
      sendStatus = "Clipboard snapshot loaded. Review it, choose a device, then Send."
    } catch {
      sendStatus = error.localizedDescription
    }
  }

  @discardableResult
  func selectFiles(_ urls: [URL]) -> Bool {
    do {
      file = try BeamDesktopBridge.selectedFile(urls: urls)
      kind = .file
      sendStatus = "File selected. Choose a device, then Send."
      return true
    } catch {
      sendStatus = error.localizedDescription
      return false
    }
  }

  func chooseFile() {
    let panel = NSOpenPanel()
    panel.title = "Choose one file"
    panel.canChooseDirectories = false
    panel.allowsMultipleSelection = false
    if panel.runModal() == .OK { selectFiles(panel.urls) }
  }

  func chooseFolder() {
    let panel = NSOpenPanel()
    panel.title = "Receive folder"
    panel.canChooseFiles = false
    panel.canChooseDirectories = true
    panel.allowsMultipleSelection = false
    if panel.runModal() == .OK, let url = panel.url { selectReceiveFolder(url.path) }
  }

  func send() {
    do {
      try bridge.send(peer: peer, kind: kind.bridgeKind, file: file, content: content)
      sending = bridge.sending
      sendProgress = 0
      sendStatus = "Sending…"
    } catch {
      sendStatus = error.localizedDescription
    }
  }

  func checkConnectivity() {
    do {
      try bridge.checkPeer(peer)
      checking = bridge.checking
      connectivityStatus = "Checking \(peer)…"
    } catch { connectivityStatus = error.localizedDescription }
  }

  func copyText(_ text: String, to pasteboard: NSPasteboard = .general) {
    pasteboard.clearContents()
    pasteboard.setString(text, forType: .string)
  }

  private func startReceiving() {
    do {
      if useDefaultDirectory {
        try FileManager.default.createDirectory(atPath: output, withIntermediateDirectories: true)
      }
      try bridge.startReceiving(directory: output)
      receiving = bridge.receiving
      checking = bridge.checking
      receiveStatus = "Starting…"
      receiveProgress = 0
    } catch {
      receiveStatus = "Transfer stopped: \(error.localizedDescription)"
    }
  }

  func poll() {
    do {
      for event in try bridge.events() { report(event) }
      sending = bridge.sending
      receiving = bridge.receiving
      checking = bridge.checking
      if receiveStartPending {
        receiveStatus = "Updating receive settings…"
        startReceivingWhenReady()
      }
    } catch {
      lastResult = error.localizedDescription
    }
  }

  private func report(_ event: BeamDesktopEvent) {
    if event.checking {
      connectivityStatus = event.message
      return
    }
    var status: String
    var progress: Double? = event.sending ? sendProgress : receiveProgress
    switch event.kind {
    case .progress:
      switch event.stage {
      case .hashing: status = "Hashing"
      case .connecting: status = "Connecting"
      case .waiting: status = "Waiting"
      case .transferring: status = "Transferring"
      case .verifying: status = "Verifying"
      case .complete: status = "Verified"
      @unknown default: status = "Working…"
      }
      let counted = event.stage == .hashing || event.stage == .transferring
      progress =
        counted ? (event.total == 0 ? 0 : min(1, Double(event.bytes) / Double(event.total))) : nil
      if event.stage == .transferring {
        let percent = event.total == 0 ? 0 : Int(100 * Double(event.bytes) / Double(event.total))
        status +=
          " · \(percent)% · " + String(format: "%.1f MiB/s", event.bytesPerSecond / 1_048_576)
      }
    case .listening:
      status = "Listening for paired devices on port \(port)."
    case .completed:
      let description =
        event.sending
        ? "Sent and verified with \(event.message)" : "Received and verified \(event.filename)"
      status = "\(description) (\(event.bytes) bytes)"
      progress = 1
      lastResult = status
      if !event.sending && event.contentKind == .text { receivedText = event.text }
      onNotification?(status)
    case .failed:
      status = "Transfer stopped: \(event.message)"
      progress = 0
      lastResult = status
      onNotification?(status)
    case .warning:
      status = "LAN advertisement unavailable: \(event.message)"
    case .stopped:
      status = receiveStatus.hasPrefix("Transfer stopped:") ? receiveStatus : "Receiver stopped."
      progress = 0
    @unknown default:
      return
    }
    if event.sending {
      sendStatus = status
      sendProgress = progress
    } else {
      receiveStatus = status
      receiveProgress = progress
    }
  }

  func shutdown() {
    receiveStartPending = false
    bridge.shutdown()
    sending = false
    receiving = false
    checking = false
  }
}
