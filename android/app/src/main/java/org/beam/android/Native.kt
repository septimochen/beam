package org.beam.android

internal object Native {
  init {
    System.loadLibrary("beam-android")
  }

  external fun create(directory: ByteArray): Long

  external fun destroy(handle: Long)

  external fun call(handle: Long, command: Int, arguments: Array<ByteArray>): Array<ByteArray>
}

internal class NativeSession(directory: String) : AutoCloseable {
  private var handle = Native.create(directory.toByteArray(Charsets.UTF_8))

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
}
