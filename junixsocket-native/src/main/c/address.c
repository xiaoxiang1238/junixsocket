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
#include "address.h"

#include "exceptions.h"
#include "filedescriptors.h"
#include "jniutil.h"
#include "socket.h"

// Either we have sun_len (and we can skip the null byte), or we add the null byte at the end
static const socklen_t SUN_NAME_MAX_LEN = (socklen_t)(sizeof(struct sockaddr_un) - 2);

struct __attribute__((__packed__)) jux_tipc_addr {
    jint addrType;
    jint scope;
    jint a;
    jint b;
    jint c;
};

int domainToNative(int domain) {
    switch(domain) {
        case org_newsclub_net_unix_NativeUnixSocket_DOMAIN_UNIX:
            return AF_UNIX;
#if junixsocket_have_tipc
        case org_newsclub_net_unix_NativeUnixSocket_DOMAIN_TIPC:
            return AF_TIPC;
#endif
        default:
            // do not throw: _throwException(env, kExceptionSocketException, "Unsupported domain");
            return -1;
    }
}

/**
 * Initializes a sockaddr_un given a byte[] address, returning the socklen,
 * or 0 if an error occurred.
 */
socklen_t initSu(JNIEnv * env, struct sockaddr_un *su, jbyteArray addr) {
    const int maxLen = sizeof(su->sun_path);

    socklen_t addrLen = (socklen_t)(*env)->GetArrayLength(env, addr);
    if((int)addrLen <= 0 || addrLen >= maxLen) {
        return 0; // address out of range
    }

    const char* socketFile = (char*)(void*)(*env)->GetByteArrayElements(env, addr, NULL);
    if(socketFile == NULL) {
        return 0; // OOME
    }

    su->sun_family = AF_UNIX;
    memset(su->sun_path, 0, maxLen);
    memcpy(su->sun_path, socketFile, addrLen);

    (*env)->ReleaseByteArrayElements(env, addr, (jbyte*)(void*)socketFile, 0);
    socketFile = NULL;

#if defined(junixsocket_have_sun_len)
    su->sun_len = (unsigned char)(sizeof(*su) - sizeof(su->sun_path) + addrLen);
#endif

    socklen_t suLength = (socklen_t)(addrLen + sizeof(su->sun_family)
#if defined(junixsocket_have_sun_len)
                                     + sizeof(su->sun_len)
#endif
                                     );

    return suLength;
}

