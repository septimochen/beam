import AppKit
import SwiftUI

struct DevicesView: View {
  @ObservedObject var model: DesktopModel
  @State private var search = ""
  @State private var editing: ManagedDevice?
  @State private var removing: ManagedDevice?
  @State private var showUnpair = false
  @State private var backup = ""
  @State private var preview: [String] = []
  @State private var showRestore = false
  @State private var showIdentity = false
  @State private var identity: [String] = []

  private var filtered: [ManagedDevice] {
    model.devices.filter {
      search.isEmpty || $0.name.localizedCaseInsensitiveContains(search)
        || $0.endpoint.localizedCaseInsensitiveContains(search)
        || $0.fallback.localizedCaseInsensitiveContains(search)
    }
  }

  var body: some View {
    ScrollView {
      VStack(alignment: .leading, spacing: 22) {
        HStack(alignment: .top) {
          VStack(alignment: .leading, spacing: 6) {
            Text("Your devices").font(.largeTitle.bold())
            Text("A trusted connection, wherever you work.").foregroundStyle(.secondary)
          }
          Spacer()
          Button(action: model.openPairing) { Label("Add device", systemImage: "plus") }
            .buttonStyle(.borderedProminent).disabled(model.sending || model.checking)
        }
        HStack {
          Label("\(model.devices.count) paired", systemImage: "checkmark.shield")
            .font(.callout).foregroundStyle(.secondary)
          Spacer()
          Button("My identity", systemImage: "person.crop.circle") {
            perform {
              identity = try model.bridge.devices(command: "identity", arguments: [])
              showIdentity = true
            }
          }
          Button("Refresh", systemImage: "arrow.clockwise") {
            model.deviceError = ""
            model.refreshPeers()
          }
        }
        HStack {
          Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
          TextField("Search devices or addresses", text: $search).textFieldStyle(.plain)
          if !search.isEmpty { Button("Clear") { search = "" }.buttonStyle(.plain) }
        }.padding(12).background(.quaternary.opacity(0.5), in: RoundedRectangle(cornerRadius: 12))
        if !model.deviceError.isEmpty {
          Label(model.deviceError, systemImage: "exclamationmark.circle")
            .foregroundStyle(.red).textSelection(.enabled)
        }
        if !model.deviceStatus.isEmpty {
          Text(model.deviceStatus).font(.callout).foregroundStyle(.secondary).textSelection(
            .enabled)
        }
        if filtered.isEmpty {
          VStack(spacing: 12) {
            Image(
              systemName: model.devices.isEmpty ? "laptopcomputer.and.iphone" : "magnifyingglass"
            )
            .font(.system(size: 44)).foregroundStyle(.tint)
            Text(model.devices.isEmpty ? "Make your first connection" : "No matching devices")
              .font(.title3.bold())
            Text(
              model.devices.isEmpty
                ? "Add a device with a QR code, a nearby PIN, or an invitation."
                : "Try another name or IP address."
            ).foregroundStyle(.secondary)
            if model.devices.isEmpty { Button("Add device", action: model.openPairing) }
          }.frame(maxWidth: .infinity).padding(.vertical, 28)
        }
        ForEach(filtered) { device in
          VStack(alignment: .leading, spacing: 14) {
            HStack(spacing: 14) {
              Image(systemName: "desktopcomputer")
                .font(.title2).foregroundStyle(.tint)
                .frame(width: 48, height: 48)
                .background(.tint.opacity(0.1), in: RoundedRectangle(cornerRadius: 14))
              VStack(alignment: .leading, spacing: 5) {
                Text(device.name).font(.headline)
                Text(device.endpoint.isEmpty ? "No direct address saved" : device.endpoint)
                  .font(.callout.monospaced()).foregroundStyle(.secondary).textSelection(.enabled)
                if !device.fallback.isEmpty {
                  Text("Fallback · \(device.fallback)").font(.caption.monospaced())
                    .foregroundStyle(.secondary).textSelection(.enabled)
                }
              }
              Spacer()
              Button("Edit", systemImage: "slider.horizontal.3") { editing = device }
            }
            Divider()
            HStack(alignment: .top) {
              VStack(alignment: .leading, spacing: 4) {
                Label("Certificate paired", systemImage: "lock.shield")
                  .font(.caption).foregroundStyle(.secondary)
                Text(model.deviceChecks[device.name] ?? "Connection not checked")
                  .font(.callout).textSelection(.enabled)
              }
              Spacer()
              Button("Check connection") { model.checkDevice(device.name) }.disabled(model.checking)
              Menu {
                Button("View certificate") { editing = device }
                Button("Unpair device…", role: .destructive) {
                  removing = device
                  showUnpair = true
                }
              } label: {
                Image(systemName: "ellipsis")
              }.menuStyle(.borderlessButton).frame(width: 26)
                .accessibilityLabel("More actions for \(device.name)")
            }
          }.padding(18).background(.background, in: RoundedRectangle(cornerRadius: 18))
            .overlay(RoundedRectangle(cornerRadius: 18).stroke(.quaternary))
        }
        VStack(alignment: .leading, spacing: 12) {
          Label("Backup & restore", systemImage: "externaldrive").font(.headline)
          Text(
            "Keep a copy of your devices, addresses, and public certificates. Your private key stays on this device."
          )
          .font(.callout).foregroundStyle(.secondary)
          HStack {
            Button(
              "Export backup…", systemImage: "square.and.arrow.up", action: model.exportDevices)
            Button("Import backup…", systemImage: "square.and.arrow.down", action: chooseBackup)
          }
        }.padding(18).frame(maxWidth: .infinity, alignment: .leading)
          .background(.quaternary.opacity(0.4), in: RoundedRectangle(cornerRadius: 18))
      }.padding(28)
    }
    .sheet(item: $editing) { DeviceEditor(model: model, device: $0) }
    .confirmationDialog(
      "Unpair \(removing?.name ?? "device")?", isPresented: $showUnpair,
      titleVisibility: .visible
    ) {
      Button("Unpair", role: .destructive) {
        if let device = removing { model.unpair(device.name) }
        removing = nil
      }
    } message: {
      Text(
        "Active transfers and checks will stop. This removes local trust. Pair again to reconnect.")
    }
    .sheet(isPresented: $showRestore) {
      VStack(alignment: .leading, spacing: 16) {
        Text("Restore your devices").font(.title2.bold())
        Text("\(preview[1]) devices to add · \(preview[2]) already paired")
        Text("Only restore a backup you trust. Existing devices keep their current settings.")
          .foregroundStyle(.secondary)
        if preview[3] == "0" {
          Label(
            "This backup came from another identity. Pair this device on the other devices before transferring.",
            systemImage: "exclamationmark.triangle"
          ).foregroundStyle(.orange)
        }
        ScrollView {
          VStack(alignment: .leading, spacing: 14) {
            ForEach(Array(stride(from: 4, to: preview.count, by: 4)), id: \.self) { i in
              VStack(alignment: .leading, spacing: 5) {
                Text(preview[i]).font(.headline)
                Text(preview[i + 1].isEmpty ? "No direct address" : preview[i + 1])
                if !preview[i + 2].isEmpty { Text("Fallback: \(preview[i + 2])") }
                Text(preview[i + 3]).font(.caption.monospaced()).textSelection(.enabled)
              }
              Divider()
            }
          }
        }.frame(maxHeight: 300)
        Text(model.deviceError).foregroundStyle(.red)
        HStack {
          Button("Cancel") {
            showRestore = false
            backup = ""
          }
          Spacer()
          Button("Restore devices") {
            perform {
              try model.restoreDevices(backup)
              showRestore = false
              backup = ""
            }
          }.buttonStyle(.borderedProminent)
        }
      }.padding(28).frame(width: 530)
    }
    .sheet(isPresented: $showIdentity) {
      VStack(alignment: .leading, spacing: 16) {
        Label("This device’s identity", systemImage: "checkmark.shield").font(.title2.bold())
        Text("SHA-256 fingerprint").font(.headline)
        Text(identity.first ?? "").font(.body.monospaced()).textSelection(.enabled)
        Text("Compare all 64 characters when manually pairing. Share only your public certificate.")
          .foregroundStyle(.secondary)
        HStack {
          Button("Copy public certificate") {
            if identity.count > 2 { model.copyText(identity[2]) }
          }
          Spacer()
          Button("Done") { showIdentity = false }
        }
      }.padding(28).frame(width: 530)
    }
  }

