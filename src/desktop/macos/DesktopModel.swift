import AppKit
import Combine

func humanSize(_ bytes: Double) -> String {
  let units = ["B", "KB", "MB", "GB", "TB", "PB", "EB"]
  var value = bytes
  var unit = 0
  while value >= 1000 && unit + 1 < units.count {
    value /= 1000
    unit += 1
  }
  return String(
    format: unit == 0 ? "%.0f %@" : "%.2f %@", locale: Locale(identifier: "en_US_POSIX"), value,
    units[unit])
}

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

struct ManagedDevice: Identifiable, Equatable {
  let name: String
  let endpoint: String
  let fallback: String
  let fingerprint: String
  let certificate: String
  var id: String { name }
}

@MainActor
final class DesktopModel: ObservableObject {
  @Published var peers: [String] = []
  @Published var devices: [ManagedDevice] = []
  @Published var deviceStatus = ""
  @Published var deviceError = ""
  @Published var deviceChecks: [String: String] = [:]
  private var checkedDevice = ""
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
  @Published var showPairing = false
  @Published var pairingValues: [String] = []
  @Published var pairingAlias = ""
  @Published var pairingInvitation = ""
  @Published var pairingError = ""
  private var lastPairingState = ""
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
      let values = try bridge.devices(command: "details", arguments: [])
      devices = stride(from: 0, to: values.count, by: 5).map {
        ManagedDevice(
          name: values[$0], endpoint: values[$0 + 1], fallback: values[$0 + 2],
          fingerprint: values[$0 + 3], certificate: values[$0 + 4])
      }
      peers = devices.map(\.name)
      if !peers.contains(peer) { peer = peers.first ?? "" }
      if peers.isEmpty {
        sendStatus = "No paired devices. Choose Add device to pair."
      }
    } catch {
      devices = []
      peers = []
      deviceError = error.localizedDescription
      peer = ""
      sendStatus = "Pairing setup needed: \(error.localizedDescription)"
    }
    restartReceiving()
  }

  private func startReceivingWhenReady() {
    guard receiveStartPending, !bridge.receiving else { return }
    receiveStartPending = false
    guard !peers.isEmpty else {
      receiveStatus = "Choose Add device to pair your first device."
      return
    }
    startReceiving()
  }

  func unpair(_ name: String) {
    do {
      try bridge.unpairPeer(name)
      poll()
      connectivityStatus = ""
      refreshPeers()
      lastResult = "Unpaired \(name) on this device. Pair again to reconnect."
      deviceStatus = lastResult
      deviceChecks.removeValue(forKey: name)
    } catch {
      lastResult = error.localizedDescription
      deviceError = error.localizedDescription
    }
  }

  func saveDevice(
    _ device: ManagedDevice, name: String, endpoint: String, fallback: String,
    certificate: String, fingerprint: String
  ) throws {
    defer { refreshPeers() }
    _ = try bridge.devices(
      command: "edit",
      arguments: [
        device.name, name, endpoint, fallback,
        certificate, fingerprint, device.fingerprint,
      ])
    poll()
    deviceChecks.removeValue(forKey: device.name)
    peer = name
    deviceStatus = "Saved \(name)."
    deviceError = ""
  }

  func exportDevices() {
    let panel = NSSavePanel()
    panel.title = "Export paired devices"
    panel.nameFieldStringValue = "beam-devices.beam-backup"
    guard panel.runModal() == .OK, let url = panel.url else { return }
    do {
      _ = try bridge.devices(command: "save", arguments: [url.path])
      deviceStatus = "Backup saved. It contains public certificates and connection settings."
      deviceError = ""
    } catch { deviceError = error.localizedDescription }
  }

  func restoreDevices(_ contents: String) throws {
    defer { refreshPeers() }
    let result = try bridge.devices(command: "import", arguments: [contents])
    poll()
    deviceStatus = "Restored \(result[1]) devices. Kept \(result[2]) existing devices."
    deviceError = ""
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

  func checkConnectivity() { checkDevice(peer) }

  func checkDevice(_ name: String) {
    do {
      try bridge.checkPeer(name)
      checkedDevice = name
      deviceChecks[name] = "Checking…"
      checking = bridge.checking
      connectivityStatus = "Checking \(name)…"
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
    if showPairing { pairingAction("snapshot") }
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
      deviceChecks[checkedDevice] = event.message
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
          " · \(percent)% · \(humanSize(Double(event.bytes))) / \(humanSize(Double(event.total))) · \(humanSize(event.bytesPerSecond))/s"
      }
    case .listening:
      status = "Listening for paired devices on port \(port)."
    case .completed:
      let description =
        event.sending
        ? "Sent and verified with \(event.message)" : "Received and verified \(event.filename)"
      status = "\(description) (\(humanSize(Double(event.bytes))))"
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
    if showPairing { pairingAction("cancel") }
    receiveStartPending = false
    bridge.shutdown()
    sending = false
    receiving = false
    checking = false
  }

  func openPairing() {
    pairingError = ""
    showPairing = true
    pairingAction("host", [Host.current().localizedName ?? "Beam Mac"])
    pairingAction("browse")
  }

  func closePairing() {
    pairingAction("cancel")
    showPairing = false
    refreshPeers()
  }

  func pairingAction(_ command: String, _ arguments: [String] = []) {
    do {
      let values = try bridge.pairing(command: command, arguments: arguments)
      guard values.count >= 8 else { return }
      pairingValues = values
      if values[0] != lastPairingState {
        lastPairingState = values[0]
        if values[0] == "awaiting approval" { pairingAlias = values[5] }
        if values[0] == "paired" { refreshPeers() }
      }
    } catch { pairingError = error.localizedDescription }
  }
}
