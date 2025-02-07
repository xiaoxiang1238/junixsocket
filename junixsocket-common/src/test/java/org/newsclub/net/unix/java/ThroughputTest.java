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
package org.newsclub.net.unix.java;

import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import java.lang.reflect.InvocationTargetException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ProtocolFamily;
import java.net.SocketAddress;
import java.net.StandardProtocolFamily;
import java.nio.channels.DatagramChannel;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.channels.spi.SelectorProvider;

import org.junit.jupiter.api.Test;

import com.kohlschutter.annotations.compiletime.SuppressFBWarnings;
import com.kohlschutter.testutil.AvailabilityRequirement;
import com.kohlschutter.testutil.SystemPropertyRequirement;

@SuppressFBWarnings({
    "THROWS_METHOD_THROWS_CLAUSE_THROWABLE", "THROWS_METHOD_THROWS_CLAUSE_BASIC_EXCEPTION",
    "NM_SAME_SIMPLE_NAME_AS_SUPERCLASS"})
@JavaInetStackRequirement
public final class ThroughputTest extends org.newsclub.net.unix.ThroughputTest<InetSocketAddress> {

  public ThroughputTest() {
    super(JavaAddressSpecifics.INSTANCE);
  }

  @Test
  @AvailabilityRequirement(classes = {"java.net.UnixDomainSocketAddress"}, //
      message = "This test requires Java 16 or later")
  public void testJEP380() throws Exception {
    assumeTrue(ENABLED > 0, "Throughput tests are disabled");
    assumeTrue(PAYLOAD_SIZE > 0, "Payload must be positive");
    runTestJEP380(false);
  }

  @Test
  @AvailabilityRequirement(classes = {"java.net.UnixDomainSocketAddress"}, //
      message = "This test requires Java 16 or later")
  public void testJEP380directBuffer() throws Exception {
    assumeTrue(ENABLED > 0, "Throughput tests are disabled");
    assumeTrue(PAYLOAD_SIZE > 0, "Payload must be positive");
    runTestJEP380(true);
  }

  private static SocketAddress jep380SocketAddress(String path) throws IllegalAccessException,
      IllegalArgumentException, InvocationTargetException, SecurityException {
    try {
      // We use reflection so we can compile on older Java versions
      Class<?> klazz = Class.forName("java.net.UnixDomainSocketAddress");
      return (SocketAddress) klazz.getMethod("of", String.class).invoke(null, path);
    } catch (NoSuchMethodException | ClassNotFoundException e) {
      assumeTrue(false, "java.net.UnixDomainSocketAddress (JEP 380) not supported by JVM");
      return null;
    }
  }

  private void runTestJEP380(boolean direct) throws Exception {
    SocketAddress sa = jep380SocketAddress(socketFile().getPath());

    ServerSocketChannel ssc;
    // We use reflection so we can compile on older Java versions
    try {
      ssc = (ServerSocketChannel) ServerSocketChannel.class.getMethod("open", ProtocolFamily.class)
          .invoke(null, StandardProtocolFamily.valueOf("UNIX"));
    } catch (Exception e) {
      throw new IllegalStateException(e);
    }

    runTestSocketChannel("JEP380 SocketChannel", sa, ssc, () -> SocketChannel.open(ssc
        .getLocalAddress()), direct);
  }

  @Test
  @SystemPropertyRequirement(property = "org.newsclub.net.unix.throughput-test.ip.enabled", //
      value = "1", message = "Loopback TCP/IP testing is disabled")
  public void testTCPLoopback() throws Exception {
    assumeTrue(ENABLED > 0, "Throughput tests are disabled");
    assumeTrue(PAYLOAD_SIZE > 0, "Payload must be positive");
    runTestTCPLoopback(false);
  }

  @Test
  @SystemPropertyRequirement(property = "org.newsclub.net.unix.throughput-test.ip.enabled", //
      value = "1", message = "Loopback TCP/IP testing is disabled")
  public void testTCPLoopbackDirectBuffer() throws Exception {
    assumeTrue(ENABLED > 0, "Throughput tests are disabled");
    assumeTrue(PAYLOAD_SIZE > 0, "Payload must be positive");
    runTestTCPLoopback(true);
  }

  private void runTestTCPLoopback(boolean direct) throws Exception {
    final SocketAddress sa = new InetSocketAddress(InetAddress.getLoopbackAddress(), 0);

    ServerSocketChannel ssc = ServerSocketChannel.open();

    runTestSocketChannel("TCP-Loopback", sa, ssc, () -> SocketChannel.open(ssc.getLocalAddress()),
        direct);
  }

  @Test
  @SystemPropertyRequirement(property = "org.newsclub.net.unix.throughput-test.ip.enabled", //
      value = "1", message = "Loopback UDP/IP testing is disabled")
  public void testUDPLoopback() throws Exception {
    testUDPLoopbackDatagramChannel(false);
  }

  @Test
  @SystemPropertyRequirement(property = "org.newsclub.net.unix.throughput-test.ip.enabled", //
      value = "1", message = "Loopback UDP/IP testing is disabled")
  public void testUDPLoopbackDirectBuffer() throws Exception {
    testUDPLoopbackDatagramChannel(true);
  }

  private void testUDPLoopbackDatagramChannel(boolean direct) throws Exception {
    SocketAddress dsAddr = new InetSocketAddress(InetAddress.getLoopbackAddress(), 0);
    SocketAddress dcAddr = new InetSocketAddress(InetAddress.getLoopbackAddress(), 0);

    try (DatagramChannel ds = SelectorProvider.provider().openDatagramChannel();
        DatagramChannel dc = SelectorProvider.provider().openDatagramChannel()) {
      ds.bind(dsAddr);
      dc.bind(dcAddr).connect(ds.getLocalAddress());
      ds.connect(dc.getLocalAddress());

      assertNotEquals(ds.getLocalAddress(), dc.getLocalAddress());

      SelectorProvider sp = selectorProvider();

      testSocketDatagramChannel("UDP-Loopback DatagramChannel", ds, dc, sp, direct, true);
    }
  }

  @Override
  protected String stbTestType() {
    return "java.net";
  }
}