/*
 * Class:     org_newsclub_net_unix_NativeUnixSocket
 * Method:    maxAddressLength
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_org_newsclub_net_unix_NativeUnixSocket_maxAddressLength
(JNIEnv * env CK_UNUSED, jclass clazz CK_UNUSED)
{
    static struct sockaddr_un su;
    return sizeof(su.sun_path);
}

/*
 * Class:     org_newsclub_net_unix_NativeUnixSocket
 * Method:    sockAddrLength
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL Java_org_newsclub_net_unix_NativeUnixSocket_sockAddrLength
(JNIEnv * env, jclass clazz CK_UNUSED, jint domain)
{
    if(domain == 0) {
        // return maximum size of any supported address.
        return sizeof(jux_sockaddr_t);
    }
    switch(domainToNative(domain)) {
        case AF_UNIX:
            return sizeof(struct sockaddr_un);
#if junixsocket_have_tipc
        case AF_TIPC:
            return sizeof(struct sockaddr_tipc);
#endif
        default:
            _throwException(env, kExceptionSocketException, "Unsupported domain");
            return -1;
    }
}

static jbyteArray sockAddrUnToBytes(JNIEnv *env, struct sockaddr_un *addr, socklen_t len) {
    if(len <= 0) {
        return NULL;
    }
#if defined(junixsocket_have_sun_len)
    len = MIN(len, sizeof(struct sockaddr_un)-2);
    if(len < 256 && addr->sun_len < len) {
        len = addr->sun_len;
    }
#endif
    int terminator = -1;
    jboolean firstZero = (addr->sun_path[0] == 0);
    jboolean allZeros = firstZero;

    for(socklen_t i=1; i<len; i++) {
        char c = addr->sun_path[i];
        if(c == 0) {
            if(!firstZero && terminator == -1) {
                terminator = i;
                len = i;
            }
        } else {
            if(firstZero || terminator == -1) {
                allZeros = false;
            }
        }
    }

    if(allZeros) {
        len = 0;
    }

    if(len == 0) {
        return NULL;
    }

    jbyteArray array = (*env)->NewByteArray(env, len);
    (*env)->SetByteArrayRegion(env, array, 0, len, (jbyte*)addr->sun_path);

    return array;
}

#if junixsocket_have_tipc

static jbyteArray sockAddrTipcToBytes(JNIEnv *env, struct sockaddr_tipc *addr) {
    CK_IGNORE_RESERVED_IDENTIFIER_BEGIN // htonl
    struct jux_tipc_addr buf = {
        .addrType = htonl(addr->addrtype),
        .scope = htonl(addr->scope),
        .a = htonl(addr->addr.nameseq.type),
        .b = htonl(addr->addr.nameseq.lower),
        .c = htonl(addr->addr.nameseq.upper)
    };
    CK_IGNORE_RESERVED_IDENTIFIER_END

    jbyteArray array = (*env)->NewByteArray(env, sizeof(struct jux_tipc_addr));
    (*env)->SetByteArrayRegion(env, array, 0, sizeof(struct jux_tipc_addr), (jbyte*)&buf);
    return array;
}

#endif

/*
 * Class:     org_newsclub_net_unix_NativeUnixSocket
 * Method:    sockname
 * Signature: (ILjava/io/FileDescriptor;Z)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_newsclub_net_unix_NativeUnixSocket_sockname
(JNIEnv * env, jclass clazz CK_UNUSED, jint domain, jobject fd, jboolean peerName) {
    int handle = _getFD(env, fd);

    domain = domainToNative(domain);
    if(domain == -1) {
        _throwException(env, kExceptionSocketException, "Unsupported domain");
        return NULL;
    }

    jux_sockaddr_t addr = {0};
    socklen_t len = sizeof(jux_sockaddr_t);

    int ret;
    if(peerName) {
        ret = getpeername(handle, (struct sockaddr *)&addr, &len);
    } else {
        ret = getsockname(handle, (struct sockaddr *)&addr, &len);
    }

    if(ret == -1) {
        int errnum = socket_errno;
        switch(errnum) {
            case ENOTCONN:
            case EINVAL:
            case EBADF:
            case ENOTSOCK: // OSv socketpair
                // not connected or otherwise invalid
                break;
            default:
                _throwErrnumException(env, errnum, fd);
        }
        return NULL;
    }

    if(len > (socklen_t)sizeof(jux_sockaddr_t)) {
        _throwException(env, kExceptionSocketException,
                        peerName ? "peer sockname too long" : "sockname too long");
        return NULL;
    }

    if(len <= 2) {
        // hat tip: https://github.com/wahern/cqueues/blob/master/PORTING.md
        // also: Linux may return an incomplete address
        return NULL;
    }

    if(addr.addr.sa_family != domain) {
#if defined(_WIN32)
        if(addr.addr.sa_family == AF_INET && domain == AF_UNIX) {
            // socketpair emulation uses AF_INET sockets
            // just return NULL here; it's OK.
            return NULL;
        }
#endif
        _throwException(env, kExceptionSocketException,
                        "Unexpected socket address family");
        return NULL;
    }

    switch(((struct sockaddr *)&addr)->sa_family) {
        case AF_UNIX:
        {
            if(len > (socklen_t)sizeof(struct sockaddr_un)) {
                _throwException(env, kExceptionSocketException,
                                peerName ? "peer sockname too long" : "sockname too long");
                return NULL;
            }

#if defined(junixsocket_have_sun_len)
            len -= 2;
#else
            len -= 1;
#endif
            return sockAddrUnToBytes(env, (struct sockaddr_un *)&addr, len);
        }
#if defined(_WIN32)
        case AF_INET:
            // only to support our "socketpair" workaround (which we expected to return NULL here on UNIX)
            return NULL;
#endif
#if junixsocket_have_tipc
        case AF_TIPC:
            if(len > (socklen_t)sizeof(struct sockaddr_tipc)) {
                _throwException(env, kExceptionSocketException,
                                peerName ? "peer sockname too long" : "sockname too long");
                return NULL;
            }
            return sockAddrTipcToBytes(env, &addr.tipc);
#endif
        default:
            _throwException(env, kExceptionSocketException,
                            "Unsupported socket family");
            return NULL;
    }
}

/*
 * Class:     org_newsclub_net_unix_NativeUnixSocket
 * Method:    sockAddrToBytes
 * Signature: (ILjava/nio/ByteBuffer;)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_newsclub_net_unix_NativeUnixSocket_sockAddrToBytes
(JNIEnv *env, jclass clazz CK_UNUSED, jint domain, jobject directByteBuf) {
    domain = domainToNative(domain);
    size_t sockAddrLen;
    switch(domain) {
        case AF_UNIX:
            sockAddrLen = sizeof(struct sockaddr_un);
            break;
#if junixsocket_have_tipc
        case AF_TIPC:
            sockAddrLen = sizeof(struct sockaddr_tipc);
            break;
#endif
        default:
            _throwException(env, kExceptionSocketException, "Unsupported domain");
            return NULL;
    }

    struct jni_direct_byte_buffer_ref directByteBufRef =
    getDirectByteBufferRef (env, directByteBuf, 0, sockAddrLen);
    if(directByteBufRef.size <= 0) {
        _throwException(env, kExceptionSocketException, "Invalid byte buffer");
        return NULL;
    }

    jux_sockaddr_t *addr = (jux_sockaddr_t *)directByteBufRef.buf;
    if(addr->addr.sa_family != domain) {
        if(addr->addr.sa_family == 0) {
            return NULL;
        }
#if defined(_WIN32)
        if(addr->addr.sa_family == AF_INET && domain == AF_UNIX) {
            // socketpair emulation uses AF_INET sockets
            // just return NULL here; it's OK.
            return NULL;
        }
#endif
        _throwException(env, kExceptionSocketException, "Unexpected address family");
        return NULL;
    }

    switch(domain) {
        case AF_UNIX:
            return sockAddrUnToBytes(env, (struct sockaddr_un *)addr, SUN_NAME_MAX_LEN);
#if junixsocket_have_tipc
        case AF_TIPC:
            return sockAddrTipcToBytes(env, (struct sockaddr_tipc *)addr);
#endif
        default:
            _throwException(env, kExceptionSocketException, "Unsupported domain");
            return NULL;
    }
}

/*
 * Class:     org_newsclub_net_unix_NativeUnixSocket
 * Method:    bytesToSockAddr
 * Signature: (ILjava/nio/ByteBuffer;[B)I
 */
