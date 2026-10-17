import AppKit
import Darwin
import Foundation

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
private func tests() throws {
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
  try rejects { try BeamDesktopBridge.validate(content: "file:///etc/passwd", kind: .url) }
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
  try require(
    options.port == 12345 && !options.advertise && options.output == root.path,
    "Launch options lost")

  let missing = try BeamDesktopBridge(
    stateDirectory: root.appendingPathComponent("missing").path, port: 4269, advertise: false)
  let empty = DesktopModel(bridge: missing, port: 4269, defaults: defaults)
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
  let delegate = AppDelegate(model: empty)
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
    !unpaired.receiving && unpaired.receiveStatus.contains("pair a device"),
    "Unpaired launch did not explain setup")
  unpaired.shutdown()
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
  let sender = try BeamDesktopBridge(
    stateDirectory: stateRoot + "/left", port: UInt(port), advertise: false)
  defer {
    sender.shutdown()
    receiver.shutdown()
  }
  let output = root.appendingPathComponent("received")
  try manager.createDirectory(at: output, withIntermediateDirectories: false)
  let receive = DesktopModel(bridge: receiver, port: port, defaults: defaults)
  let send = DesktopModel(bridge: sender, port: port, defaults: defaults)
  try require(send.peers == ["right"] && send.peer == "right", "Trusted peers lost at bridge")
  try require(
    !receive.receiving && receive.receiveStatus.contains("Choose a receive folder"),
    "First launch did not request a receive folder")
  receive.selectReceiveFolder(output.path)
  try require(receive.receiving, "Choosing the first receive folder did not start reception")
  try waitUntil {
    receive.poll()
    return receive.receiveStatus.hasPrefix("Listening")
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
  let received = try manager.contentsOfDirectory(at: output, includingPropertiesForKeys: nil).first
  { $0.lastPathComponent.hasPrefix("text-") }
  guard let received else { throw NSError(domain: "BeamTests", code: 1) }
  let payload = try Data(contentsOf: received)
  try require(payload == Data(send.content.utf8), "Native text transfer changed bytes")
  try require(
    notifications.count == 1 && !notifications[0].contains("clipboard"),
    "Notification exposed content or preceded verification")
  // Reuse the active receiver, send a reviewed URL, then a selected file.
  for (kind, value) in [(ContentSelection.url, "https://example.com/native"), (.file, "")] {
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
  let urlFile = try manager.contentsOfDirectory(at: output, includingPropertiesForKeys: nil).first {
    $0.lastPathComponent.hasPrefix("url-")
  }
  guard let urlFile else { throw NSError(domain: "BeamTests", code: 1) }
  let savedURL = try Data(contentsOf: urlFile)
  try require(
    savedURL == Data("https://example.com/native".utf8), "Native URL transfer changed bytes")
  receive.stopReceiving()
  try waitUntil {
    receive.poll()
    return !receive.receiving
  }
  receive.refreshPeers()
  receive.poll()
  try require(!receive.receiving, "Refresh undid explicit Stop")
  receive.toggleReceiving()
  try waitUntil {
    receive.poll()
    return receive.receiveStatus.hasPrefix("Listening")
  }
  receive.shutdown()
  try require(!receiver.receiving, "Shutdown left a receiver worker running")

  let restoredBridge = try BeamDesktopBridge(
    stateDirectory: stateRoot + "/right", port: UInt(port), advertise: false)
  let restored = DesktopModel(bridge: restoredBridge, port: port, defaults: defaults)
  try require(restored.output == output.path && restored.receiving, "Launch did not reuse folder")
  try waitUntil {
    restored.poll()
    return restored.receiveStatus.hasPrefix("Listening")
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
  try require(!invalid.receiving, "Invalid folder caused an automatic restart")
  try require(invalid.output.hasSuffix("missing-output"), "Explicit output lost to saved folder")
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
