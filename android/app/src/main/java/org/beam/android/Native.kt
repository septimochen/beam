package org.beam.android

internal object Native {
  init {
    System.loadLibrary("beam-android")
  }

  external fun create(directory: ByteArray, port: Int, timeout: Int): Long

  external fun destroy(handle: Long)

  external fun call(handle: Long, command: Int, arguments: Array<ByteArray>): Array<ByteArray>
}

internal class NativeSession(directory: String, port: Int = 4269, timeout: Int = 30000) :
  AutoCloseable {
  private var handle = Native.create(directory.toByteArray(Charsets.UTF_8), port, timeout)

  fun call(command: Int, vararg arguments: String): List<String> {
    check(handle != 0L) { "Beam session is closed" }
    return Native.call(
        handle,
        command,
        arguments.map { it.toByteArray(Charsets.UTF_8) }.toTypedArray(),
      )
      .map { it.toString(Charsets.UTF_8) }
  }

  override fun close() {
    if (handle != 0L) Native.destroy(handle)
    handle = 0
  }
}

internal object Command {
  const val IDENTITY = 0
  const val PEERS = 1
  const val PAIR = 2
  const val UNPAIR = 3
  const val ENDPOINT = 4
  const val SEND = 5
  const val RECEIVE = 6
  const val CANCEL = 7
  const val STOP = 8
  const val EVENTS = 9
  const val BUSY = 10
  const val CHECK = 11
  const val PAIRING_HOST = 12
  const val PAIRING_JOIN = 13
  const val PAIRING_PIN = 14
  const val PAIRING_APPROVE = 15
  const val PAIRING_SNAPSHOT = 16
  const val PAIRING_BROWSE = 17
  const val PAIRING_CANCEL = 18
  const val PAIRING_FORGET = 19
  const val PAIRING_REJECT = 20
  const val DEVICE_DETAILS = 21
  const val EDIT_DEVICE = 22
  const val EXPORT_DEVICES = 23
  const val PREVIEW_DEVICES = 24
  const val IMPORT_DEVICES = 25
}
