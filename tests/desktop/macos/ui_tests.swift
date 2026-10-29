import AppKit
import Darwin
import Foundation
import SwiftUI

private func require(_ condition: @autoclosure () -> Bool, _ message: String) throws {
  if !condition() {
    throw NSError(domain: "BeamTests", code: 1, userInfo: [NSLocalizedDescriptionKey: message])
  }
}

private func rejects(_ action: () throws -> Void) throws {
  do { try action() } catch { return }
  throw NSError(
    domain: "BeamTests", code: 1,
    userInfo: [NSLocalizedDescriptionKey: "Invalid operation accepted"])
}

private func runCLI(_ executable: String, _ arguments: [String]) throws -> Data {
  let process = Process()
  process.executableURL = URL(fileURLWithPath: executable)
  process.arguments = arguments
  let pipe = Pipe()
  process.standardOutput = pipe
  try process.run()
  let data = pipe.fileHandleForReading.readDataToEndOfFile()
  process.waitUntilExit()
  try require(
    process.terminationStatus == 0, "CLI fixture failed: \(arguments.joined(separator: " "))")
  return data
}

private func reservePort() throws -> Int {
  let descriptor = socket(AF_INET, SOCK_DGRAM, 0)
  try require(descriptor >= 0, "Cannot create test socket")
  defer { close(descriptor) }
  var address = sockaddr_in()
  address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
  address.sin_family = sa_family_t(AF_INET)
  address.sin_addr.s_addr = inet_addr("127.0.0.1")
  let status = withUnsafePointer(to: &address) { pointer in
    pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
      bind(descriptor, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
    }
  }
  try require(status == 0, "Cannot reserve test port")
  var length = socklen_t(MemoryLayout<sockaddr_in>.size)
  let result = withUnsafeMutablePointer(to: &address) { pointer in
    pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
      getsockname(descriptor, $0, &length)
    }
  }
  try require(result == 0, "Cannot read test port")
  return Int(UInt16(bigEndian: address.sin_port))
}

@MainActor
private func waitUntil(_ condition: () throws -> Bool) throws {
  let deadline = Date().addingTimeInterval(8)
  repeat {
    if try condition() { return }
    Thread.sleep(forTimeInterval: 0.01)
  } while Date() < deadline
  throw NSError(
    domain: "BeamTests", code: 1,
    userInfo: [NSLocalizedDescriptionKey: "Native desktop event timed out"])
}

@MainActor
private func renderDevices(_ model: DesktopModel) throws {
  guard let directory = ProcessInfo.processInfo.environment["BEAM_UI_PREVIEW_DIRECTORY"] else {
    return
  }
  try FileManager.default.createDirectory(atPath: directory, withIntermediateDirectories: true)
  for appearance in [NSAppearance.Name.aqua, .darkAqua] {
    let hosting = NSHostingView(
      rootView: DevicesView(model: model)
        .environment(\.colorScheme, appearance == .aqua ? .light : .dark)
        .background(Color(nsColor: .windowBackgroundColor)))
    hosting.appearance = NSAppearance(named: appearance)
    let window = NSWindow(
      contentRect: NSRect(x: 0, y: 0, width: 720, height: 940),
      styleMask: [.borderless], backing: .buffered, defer: false)
    window.appearance = NSAppearance(named: appearance)
    window.contentView = hosting
    hosting.frame = NSRect(x: 0, y: 0, width: 720, height: 940)
    RunLoop.current.run(until: Date().addingTimeInterval(0.2))
    hosting.layoutSubtreeIfNeeded()
    window.displayIfNeeded()
    guard let bitmap = hosting.bitmapImageRepForCachingDisplay(in: hosting.bounds) else {
      throw NSError(domain: "BeamTests", code: 1)
    }
    hosting.cacheDisplay(in: hosting.bounds, to: bitmap)
    guard let data = bitmap.representation(using: .png, properties: [:]) else {
      throw NSError(domain: "BeamTests", code: 2)
    }
    try data.write(
      to: URL(fileURLWithPath: directory).appendingPathComponent(
        "devices-\(appearance == .aqua ? "light" : "dark").png"))
  }
}

