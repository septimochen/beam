package org.beam.android

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Handler
import android.os.Looper

/** Untrusted LAN hints only. No PIN, invitation, certificate or key is advertised. */
@Suppress("DEPRECATION")
internal class PairingDiscovery(
  context: Context,
  private val changed: (List<Pair<String, String>>) -> Unit,
) {
  private val manager = context.getSystemService(NsdManager::class.java)
  private val main = Handler(Looper.getMainLooper())
  private var generation = 0
  private var discovery: NsdManager.DiscoveryListener? = null
  private var registration: NsdManager.RegistrationListener? = null
  private var ownName = ""
  private val devices = linkedMapOf<String, Pair<String, String>>()
  private var advertisedPort = 0
  private val observed = mutableSetOf<String>()
  private var resolving = 0

  fun browse() {
    stopBrowsing()
    val current = generation
    val listener =
      object : NsdManager.DiscoveryListener {
        override fun onDiscoveryStarted(type: String) {}

        override fun onDiscoveryStopped(type: String) {}

        override fun onStartDiscoveryFailed(type: String, code: Int) {
          main.post { if (current == generation) changed(emptyList()) }
        }

        override fun onStopDiscoveryFailed(type: String, code: Int) {}

        override fun onServiceLost(info: NsdServiceInfo) {
          main.post {
            if (current == generation) {
              observed.remove(info.serviceName)
              devices.remove(info.serviceName)
              changed(devices.values.toList())
            }
          }
        }

        override fun onServiceFound(info: NsdServiceInfo) {
          if (
            info.serviceType.trimEnd('.') != "_beam-pair._tcp" ||
              info.serviceName.isEmpty() ||
              info.serviceName.length > 63 ||
              info.serviceName.any { it.code !in 32..126 }
          )
            return
          main.post {
            if (
              current != generation ||
                observed.size >= 128 ||
                resolving >= 4 ||
                info.serviceName == ownName
            )
              return@post
            observed.add(info.serviceName)
            resolving++
            manager.resolveService(
              info,
              object : NsdManager.ResolveListener {
                override fun onResolveFailed(service: NsdServiceInfo, code: Int) {
                  main.post { if (current == generation) resolving-- }
                }

                override fun onServiceResolved(service: NsdServiceInfo) {
                  main.post { if (current == generation) resolving-- }
                  val address = service.host ?: return
                  val version = service.attributes["v"]?.toString(Charsets.US_ASCII)
                  if (version != "1") return
                  val endpoint = PairingRoute.endpoint(address, service.port) ?: return
                  main.post {
                    if (
                      current == generation &&
                        service.serviceName != ownName &&
                        observed.contains(service.serviceName) &&
                        devices.size < 128
                    ) {
                      devices[service.serviceName] = service.serviceName to endpoint
                      changed(devices.values.toList())
                    }
                  }
                }
              },
            )
          }
        }
      }
    discovery = listener
    manager.discoverServices("_beam-pair._tcp.", NsdManager.PROTOCOL_DNS_SD, listener)
  }

  fun advertise(port: Int, name: String) {
    if (port == advertisedPort) return
    registration?.let { runCatching { manager.unregisterService(it) } }
    registration = null
    advertisedPort = port
    if (port == 0) return
    val listener =
      object : NsdManager.RegistrationListener {
        override fun onServiceRegistered(info: NsdServiceInfo) {
          main.post {
            if (registration === this) ownName = info.serviceName
            else runCatching { manager.unregisterService(this) }
          }
        }

        override fun onRegistrationFailed(info: NsdServiceInfo, code: Int) {}

        override fun onServiceUnregistered(info: NsdServiceInfo) {}

        override fun onUnregistrationFailed(info: NsdServiceInfo, code: Int) {}
      }
    registration = listener
    manager.registerService(
      NsdServiceInfo().apply {
        serviceName = name
        serviceType = "_beam-pair._tcp."
        setPort(port)
        setAttribute("v", "1")
      },
      NsdManager.PROTOCOL_DNS_SD,
      listener,
    )
  }

  private fun stopBrowsing() {
    generation++
    discovery?.let { runCatching { manager.stopServiceDiscovery(it) } }
    discovery = null
    devices.clear()
    observed.clear()
    resolving = 0
    changed(emptyList())
  }

  fun close() {
    stopBrowsing()
    advertise(0, "")
    ownName = ""
  }
}
