/*
 * junixsocket
 *
 * Copyright 2009-2022 Christian Kohlschütter
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package org.newsclub.net.unix;

import java.io.FileDescriptor;
import java.io.IOException;
import java.net.SocketAddress;
import java.net.SocketException;
import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * The core functionality of file descriptor based I/O.
 * 
 * @author Christian Kohlschütter
 */
class AFCore extends CleanableState {
  private static final ThreadLocal<ByteBuffer> DATAGRAMPACKET_BUFFER_TL = new ThreadLocal<>();

  private static final int DATAGRAMPACKET_BUFFER_MIN_CAPACITY = 8192;
  private static final int DATAGRAMPACKET_BUFFER_MAX_CAPACITY = 1 * 1024 * 1024;

  private final AtomicBoolean closed = new AtomicBoolean(false);

  final FileDescriptor fd;
  final AncillaryDataSupport ancillaryDataSupport;

  private final boolean datagramMode;

  private boolean blocking = true;

  AFCore(Object observed, FileDescriptor fd, AncillaryDataSupport ancillaryDataSupport,
      boolean datagramMode) {
    super(observed);
    this.datagramMode = datagramMode;
    this.fd = (fd == null) ? new FileDescriptor() : fd;
    this.ancillaryDataSupport = ancillaryDataSupport;
  }

  AFCore(Object observed, FileDescriptor fd) {
    this(observed, fd, null, false);
  }

  @Override
  protected void doClean() {
    if (fd != null && fd.valid()) {
      try {
        doClose();
      } catch (IOException e) {
        // ignore
      }
    }
    if (ancillaryDataSupport != null) {
      ancillaryDataSupport.close();
    }
  }

  boolean isClosed() {
    return closed.get();
  }

  void doClose() throws IOException {
    NativeUnixSocket.close(fd);
    closed.set(true);
  }

  FileDescriptor validFdOrException() throws SocketException {
    FileDescriptor fdesc = validFd();
    if (fdesc == null) {
      closed.set(true);
      throw new SocketClosedException("Not open");
    }
    return fdesc;
  }

  synchronized FileDescriptor validFd() {
    if (isClosed()) {
      return null;
    }
    FileDescriptor descriptor = this.fd;
    if (descriptor != null) {
      if (descriptor.valid()) {
        return descriptor;
      }
    }
    return null;
  }

  int read(ByteBuffer dst) throws IOException {
    return read(dst, null, 0);
  }

  int read(ByteBuffer dst, ByteBuffer socketAddressBuffer, int options) throws IOException {
    int remaining = dst.remaining();
    if (remaining == 0) {
      return 0;
    }
    FileDescriptor fdesc = validFdOrException();

    ByteBuffer buf;
    if (dst.isDirect()) {
      buf = dst;
    } else {
      buf = getThreadLocalDirectByteBuffer(remaining);
      remaining = Math.min(remaining, buf.remaining());
    }

    if (!blocking) {
      options |= NativeUnixSocket.OPT_NON_BLOCKING;
    }

    int pos = dst.position();

    int count = NativeUnixSocket.receive(fdesc, buf, pos, remaining, socketAddressBuffer, options,
        ancillaryDataSupport, 0);
    if (count == -1) {
      return count;
    }
    if (buf != dst) { // NOPMD
      buf.limit(count);
      dst.put(buf);
    } else {
      if (count < 0) {
        throw new IllegalStateException();
      }
      dst.position(pos + count);
    }
    return count;
  }

  int write(ByteBuffer src) throws IOException {
    return write(src, null, 0);
  }

  int write(ByteBuffer src, SocketAddress target, int options) throws IOException {
    int remaining = src.remaining();

    if (remaining == 0) {
      return 0;
    }

    FileDescriptor fdesc = validFdOrException();
    final ByteBuffer addressTo;
    final int addressToLen;
    if (target == null) {
      addressTo = null;
      addressToLen = 0;
    } else {
      addressTo = AFSocketAddress.SOCKETADDRESS_BUFFER_TL.get();
      addressToLen = AFSocketAddress.unwrapAddressDirectBufferInternal(addressTo, target);
    }

    // accept "send buffer overflow" as packet loss
    // and don't retry (which may slow things down quite a bit)
    if (!blocking) {
      options |= NativeUnixSocket.OPT_NON_BLOCKING;
    }

    int pos = src.position();

    boolean isDirect = src.isDirect();
    ByteBuffer buf;
    if (isDirect) {
      buf = src;
    } else {
      buf = getThreadLocalDirectByteBuffer(remaining);
      remaining = Math.min(remaining, buf.remaining());

      // Java 16: buf.put(buf.position(), src, src.position(), Math.min(buf.limit(), src.limit()));
      int limit = src.limit();
      if (limit > buf.limit()) {
        src.limit(buf.limit());
        buf.put(src);
        src.limit(limit);
      } else {
        buf.put(src);
      }

      buf.position(0);
    }
    if (datagramMode) {
      options |= NativeUnixSocket.OPT_DGRAM_MODE;
    }

    int written = NativeUnixSocket.send(fdesc, buf, pos, remaining, addressTo, addressToLen,
        options, ancillaryDataSupport);
    src.position(pos + written);

    return written;
  }

  ByteBuffer getThreadLocalDirectByteBuffer(int capacity) {
    if (capacity > DATAGRAMPACKET_BUFFER_MAX_CAPACITY) {
      capacity = DATAGRAMPACKET_BUFFER_MAX_CAPACITY;
    } else if (capacity < DATAGRAMPACKET_BUFFER_MIN_CAPACITY) {
      capacity = DATAGRAMPACKET_BUFFER_MIN_CAPACITY;
    }
    ByteBuffer datagramPacketBuffer = DATAGRAMPACKET_BUFFER_TL.get();
    if (datagramPacketBuffer == null || capacity > datagramPacketBuffer.capacity()) {
      datagramPacketBuffer = ByteBuffer.allocateDirect(capacity);
      DATAGRAMPACKET_BUFFER_TL.set(datagramPacketBuffer);
    }
    datagramPacketBuffer.clear();
    return datagramPacketBuffer;
  }

  void implConfigureBlocking(boolean block) throws IOException {
    NativeUnixSocket.configureBlocking(validFdOrException(), block);
    this.blocking = block;
  }

  boolean isBlocking() {
    return blocking;
  }
}
