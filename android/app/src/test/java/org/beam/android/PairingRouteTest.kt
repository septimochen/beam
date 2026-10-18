package org.beam.android

import java.net.Inet6Address
import java.net.InetAddress
import org.junit.Assert.*
import org.junit.Test

class PairingRouteTest {
  @Test
  fun normalizesReceiverLocalIpv6Scopes() {
    val bytes = InetAddress.getByName("fe80::1").address
    assertTrue(
      checkNotNull(PairingRoute.endpoint(Inet6Address.getByAddress(null, bytes, 7), 4269))
        .endsWith("%7]:4269")
    )
    assertNull(PairingRoute.endpoint(Inet6Address.getByAddress(null, bytes, 0), 4269))
    assertEquals(
      "192.168.1.2:4269",
      PairingRoute.endpoint(InetAddress.getByName("192.168.1.2"), 4269),
    )
  }

  @Test
  fun rejectsUnusableDiscoveryHints() {
    assertNull(PairingRoute.endpoint(InetAddress.getByName("0.0.0.0"), 4269))
    assertNull(PairingRoute.endpoint(InetAddress.getByName("224.0.0.1"), 4269))
    assertNull(PairingRoute.endpoint(InetAddress.getByName("192.168.1.2"), 0))
  }
}