  private func perform(_ action: () throws -> Void) {
    do {
      model.deviceError = ""
      try action()
    } catch { model.deviceError = error.localizedDescription }
  }

  private func chooseBackup() {
    let panel = NSOpenPanel()
    panel.title = "Import device backup"
    panel.allowsMultipleSelection = false
    panel.canChooseDirectories = false
    guard panel.runModal() == .OK, let url = panel.url else { return }
    perform {
      backup = try model.bridge.devices(command: "read", arguments: [url.path])[0]
      preview = try model.bridge.devices(command: "preview", arguments: [backup])
      showRestore = true
    }
  }
}

private struct DeviceEditor: View {
  @ObservedObject var model: DesktopModel
  let device: ManagedDevice
  @Environment(\.dismiss) private var dismiss
  @State private var name: String
  @State private var endpoint: String
  @State private var fallback: String
  @State private var replaceCertificate = false
  @State private var certificate = ""
  @State private var fingerprint = ""
  @State private var error = ""

  init(model: DesktopModel, device: ManagedDevice) {
    self.model = model
    self.device = device
    _name = State(initialValue: device.name)
    _endpoint = State(initialValue: device.endpoint)
    _fallback = State(initialValue: device.fallback)
  }

  var body: some View {
    VStack(alignment: .leading, spacing: 18) {
      HStack {
        Text("Edit device").font(.title2.bold())
        Spacer()
        Text(device.name).foregroundStyle(.secondary)
      }
      ScrollView {
        Form {
          Section("Connection") {
            TextField("Device name", text: $name)
            Text("1–63 lowercase letters, numbers, - or _; start with a letter.")
              .font(.caption).foregroundStyle(.secondary)
            TextField("IP address & port", text: $endpoint, prompt: Text("192.168.1.20:4269"))
            TextField("Fallback address (optional)", text: $fallback)
            Text(
              "IPv6: [address]:port. Leave an address empty to remove it. Saving stops active transfers and checks."
            )
            .font(.caption).foregroundStyle(.secondary)
          }
          Section("Certificate") {
            Text("Current SHA-256 fingerprint").font(.caption).foregroundStyle(.secondary)
            Text(device.fingerprint).font(.caption.monospaced()).textSelection(.enabled)
            DisclosureGroup("View public certificate") {
              Text(device.certificate).font(.caption.monospaced()).textSelection(.enabled)
            }
            Toggle("Replace certificate", isOn: $replaceCertificate)
            if replaceCertificate {
              Text(
                "This changes which identity you trust. Verify the full new fingerprint on the other device. Active transfers and checks will stop when saved."
              )
              .font(.callout).foregroundStyle(.secondary)
              Button("Choose public certificate…") {
                let panel = NSOpenPanel()
                panel.allowsMultipleSelection = false
                if panel.runModal() == .OK, let url = panel.url {
                  do {
                    certificate = try model.bridge.devices(command: "read", arguments: [url.path])[
                      0]
                  } catch { self.error = error.localizedDescription }
                }
              }
              TextEditor(text: $certificate).font(.caption.monospaced()).frame(height: 95)
                .accessibilityLabel("Replacement public certificate PEM")
              TextField("Verified SHA-256 (all 64 characters)", text: $fingerprint)
            }
          }
        }.formStyle(.grouped)
      }
      if !error.isEmpty { Text(error).foregroundStyle(.red).textSelection(.enabled) }
      HStack {
        Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
        Spacer()
        Button(replaceCertificate ? "Save & trust certificate" : "Save changes") {
          do {
            try model.saveDevice(
              device, name: name, endpoint: endpoint, fallback: fallback,
              certificate: replaceCertificate ? certificate : "",
              fingerprint: replaceCertificate ? fingerprint : "")
            dismiss()
          } catch { self.error = error.localizedDescription }
        }.buttonStyle(.borderedProminent).keyboardShortcut(.defaultAction)
          .disabled(
            name.isEmpty || (replaceCertificate && (certificate.isEmpty || fingerprint.count != 64))
          )
      }
    }.padding(24).frame(width: 580, height: 610)
  }
}
