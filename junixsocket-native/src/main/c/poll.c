/*
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

#include "config.h"
#include "poll.h"

#include "exceptions.h"
#include "filedescriptors.h"

#if defined(junixsocket_use_poll_for_accept) || defined(junixsocket_use_poll_for_read)

static uint64_t timespecToMillis(struct timespec* ts) {
    return (uint64_t)ts->tv_sec * 1000 + (uint64_t)ts->tv_nsec / 1000000;
}

/*
 * Waits until the connection is ready to read/accept.
 *
 * Returns -1 if an exception was thrown, 0 if a timeout occurred, 1 if ready.
 */
jint pollWithTimeout(JNIEnv * env, jobject fd, int handle, int timeout) {
#if defined(_WIN32)
    DWORD optVal;
#else
    struct timeval optVal;
#endif
    socklen_t optLen = sizeof(optVal);
    int ret = getsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, WIN32_NEEDS_CHARP &optVal, &optLen);

    uint64_t millis = 0;
    if(ret != 0) {
        _throwSockoptErrnumException(env, socket_errno, fd);
        return -1;
    }
#if defined(_WIN32)
    if(optLen >= (socklen_t)sizeof(optVal)) {
        millis = optVal;
    }
#else
    if(optLen >= sizeof(optVal) && (optVal.tv_sec > 0 || optVal.tv_usec > 0)) {
        millis = ((uint64_t)optVal.tv_sec * 1000) + (uint64_t)(optVal.tv_usec / 1000);
    }
#endif

    if(timeout > 0 && millis < (uint64_t)timeout) {
        // Some platforms (Windows) may not support SO_TIMEOUT, so let's override the timeout with our own value
        millis = (uint64_t)timeout;
    }

    if(millis <= 0) {
        return 1;
    }

    if(millis > INT_MAX) {
        millis = INT_MAX;
    }
    struct pollfd pfd;
    pfd.fd = handle;
    pfd.events = (POLLIN);
    pfd.revents = 0;

    int millisRemaining = (int)millis;

    struct pollfd fds[] = {pfd};

    struct timespec timeStart;
    struct timespec timeEnd;

    if(clock_gettime(CLOCK_MONOTONIC, &timeEnd) == -1) {
        _throwErrnumException(env, errno, NULL);
        return -1;
    }

    while (millisRemaining > 0) {
        // FIXME: should this be in a loop to ensure the timeout condition is met?

        timeStart = timeEnd;

        int pollTime = millisRemaining;
#  if defined(junixsocket_use_poll_interval_millis)
        // Since poll doesn't abort upon closing the socket,
        // let's simply poll on a frequent basis
        if(pollTime > junixsocket_use_poll_interval_millis) {
            pollTime = junixsocket_use_poll_interval_millis;
        }
#  endif

#  if defined(_WIN32)
        ret = WSAPoll(fds, 1, pollTime);
#  else
        ret = poll(fds, 1, pollTime);
#  endif
        if(ret == 1) {
            if((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) == 0) {
                break;
            } else {
                // timeout
                return 0;
            }
        }
        int errnum = socket_errno;
        if(clock_gettime(CLOCK_MONOTONIC, &timeEnd) == -1) {
            _throwErrnumException(env, errnum, NULL);
            return -1;
        }
        int elapsed = (int)(timespecToMillis(&timeEnd) - timespecToMillis(&timeStart));
        if(elapsed <= 0) {
            elapsed = 1;
        }
        millisRemaining -= elapsed;
        if(millisRemaining <= 0) {
            // timeout
            return 0;
        }

        if(ret == -1) {
            if(errnum == EAGAIN) {
                // try again
                continue;
            }

            if(errnum == ETIMEDOUT) {
                return 0;
            } else {
                _throwErrnumException(env, errnum, fd);
                return -1;
            }
        }
    }

    return 1;
}

#endif

/*
 * Class:     org_newsclub_net_unix_NativeUnixSocket
 * Method:    available
 * Signature: (Ljava/io/FileDescriptor;)I
 */
JNIEXPORT jint JNICALL Java_org_newsclub_net_unix_NativeUnixSocket_available(
                                                                             JNIEnv * env, jclass clazz CK_UNUSED, jobject fd)
{
    int handle = _getFD(env, fd);

    // the following would actually block and keep the peek'ed byte in the buffer
    //ssize_t count = recv(handle, &buf, 1, MSG_PEEK);

    int ret;
#if defined(_WIN32)
    u_long count;
    ret = ioctlsocket(handle, FIONREAD, &count);
#else
    int count;
    ret = ioctl(handle, FIONREAD, &count);
#endif
    if((int)count == -1 || ret == -1) {
        _throwErrnumException(env, socket_errno, fd);
        return -1;
    }

    return count;
}