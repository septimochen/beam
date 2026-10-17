import AppKit

@main
struct BeamApplication {
  @MainActor static func main() {
    do {
      let arguments = Array(CommandLine.arguments.dropFirst())
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
}