@MainActor
private func tests() throws {
  try require(humanSize(0) == "0 B", "Incorrect zero size")
  try require(humanSize(999) == "999 B", "Incorrect byte size")
  try require(humanSize(1000) == "1.00 KB", "Incorrect kilobyte size")
  try require(humanSize(1_048_576) == "1.05 MB", "Incorrect megabyte size")
  try require(humanSize(2_500_000_000) == "2.50 GB", "Incorrect gigabyte size")
  // Test the app's signal policy even if the test runner ignores broken pipes.
  signal(SIGPIPE, SIG_DFL)
  let manager = FileManager.default
  let suite = "beam-tests-\(UUID().uuidString)"
  let defaults = UserDefaults(suiteName: suite)!
  defer { defaults.removePersistentDomain(forName: suite) }
  let root = manager.temporaryDirectory.resolvingSymlinksInPath().appendingPathComponent(
    "beam-native-\(UUID().uuidString)")
  try manager.createDirectory(at: root, withIntermediateDirectories: false)
  defer { try? manager.removeItem(at: root) }
  let file = root.appendingPathComponent("hello.txt")
  try Data("native file\n".utf8).write(to: file)
  let link = root.appendingPathComponent("link.txt")
  try manager.createSymbolicLink(at: link, withDestinationURL: file)
  let selected = try BeamDesktopBridge.selectedFile(urls: [file])
  try require(selected == file.path, "Valid local file rejected")
  for urls in [
    [], [root], [link], [file, file], [URL(string: "https://example.com/file")!],
    [URL(string: "file://server/share/file.txt")!], [URL(string: "file:///tmp/a%00b.txt")!],
  ] {
    try rejects { _ = try BeamDesktopBridge.selectedFile(urls: urls) }
  }
  try rejects {
    try BeamDesktopBridge.validate(content: String(repeating: "界", count: 22000), kind: .text)
  }
  try BeamDesktopBridge.validate(content: "file:///etc/passwd", kind: .text)
  try rejects { try BeamDesktopBridge.validate(content: "text\0hidden", kind: .text) }
  try rejects { _ = try BeamDesktopBridge(stateDirectory: root.path, port: 0, advertise: false) }
  try rejects {
    _ = try BeamDesktopBridge(stateDirectory: root.path, port: 65536, advertise: false)
  }
  for arguments in [
    ["--listen", "0"], ["--listen", "65536"], ["--listen", "-1"], ["--listen", "abc"], ["--output"],
    ["unexpected"],
  ] {
    try rejects { _ = try LaunchOptions(arguments: arguments) }
  }
  let options = try LaunchOptions(arguments: [
    "--state-dir", root.path, "--output", root.path, "--listen", "12345", "--no-discovery",
  ])
  let configFile = root.appendingPathComponent("config")
  try Data("listen_port=5678\ntimeout_seconds=12\ndiscovery=false\n".utf8).write(to: configFile)
  let launchConfig = try BeamDesktopBridge.configuration(atPath: configFile.path)
  let configured = try LaunchOptions(arguments: ["--listen", "12345"], configuration: launchConfig)
  try require(
    configured.port == 12345 && configured.timeout == 12000 && !configured.advertise,
    "Config defaults or CLI precedence lost")
  try require(
    options.port == 12345 && !options.advertise && options.output == root.path,
    "Launch options lost")

  guard let freshCanonical = realpath(root.path, nil) else {
    throw NSError(domain: "BeamTests", code: 1)
  }
  let freshDirectory = String(cString: freshCanonical) + "/missing"
  free(freshCanonical)
  let missing = try BeamDesktopBridge(
    stateDirectory: freshDirectory, port: 4269, advertise: false)
  let empty = DesktopModel(bridge: missing, port: 4269, defaults: defaults)
  let freshIdentity = try missing.devices(command: "identity", arguments: [])
  let sameIdentity = try missing.devices(command: "identity", arguments: [])
  try require(
    freshIdentity.count == 3 && freshIdentity == sameIdentity,
    "First-use device management replaced or omitted identity")
  let emptyBackupPath = root.appendingPathComponent("empty.beam-backup").path
  _ = try missing.devices(command: "save", arguments: [emptyBackupPath])
  let emptyBackup = try missing.devices(command: "read", arguments: [emptyBackupPath])[0]
  let emptyPreview = try missing.devices(command: "preview", arguments: [emptyBackup])
  try require(
    emptyPreview[1] == "0" && emptyPreview[2] == "0", "Fresh identity cannot preview backups")
  let pasteboard = NSPasteboard.withUniqueName()
  defer { pasteboard.releaseGlobally() }
  pasteboard.setString("private snapshot\r\n世界", forType: .string)
  try require(
    empty.content.isEmpty && empty.peers.isEmpty && !empty.sending && !empty.receiving,
    "Startup loaded content or started an unpaired transfer")
  empty.loadClipboard(pasteboard)
  try require(
    empty.content == "private snapshot\r\n世界" && empty.kind == .text, "Clipboard bytes changed")
  pasteboard.clearContents()
  pasteboard.setString("later clipboard", forType: .string)
  empty.poll()
  try require(empty.content == "private snapshot\r\n世界", "Clipboard snapshot was watched")
  try require(pasteboard.string(forType: .string) == "later clipboard", "App wrote the clipboard")
  pasteboard.clearContents()
  pasteboard.setString(String(repeating: "x", count: 65537), forType: .string)
  empty.loadClipboard(pasteboard)
  try require(empty.content == "private snapshot\r\n世界", "Invalid clipboard replaced selection")
  try require(empty.selectFiles([file]) && !empty.sending, "Drop sent automatically")
  empty.send()
  try require(!empty.sending, "Unpaired send was allowed")
  // Check AppKit's URL pasteboard adapter and close-to-menu-bar policy without
  // opening a real window, requesting notification access, or touching clipboard.
  _ = NSApplication.shared
  let hosting = FileDropHostingView(model: empty)
  pasteboard.clearContents()
  pasteboard.writeObjects([file as NSURL])
  try require(hosting.droppedURLs(pasteboard) == [file], "Native file drop lost its URL")
  try require(
    FileDrop.operation(pasteboard: pasteboard, sourceOperations: .move).isEmpty,
    "Move-only drop accepted")
  try require(
    FileDrop.operation(pasteboard: pasteboard, sourceOperations: [.copy, .move]) == .copy,
    "Drop did not negotiate copy")
  let editor = ReviewTextView(frame: .zero)
  editor.model = empty
  editor.string = "reviewed text"
  empty.kind = .text
  try require(
    editor.selectDrop(pasteboard) && empty.kind == .file, "Editor bypassed file selection")
  try require(
    editor.string == "reviewed text" && !empty.sending, "Editor inserted or sent a dropped URL")
  pasteboard.clearContents()
  pasteboard.writeObjects([file as NSURL, link as NSURL])
  try require(hosting.droppedURLs(pasteboard).isEmpty, "Native drop accepted multiple items")
  try require(!editor.selectDrop(pasteboard), "Editor accepted multiple files")
  let delegate = AppDelegate(model: empty, notificationDefaults: defaults)
  try require(!delegate.notificationsEnabled, "Notifications should require explicit opt-in")
  defaults.set(true, forKey: "transferNotificationsEnabled")
  let optedIn = AppDelegate(model: empty, notificationDefaults: defaults)
  try require(optedIn.notificationsEnabled, "Notification opt-in was not remembered")
  let window = NSWindow(contentRect: .zero, styleMask: [.titled], backing: .buffered, defer: true)
  try require(!delegate.windowShouldClose(window), "Closing should hide the native window")
  try require(
    !delegate.applicationShouldTerminateAfterLastWindowClosed(NSApplication.shared),
    "Closing should not quit Beam")
  delegate.applicationWillTerminate(Notification(name: NSApplication.willTerminateNotification))
  empty.shutdown()
  empty.shutdown()
  try rejects { _ = try missing.events() }

  // Exercise the Objective-C++ boundary and Swift event presentation with
  // real paired transfers, rather than duplicating the core test fixtures.
  try require(CommandLine.arguments.count == 2, "Missing CLI fixture executable")
  let executable = CommandLine.arguments[1]
  // Foundation resolves temporary URLs to /var on macOS; the identity store
  // deliberately rejects that symlink ancestor. Preserve POSIX canonical paths.
  guard let canonical = realpath(root.path, nil) else {
    throw NSError(domain: "BeamTests", code: 1)
  }
  let stateRoot = String(cString: canonical)
  free(canonical)
  var identities: [String: String] = [:]
  for name in ["left", "right"] {
    let directory = stateRoot + "/" + name
    let data = try runCLI(executable, ["identity", "init", "--state-dir", directory])
    let text = String(decoding: data, as: UTF8.self)
    identities[name] = text.components(separatedBy: "\n").first(where: {
      $0.hasPrefix("Fingerprint: ")
    })?.replacingOccurrences(of: "Fingerprint: ", with: "")
    let certificate = try runCLI(executable, ["identity", "export", "--state-dir", directory])
    try certificate.write(to: root.appendingPathComponent("\(name).pem"))
  }
  let port = try reservePort()
  let unpairedBridge = try BeamDesktopBridge(
    stateDirectory: stateRoot + "/right", port: UInt(port), advertise: false)
  let unpaired = DesktopModel(
    bridge: unpairedBridge, port: port, output: stateRoot, defaults: defaults)
  try require(
    !unpaired.receiving && unpaired.receiveStatus.contains("Add device"),
    "Unpaired launch did not explain setup")
  unpaired.shutdown()

  // Exercise the actual Objective-C++ value bridge for both ends of pairing.
  let pairingLeft = try BeamDesktopBridge(
    stateDirectory: stateRoot + "/network-left", port: 4269, advertise: false)
  let pairingRight = try BeamDesktopBridge(
    stateDirectory: stateRoot + "/network-right", port: 4270, advertise: false)
  defer {
    pairingLeft.shutdown()
    pairingRight.shutdown()
  }
  _ = try pairingLeft.pairing(command: "host", arguments: ["left"])
  var invitation: [String] = []
  try waitUntil {
    invitation = try pairingLeft.pairing(command: "snapshot", arguments: [])
    return invitation[0] == "hosting"
  }
  try require(
    invitation.count == 7 && !invitation[6].isEmpty && invitation[2].hasPrefix("beam-pair:2:"),
    "Pairing bridge lost QR or version 2 invitation")
  _ = try pairingRight.pairing(command: "host", arguments: ["right"])
  _ = try pairingRight.pairing(command: "join", arguments: [invitation[2]])
  var leftApproval: [String] = []
  var rightApproval: [String] = []
  try waitUntil {
    leftApproval = try pairingLeft.pairing(command: "snapshot", arguments: [])
    rightApproval = try pairingRight.pairing(command: "snapshot", arguments: [])
    return leftApproval[0] == "awaiting approval" && rightApproval[0] == "awaiting approval"
  }
  let beforeApproval = try pairingLeft.peers()
  try require(beforeApproval.isEmpty, "Pairing bridge granted trust before approval")
  _ = try pairingLeft.pairing(command: "approve", arguments: [leftApproval[4]])
  _ = try pairingRight.pairing(command: "approve", arguments: [rightApproval[4]])
  try waitUntil {
    let left = try pairingLeft.pairing(command: "snapshot", arguments: [])
    let right = try pairingRight.pairing(command: "snapshot", arguments: [])
    return left[0] == "paired" && right[0] == "paired"
  }
  let networkPeers = try pairingLeft.peers()
  try require(networkPeers == ["right"], "Pairing bridge did not refresh stored devices")
  for (name, peer) in [("left", "right"), ("right", "left")] {
    guard let fingerprint = identities[peer] else { throw NSError(domain: "BeamTests", code: 1) }
    _ = try runCLI(
      executable,
      [
        "pair", peer, "--state-dir", stateRoot + "/" + name, "--cert",
        root.appendingPathComponent("\(peer).pem").path, "--fingerprint", fingerprint, "--endpoint",
        "127.0.0.1:\(port)",
      ])
  }
  let receiver = try BeamDesktopBridge(
    stateDirectory: stateRoot + "/right", port: UInt(port), advertise: false)
  var sendPort = try reservePort()
  while sendPort == port { sendPort = try reservePort() }
  let sender = try BeamDesktopBridge(
    stateDirectory: stateRoot + "/left", port: UInt(sendPort), advertise: false)
  defer {
    sender.shutdown()
    receiver.shutdown()
  }
  let output = root.appendingPathComponent("received")
  try manager.createDirectory(at: output, withIntermediateDirectories: false)
  let receive = DesktopModel(bridge: receiver, port: port, defaults: defaults)
  let send = DesktopModel(bridge: sender, port: sendPort, output: stateRoot, defaults: defaults)
  try require(send.peers == ["right"] && send.peer == "right", "Trusted peers lost at bridge")
  try require(
    receive.output
      == manager.homeDirectoryForCurrentUser.appendingPathComponent("Downloads").path
      && receive.receiving,
    "First launch did not start receiving in Downloads: \(receive.output); \(receive.receiveStatus)"
  )
  try waitUntil {
    receive.poll()
    return receive.receiveStatus.hasPrefix("Listening")
  }
  receive.selectReceiveFolder(output.path)
  try waitUntil {
    receive.poll()
    return receive.receiveStatus.hasPrefix("Listening") && receive.receiving
  }
  var notifications: [String] = []
  send.onNotification = { notifications.append($0) }
  send.kind = .text
  send.content = "native clipboard\r\n世界"
  send.send()
  try waitUntil {
    send.poll()
    receive.poll()
    return send.lastResult.hasPrefix("Sent and verified")
      && receive.lastResult.hasPrefix("Received and verified")
  }
  try require(receive.receivedText == send.content, "Native text transfer changed bytes")
  let textOutput = try manager.contentsOfDirectory(atPath: output.path)
  try require(textOutput.isEmpty, "Text created a file")
  receive.copyText(receive.receivedText ?? "", to: pasteboard)
  try require(pasteboard.string(forType: .string) == send.content, "Explicit copy changed text")
  try require(
    notifications.count == 1 && !notifications[0].contains("clipboard"),
    "Notification exposed content or preceded verification")
  // Reuse the active receiver, send a reviewed URL, then a selected file.
  for (kind, value) in [(ContentSelection.text, "https://example.com/native"), (.file, "")] {
    try waitUntil {
      receive.poll()
      return receive.receiveStatus.hasPrefix("Listening")
    }
    send.kind = kind
    send.content = value
    send.file = file.path
    send.lastResult = ""
    send.send()
    try waitUntil {
      send.poll()
      receive.poll()
      return send.lastResult.hasPrefix("Sent and verified")
    }
  }
  try waitUntil {
    receive.poll()
    return receive.lastResult.contains("hello.txt")
  }
  let saved = try Data(contentsOf: output.appendingPathComponent("hello.txt"))
  try require(saved == Data("native file\n".utf8), "Native file transfer changed bytes")
  try require(receive.receivedText == "https://example.com/native", "URL text changed bytes")
  receive.refreshPeers()
  try waitUntil {
    receive.poll()
    return receive.receiveStatus.hasPrefix("Listening") && receive.receiving
  }
  send.checkConnectivity()
  try waitUntil {
    send.poll()
    receive.poll()
    return send.connectivityStatus.contains("is reachable") && !send.checking
  }
  receive.shutdown()
  try require(!receiver.receiving, "Shutdown left a receiver worker running")
  send.checkConnectivity()
  // Darwin can deliver this process-wide when MsQuic's UDP send fails.
  raise(SIGPIPE)
  try waitUntil {
    send.poll()
    return send.connectivityStatus.contains("is unavailable") && !send.checking
  }

  let restoredBridge = try BeamDesktopBridge(
    stateDirectory: stateRoot + "/right", port: UInt(port), advertise: false)
  let restored = DesktopModel(bridge: restoredBridge, port: port, defaults: defaults)
  try require(restored.output == output.path && restored.receiving, "Launch did not reuse folder")
  try waitUntil {
    restored.poll()
    return restored.receiveStatus.hasPrefix("Listening")
  }
  send.checkConnectivity()
  try waitUntil {
    send.poll()
    restored.poll()
    return send.connectivityStatus.contains("is reachable") && !send.checking
  }
  restored.shutdown()

  let invalidBridge = try BeamDesktopBridge(
    stateDirectory: stateRoot + "/right", port: UInt(port), advertise: false)
  let invalid = DesktopModel(
    bridge: invalidBridge, port: port, output: stateRoot + "/missing-output", defaults: defaults)
  try waitUntil {
    invalid.poll()
    return !invalid.receiving && invalid.receiveStatus.hasPrefix("Transfer stopped:")
  }
  invalid.refreshPeers()
  try waitUntil {
    invalid.poll()
    return !invalid.receiving && invalid.receiveStatus.hasPrefix("Transfer stopped:")
  }
  try require(invalid.output.hasSuffix("missing-output"), "Explicit output lost to saved folder")
  invalid.selectReceiveFolder(output.path)
  try waitUntil {
    invalid.poll()
    return invalid.receiving && invalid.receiveStatus.hasPrefix("Listening")
  }
  invalid.unpair("missing")
  try require(invalid.receiving && invalid.peers == ["left"], "Invalid unpair stopped reception")
  try renderDevices(invalid)
  let deviceBackupPath = root.appendingPathComponent("devices.beam-backup").path
  _ = try invalidBridge.devices(command: "save", arguments: [deviceBackupPath])
  let deviceBackup = try invalidBridge.devices(command: "read", arguments: [deviceBackupPath])[0]
  let preview = try invalidBridge.devices(command: "preview", arguments: [deviceBackup])
  try require(preview[1] == "0" && preview[2] == "1", "Backup preview lost existing devices")
  let device = invalid.devices[0]
  try invalid.saveDevice(
    device, name: "work-mac", endpoint: "127.0.0.1:4269", fallback: "",
    certificate: "", fingerprint: "")
  try waitUntil {
    invalid.poll()
    return invalid.receiving && invalid.peers == ["work-mac"]
  }
  try require(invalid.peer == "work-mac", "Rename lost selected device")
  invalid.unpair("work-mac")
  try invalid.restoreDevices(deviceBackup)
  try waitUntil {
    invalid.poll()
    return invalid.receiving && invalid.peers == ["left"]
  }
  invalid.unpair("left")
  try waitUntil {
    invalid.poll()
    return !invalid.receiving && invalid.receiveStatus.contains("Add device")
  }
  try require(invalid.peer.isEmpty && invalid.peers.isEmpty, "Unpair retained the device picker")
  try require(invalid.lastResult.contains("Unpaired left"), "Unpair omitted confirmation status")
  let remainingPeers = try invalidBridge.peers()
  try require(remainingPeers.isEmpty, "Native unpair did not persist trust removal")
  invalid.shutdown()
}

@main
struct NativeDesktopTests {
  @MainActor static func main() {
    do {
      try tests()
      print("Native desktop UI/bridge tests passed")
    } catch {
      FileHandle.standardError.write(Data("\(error.localizedDescription)\n".utf8))
      exit(1)
    }
  }
}