JNIEXPORT jint JNICALL Java_org_newsclub_net_unix_NativeUnixSocket_bytesToSockAddr
(JNIEnv *env, jclass clazz CK_UNUSED, jint domain, jobject directByteBuf, jbyteArray addressBytes) {
    domain = domainToNative(domain);
    size_t sockAddrLen;
    switch(domain) {
        case AF_UNIX:
            sockAddrLen = sizeof(struct sockaddr_un);
            break;
#if junixsocket_have_tipc
        case AF_TIPC:
            sockAddrLen = sizeof(struct sockaddr_tipc);
            break;
#endif
        default:
            // do not throw: _throwException(env, kExceptionSocketException, "Unsupported domain");
            return -1;
    }

    struct jni_direct_byte_buffer_ref directByteBufRef =
    getDirectByteBufferRef (env, directByteBuf, 0, sockAddrLen);
    if(directByteBufRef.size <= 0) {
        _throwException(env, kExceptionSocketException, "Invalid byte buffer");
        return -1;
    }

    jsize len = addressBytes == NULL ? 0 : (*env)->GetArrayLength(env, addressBytes);
    if(len > directByteBufRef.size || len > INT_MAX) {
        _throwException(env, kExceptionSocketException, "Byte array is too large");
        return -1;
    }

    jux_sockaddr_t *addr = (jux_sockaddr_t *)directByteBufRef.buf;
    memset(directByteBufRef.buf, 0, sockAddrLen);

    addr->addr.sa_family = domain;
    if(len <= 0) {
        return 0;
    }

    switch(domain) {
        case AF_UNIX:
        {
            struct sockaddr_un *addrUn = (struct sockaddr_un *) addr;
#if defined(junixsocket_have_sun_len)
            addrUn->sun_len = ((socklen_t)len >= SUN_NAME_MAX_LEN) ? SUN_NAME_MAX_LEN : len + 1; // including zero byte
#endif
            (*env)->GetByteArrayRegion(env, addressBytes, 0, len, (signed char*)addrUn->sun_path);
        }
            break;
#if junixsocket_have_tipc
        case AF_TIPC:
        {
            struct jux_tipc_addr jaddr = {0};
            (*env)->GetByteArrayRegion(env, addressBytes, 0, sizeof(struct jux_tipc_addr), (void*)&jaddr);

            CK_IGNORE_RESERVED_IDENTIFIER_BEGIN // ntohl
            addr->tipc.addrtype = ntohl(jaddr.addrType);
            addr->tipc.scope = ntohl(jaddr.scope);
            addr->tipc.addr.nameseq.type = ntohl(jaddr.a);
            addr->tipc.addr.nameseq.lower = ntohl(jaddr.b);
            addr->tipc.addr.nameseq.upper = ntohl(jaddr.c);
            CK_IGNORE_RESERVED_IDENTIFIER_END
        }
            break;
#endif
    }

    return (jint)sockAddrLen;
}

/*
 * Class:     org_newsclub_net_unix_NativeUnixSocket
 * Method:    sockAddrDataOffset
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_org_newsclub_net_unix_NativeUnixSocket_sockAddrNativeDataOffset
 (JNIEnv *env CK_UNUSED, jclass klazz CK_UNUSED) {
    return offsetof(struct sockaddr, sa_data);
}

/*
 * Class:     org_newsclub_net_unix_NativeUnixSocket
 * Method:    sockAddrNativeFamily
 * Signature: (Ljava/nio/ByteBuffer;)I
 */
JNIEXPORT jint JNICALL Java_org_newsclub_net_unix_NativeUnixSocket_sockAddrNativeFamilyOffset
 (JNIEnv *env CK_UNUSED, jclass klazz CK_UNUSED) {
    return offsetof(struct sockaddr, sa_family);
}
