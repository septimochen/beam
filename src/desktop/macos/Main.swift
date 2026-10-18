import AppKit
import UserNotifications

@main
struct BeamApplication {
  @MainActor static func main() async {
    do {
      let arguments = Array(CommandLine.arguments.dropFirst())
      if arguments == ["--clear-notifications"] {
        try await clearNotifications()
        return
      }
      let configuration =
        arguments.contains("--help") || arguments.contains("--version")
        ? [:] : try BeamDesktopBridge.configuration(atPath: nil)
      let options = try LaunchOptions(arguments: arguments, configuration: configuration)
      if options.help {
        print(LaunchOptions.usage)
        return
      }
      let bridge = try BeamDesktopBridge(
        stateDirectory: options.stateDirectory, port: UInt(options.port),
        advertise: options.advertise, timeout: UInt(options.timeout))
      if options.version {
        print("beam-desktop \(bridge.version)")
        bridge.shutdown()
        return
      }
      let model = DesktopModel(bridge: bridge, port: options.port, output: options.output)
      let delegate = AppDelegate(model: model)
      let application = NSApplication.shared
      application.setActivationPolicy(.regular)
      application.delegate = delegate
      withExtendedLifetime(delegate) { application.run() }
      model.shutdown()
    } catch {
      let message = "beam-desktop: \(error.localizedDescription)\n"
      FileHandle.standardError.write(Data(message.utf8))
      exit(1)
    }
  }

  private static func clearNotifications() async throws {
    guard let identifier = Bundle.main.bundleIdentifier,
      identifier == "org.beam.desktop" || identifier.hasPrefix("org.beam.desktop.")
    else {
      throw NSError(
        domain: "Beam", code: 1,
        userInfo: [NSLocalizedDescriptionKey: "Notification cleanup requires a Beam app bundle"])
    }
    // Keep a failed notification daemon from blocking uninstall indefinitely.
    let timeout = Task {
      try await Task.sleep(for: .seconds(10))
      FileHandle.standardError.write(Data("Notification cleanup timed out\n".utf8))
      exit(1)
    }
    defer { timeout.cancel() }
    let center = UNUserNotificationCenter.current()
    center.removeAllPendingNotificationRequests()
    center.removeAllDeliveredNotifications()
    let pending = await center.pendingNotificationRequests()
    let delivered = await center.deliveredNotifications()
    guard pending.isEmpty && delivered.isEmpty else {
      throw NSError(
        domain: "Beam", code: 1,
        userInfo: [NSLocalizedDescriptionKey: "Some Beam notifications remain; retry cleanup"])
    }
    print(
      "Cleared pending and delivered notifications for \(identifier). Authorization is unchanged.")
  }
}
