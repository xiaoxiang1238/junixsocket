/**
 * junixsocket
 *
 * Copyright 2009-2021 Christian Kohlschütter
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

import java.lang.reflect.InvocationTargetException;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Some testing-specific helper functions.
 * 
 * @author Christian Kohlschütter
 */
final class TestUtils {
  private static final Pattern PAT_PID_IN_MX_BEAN_NAME = Pattern.compile("^([0-9]+)\\@");

  private TestUtils() {
    throw new IllegalStateException("No instances");
  }

  /**
   * Returns the PID of the current process.
   * 
   * Workaround for Java 8 where <code>ProcessHandle.current().pid()</code> is not available.
   * 
   * @return The PID
   * @throws IllegalStateException if the PID could not be determined.
   */
  public static long getPid() {
    Class<?> phClass = null;
    try {
      phClass = Class.forName("java.lang.ProcessHandle");
    } catch (ClassNotFoundException e) {
      return getPidFallback();
    }
    try {
      Object currentProcessHandle = phClass.getMethod("current").invoke(null);
      return (long) phClass.getMethod("pid").invoke(currentProcessHandle);
    } catch (IllegalAccessException | IllegalArgumentException | InvocationTargetException
        | NoSuchMethodException | SecurityException e) {
      throw new IllegalStateException("Unable to determine current process PID", e);
    }
  }

  private static long getPidFallback() {
    Class<?> managementClass;
    Class<?> mxBeanClass;
    try {
      managementClass = Class.forName("java.lang.management.ManagementFactory");
      mxBeanClass = Class.forName("java.lang.management.RuntimeMXBean");
    } catch (ClassNotFoundException e) {
      throw new IllegalStateException("Unable to determine current process PID", e);
    }
    try {
      Object mxBean = managementClass.getMethod("getRuntimeMXBean").invoke(null);

      String name = mxBeanClass.getMethod("getName").invoke(mxBean).toString();
      Matcher m;
      if ((m = PAT_PID_IN_MX_BEAN_NAME.matcher(name)).find()) {
        long pid = Long.parseLong(m.group(1));
        return pid;
      }
    } catch (IllegalAccessException | IllegalArgumentException | InvocationTargetException
        | NoSuchMethodException | SecurityException e) {
      throw new IllegalStateException("Unable to determine current process PID", e);
    }
    throw new IllegalStateException("Unable to determine current process PID");
  }

  /**
   * Retrieves an integer value stored as a system property.
   *
   * @param key The name of the system property.
   * @param defaultValue The default value, if no value has been set.
   * @return The value
   * @throws IllegalArgumentException if the default value could not be parsed
   */
  public static int getIntSystemProperty(String key, int defaultValue) {
    String v = System.getProperty(key, null);
    if (v == null || v.trim().isEmpty()) { // NOPMD
      return defaultValue;
    }
    try {
      if ("true".equalsIgnoreCase(v)) {
        return 1;
      } else if ("false".equalsIgnoreCase(v)) {
        return 0;
      }
      return Integer.parseInt(v);
    } catch (Exception e) {
      throw new IllegalArgumentException("Illegal value for system property " + key + ": " + v, e);
    }
  }
}
