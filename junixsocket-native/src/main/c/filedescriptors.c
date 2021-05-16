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
#include "filedescriptors.h"

#include "exceptions.h"

/*
 * Class:     org_newsclub_net_unix_NativeUnixSocket
 * Method:    getFD
 * Signature: (Ljava/io/FileDescriptor;)I
 */
JNIEXPORT jint JNICALL Java_org_newsclub_net_unix_NativeUnixSocket_getFD(
                                                                         JNIEnv *env, jclass clazz CK_UNUSED, jobject fd)
{
    return _getFD(env, fd);
}

/*
 * Class:     org_newsclub_net_unix_NativeUnixSocket
 * Method:    initFD
 * Signature: (Ljava/io/FileDescriptor;I)V
 */
JNIEXPORT void JNICALL Java_org_newsclub_net_unix_NativeUnixSocket_initFD(
                                                                          JNIEnv * env, jclass clazz CK_UNUSED, jobject fd, jint handle)
{
    _initFD(env, fd, handle);
}

int _getFD(JNIEnv * env, jobject fd)
{
    jclass fileDescriptorClass = (*env)->GetObjectClass(env, fd);
    jfieldID fdField = (*env)->GetFieldID(env, fileDescriptorClass, "fd", "I");
    if(fdField == NULL) {
        _throwException(env, kExceptionSocketException,
                        "Cannot find field \"fd\" in java.io.FileDescriptor. Unsupported JVM?");
        return 0;
    }
    return (*env)->GetIntField(env, fd, fdField);
}

void _initFD(JNIEnv * env, jobject fd, int handle)
{
    jclass fileDescriptorClass = (*env)->GetObjectClass(env, fd);
    jfieldID fdField = (*env)->GetFieldID(env, fileDescriptorClass, "fd", "I");
    if(fdField == NULL) {
        _throwException(env, kExceptionSocketException,
                        "Cannot find field \"fd\" in java.io.FileDescriptor. Unsupported JVM?");
        return;
    }
    (*env)->SetIntField(env, fd, fdField, handle);
}

// Close a file descriptor. fd object and numeric handle must either be identical,
// or only one of them be valid.
//
// fd objects are marked closed by setting their fd value to -1.
int _closeFd(JNIEnv * env, jobject fd, int handle)
{
    int ret = 0;
    if(handle > 0) {
        shutdown(handle, SHUT_RDWR);
#if defined(_WIN32)
        ret = closesocket(handle);
#else
        ret = close(handle);
#endif
    }
    if(fd == NULL) {
        return ret;
    }
    (*env)->MonitorEnter(env, fd);
    int fdHandle = _getFD(env, fd);
    _initFD(env, fd, -1);
    (*env)->MonitorExit(env, fd);

    if(handle > 0) {
        if(fdHandle > 0 && handle != fdHandle) {
#if DEBUG
            fprintf(stderr, "NativeUnixSocket_closeFd inconsistency: handle %i vs fdHandle %i\n", handle, fdHandle);
            fflush(stderr);
#endif
        }
    } else if(fdHandle > 0) {
        shutdown(fdHandle, SHUT_RDWR);
#if defined(_WIN32)
        ret = closesocket(fdHandle);
#else
        ret = close(fdHandle);
#endif
    }

    return ret;
}

/*
 * Class:     org_newsclub_net_unix_NativeUnixSocket
 * Method:    close
 * Signature: (Ljava/io/FileDescriptor;)V
 */
JNIEXPORT void JNICALL Java_org_newsclub_net_unix_NativeUnixSocket_close(
                                                                         JNIEnv * env, jclass clazz CK_UNUSED, jobject fd)
{
    if(fd == NULL) {
        _throwException(env, kExceptionNullPointerException, "fd");
        return;
    }
    (*env)->MonitorEnter(env, fd);
    int handle = _getFD(env, fd);
    _initFD(env, fd, -1);
    (*env)->MonitorExit(env, fd);

    int ret = _closeFd(env, fd, handle);
    if(ret == -1) {
        _throwErrnumException(env, errno, NULL);
        return;
    }
}

/*
 * Class:     org_newsclub_net_unix_NativeUnixSocket
 * Method:    shutdown
 * Signature: (Ljava/io/FileDescriptor;I)V
 */
JNIEXPORT void JNICALL Java_org_newsclub_net_unix_NativeUnixSocket_shutdown(
                                                                            JNIEnv * env, jclass clazz CK_UNUSED, jobject fd, jint mode)
{
    int handle = _getFD(env, fd);
    int ret = shutdown(handle, mode);
    if(ret == -1) {
        int errnum = socket_errno;
        if(errnum == ENOTCONN) {
            // ignore
            return;
        }
        _throwErrnumException(env, errnum, fd);
        return;
    }
}