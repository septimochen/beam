package org.beam.android

import java.net.Inet6Address
import java.net.InetAddress

internal object PairingRoute {
  fun endpoint(address: InetAddress, port: Int): String? {
    if (port !in 1..65535 || address.isAnyLocalAddress || address.isMulticastAddress) return null
    val raw = address.hostAddress ?: return null
    val host =
      if (address is Inet6Address && address.isLinkLocalAddress) {
        if (address.scopeId <= 0) return null
        raw.substringBefore('%') + "%" + address.scopeId
      } else raw.substringBefore('%')
    if (host.length > 256) return null
    return if (host.contains(':')) "[$host]:$port" else "$host:$port"
  }
}
