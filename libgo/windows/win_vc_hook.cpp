#include <WinSock2.h>
#include <Windows.h>
#include <MinHook.h>
#include "../scheduler.h"

#if __MINGW32__

typedef ULONGLONG (WINAPI *GetTickCount64_t)(void);

static GetTickCount64_t
        GetTickCount64 = (GetTickCount64_t) GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetTickCount64");
#endif

namespace co
{

    typedef int (WINAPI *ioctlsocket_t)(
            _In_    SOCKET s,
            _In_    long cmd,
            _Inout_ u_long *argp
    );

    static ioctlsocket_t
            ioctlsocket_f = NULL;//(ioctlsocket_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "ioctlsocket");

    static int WINAPI
    hook_ioctlsocket(
            _In_    SOCKET s,
            _In_    long cmd,
            _Inout_ u_long *argp
    )
    {
        int ret = ioctlsocket_f(s, cmd, argp);
        int err = WSAGetLastError();
        if (ret == 0 && (unsigned long) cmd == FIONBIO)
        {
            int v = *argp;
            setsockopt(s, SOL_SOCKET, SO_GROUP_PRIORITY, (const char *) &v, sizeof(v));
        }
        WSASetLastError(err);
        return ret;
    }

    typedef int ( WINAPI *WSAIoctl_t)(
            _In_  SOCKET s,
            _In_  DWORD dwIoControlCode,
            _In_  LPVOID lpvInBuffer,
            _In_  DWORD cbInBuffer,
            _Out_ LPVOID lpvOutBuffer,
            _In_  DWORD cbOutBuffer,
            _Out_ LPDWORD lpcbBytesReturned,
            _In_  LPWSAOVERLAPPED lpOverlapped,
            _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    );

    static WSAIoctl_t WSAIoctl_f = NULL;//(WSAIoctl_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSAIoctl");

    static int WINAPI
    hook_WSAIoctl(
            _In_  SOCKET s,
            _In_  DWORD dwIoControlCode,
            _In_  LPVOID lpvInBuffer,
            _In_  DWORD cbInBuffer,
            _Out_ LPVOID lpvOutBuffer,
            _In_  DWORD cbOutBuffer,
            _Out_ LPDWORD lpcbBytesReturned,
            _In_  LPWSAOVERLAPPED lpOverlapped,
            _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    )
    {
        int
                ret = WSAIoctl_f(
                s,
                dwIoControlCode,
                lpvInBuffer,
                cbInBuffer,
                lpvOutBuffer,
                cbOutBuffer,
                lpcbBytesReturned,
                lpOverlapped,
                lpCompletionRoutine
        );
        //int err = WSAGetLastError();
        //if (ret == 0 && cmd == FIONBIO) {
        //    int v = *argp;
        //    setsockopt(s, SOL_SOCKET, SO_GROUP_PRIORITY, (const char*)&v, sizeof(v));
        //}
        //WSASetLastError(err);
        return ret;
    }

    bool
    SetNonblocking(SOCKET s, bool is_nonblocking)
    {
        u_long v = is_nonblocking ? 1 : 0;
        return ioctlsocket(s, FIONBIO, &v) == 0;
    }

    bool
    IsNonblocking(SOCKET s)
    {
        int v    = 0;
        int vlen = sizeof(v);
        if (0 != getsockopt(s, SOL_SOCKET, SO_GROUP_PRIORITY, (char *) &v, &vlen))
        {
            if (WSAENOTSOCK == WSAGetLastError())
            {
                return true;
            }
        }
        return !!v;
    }

    typedef int ( WINAPI *select_t)(
            _In_    int nfds,
            _Inout_ fd_set *readfds,
            _Inout_ fd_set *writefds,
            _Inout_ fd_set *exceptfds,
            _In_    const struct timeval *timeout
    );

    static select_t select_f = NULL;//(select_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "select");

    static inline int WINAPI
    safe_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds)
    {
        //Task *tk = g_Scheduler.GetCurrentTask();
        //DebugPrint(dbg_hook, "task(%s) safe_select(nfds=%d, rfds=%p, wfds=%p, efds=%p).",
        //    tk ? tk->DebugInfo() : "nil", (int)nfds, readfds, writefds, exceptfds);
        static const struct timeval zero_tmv{0, 0};
        fd_set                      *rfds = NULL, *wfds = NULL, *efds = NULL;
        fd_set                      fds[3];
        if (readfds)
        {
            fds[0] = *readfds;
            rfds = &fds[0];
        }
        if (writefds)
        {
            fds[1] = *writefds;
            wfds = &fds[1];
        }
        if (exceptfds)
        {
            fds[2] = *exceptfds;
            efds = &fds[2];
        }
        int ret = select_f(nfds, rfds, wfds, efds, &zero_tmv);
        if (ret <= 0)
        { return ret; }

        if (readfds)
        { *readfds = fds[0]; }
        if (writefds)
        { *writefds = fds[1]; }
        if (exceptfds)
        { *exceptfds = fds[2]; }
        return ret;
    }

    static int WINAPI
    hook_select(
            _In_    int nfds,
            _Inout_ fd_set *readfds,
            _Inout_ fd_set *writefds,
            _Inout_ fd_set *exceptfds,
            _In_    const struct timeval *timeout
    )
    {
        //static const struct timeval zero_tmv{0, 0};
        int  timeout_ms = timeout ? (timeout->tv_sec * 1000 + timeout->tv_usec / 1000) : -1;
        Task *tk        = g_Scheduler.GetCurrentTask();
        DebugPrint(dbg_hook, "task(%s) Hook select(nfds=%d, rfds=%p, wfds=%p, efds=%p, timeout=%d).",
                   tk ? tk->DebugInfo() : "nil", (int) nfds, readfds, writefds, exceptfds, timeout_ms);

        if (!tk || !timeout_ms)
        {
            return select_f(nfds, readfds, writefds, exceptfds, timeout);
        }

        // async select
        int ret = safe_select(nfds, readfds, writefds, exceptfds);
        if (ret)
        { return ret; }

        ULONGLONG start_time = GetTickCount64();
        int       delta_time = 1;
        while (-1 == timeout_ms || GetTickCount64() - start_time < (ULONGLONG) timeout_ms)
        {
            ret = safe_select(nfds, readfds, writefds, exceptfds);
            if (ret > 0)
            { return ret; }

            if (exceptfds)
            {
                // ��Ϊwindows��select, �������ȼ���ʱ���ܲ���error, ��˴˴���Ҫ�ֶ�check
                fd_set e_result;
                FD_ZERO(&e_result);
                for (u_int i = 0; i < exceptfds->fd_count; ++i)
                {
                    SOCKET fd     = exceptfds->fd_array[i];
                    int    err    = 0;
                    int    errlen = sizeof(err);
                    getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *) &err, &errlen);
                    if (err)
                    {
                        FD_SET(fd, &e_result);
                    }
                }

                if (e_result.fd_count > 0)
                {
                    // Some errors were happened.
                    if (readfds) FD_ZERO(readfds);
                    if (writefds) FD_ZERO(writefds);
                    *exceptfds = e_result;
                    return e_result.fd_count;
                }
            }

            g_Scheduler.SleepSwitch(delta_time);
            if (delta_time < 16)
            {
                delta_time <<= 2;
            }
        }

        if (readfds) FD_ZERO(readfds);
        if (writefds) FD_ZERO(writefds);
        if (exceptfds) FD_ZERO(exceptfds);
        return 0;
    }

    template<typename OriginF, typename ... Args>
    static int
    connect_mode_hook(OriginF fn, const char *fn_name, SOCKET s, Args &&... args)
    {
        Task *tk            = g_Scheduler.GetCurrentTask();
        bool is_nonblocking = IsNonblocking(s);
        DebugPrint(dbg_hook, "task(%s) Hook %s(s=%d)(nonblocking:%d).",
                   tk ? tk->DebugInfo() : "nil", fn_name, (int) s, (int) is_nonblocking);
        if (!tk || is_nonblocking)
        {
            return fn(s, std::forward<Args>(args)...);
        }

        // async connect
        if (!SetNonblocking(s, true))
        {
            return fn(s, std::forward<Args>(args)...);
        }

        int ret = fn(s, std::forward<Args>(args)...);
        if (ret == 0)
        { return 0; }

        int err = WSAGetLastError();
        if (WSAEWOULDBLOCK != err && WSAEINPROGRESS != err)
        {
            return ret;
        }

        fd_set wfds = {}, efds = {};
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
        FD_SET(s, &wfds);
        FD_SET(s, &efds);
        select(1, NULL, &wfds, &efds, NULL);
        if (!FD_ISSET(s, &efds) && FD_ISSET(s, &wfds))
        {
            SetNonblocking(s, false);
            return 0;
        }

        err = 0;
        int errlen = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char *) &err, &errlen);
        if (err)
        {
            SetNonblocking(s, false);
            WSASetLastError(err);
            return SOCKET_ERROR;
        }

        SetNonblocking(s, false);
        return 0;
    }

    typedef int ( WINAPI *connect_t)(
            _In_ SOCKET s,
            _In_ const struct sockaddr *name,
            _In_ int namelen
    );

    static connect_t connect_f = NULL;//(connect_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "connect");

    static int WINAPI
    hook_connect(
            _In_ SOCKET s,
            _In_ const struct sockaddr *name,
            _In_ int namelen
    )
    {
        return connect_mode_hook(connect_f, "connect", s, name, namelen);
    }

    typedef int ( WINAPI *WSAConnect_t)(
            _In_  SOCKET s,
            _In_  const struct sockaddr *name,
            _In_  int namelen,
            _In_  LPWSABUF lpCallerData,
            _Out_ LPWSABUF lpCalleeData,
            _In_  LPQOS lpSQOS,
            _In_  LPQOS lpGQOS
    );

    static WSAConnect_t
            WSAConnect_f = NULL;//(WSAConnect_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSAConnect");

    static int WINAPI
    hook_WSAConnect(
            _In_  SOCKET s,
            _In_  const struct sockaddr *name,
            _In_  int namelen,
            _In_  LPWSABUF lpCallerData,
            _Out_ LPWSABUF lpCalleeData,
            _In_  LPQOS lpSQOS,
            _In_  LPQOS lpGQOS
    )
    {
        return connect_mode_hook(
                WSAConnect_f,
                "WSAConnect",
                s,
                name,
                namelen,
                lpCallerData,
                lpCalleeData,
                lpSQOS,
                lpGQOS
        );
    }

    enum e_mode_hook_flags
    {
        e_nonblocking_op = 0x1,
        e_no_timeout     = 0x1 << 1,
    };

    template<typename R, typename OriginF, typename ... Args>
    static R
    read_mode_hook(OriginF fn, const char *fn_name, int flags, SOCKET s, Args &&... args)
    {
        Task *tk            = g_Scheduler.GetCurrentTask();
        bool is_nonblocking = IsNonblocking(s);
        DebugPrint(dbg_hook, "task(%s) Hook %s(s=%d)(nonblocking:%d)(flags:%d).",
                   tk ? tk->DebugInfo() : "nil", fn_name, (int) s, (int) is_nonblocking, (int) flags);
        if (!tk || is_nonblocking || (flags & e_nonblocking_op))
        {
            return fn(s, std::forward<Args>(args)...);
        }

        // async WSARecv
        if (!SetNonblocking(s, true))
        {
            return fn(s, std::forward<Args>(args)...);
        }

        R ret = fn(s, std::forward<Args>(args)...);
        if ((int) ret != -1)
        {    // accept����SOCKET���ͣ����޷������������Դ˴������ж�С��0.
            SetNonblocking(s, false);
            return ret;
        }

        // If connection is closed, the Bytes will setted 0, and ret is 0, and WSAGetLastError() returns 0.
        int err = WSAGetLastError();
        if (WSAEWOULDBLOCK != err && WSAEINPROGRESS != err)
        {
            SetNonblocking(s, false);
            WSASetLastError(err);
            return ret;
        }

        // wait data arrives.
        int timeout = 0;
        if (!(flags & e_no_timeout))
        {
            int timeoutlen = sizeof(timeout);
            getsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout, &timeoutlen);
        }

        timeval tm{timeout / 1000, timeout % 1000 * 1000};
        fd_set  rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        select(1, &rfds, NULL, NULL, timeout ? &tm : NULL);

        ret = fn(s, std::forward<Args>(args)...);
        err = WSAGetLastError();
        SetNonblocking(s, false);
        WSASetLastError(err);
        return ret;
    }

    typedef SOCKET (WINAPI *accept_t)(
            _In_    SOCKET s,
            _Out_   struct sockaddr *addr,
            _Inout_ int *addrlen
    );

    static accept_t accept_f = NULL;//(accept_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "accept");

    static SOCKET WINAPI
    hook_accept(
            _In_    SOCKET s,
            _Out_   struct sockaddr *addr,
            _Inout_ int *addrlen
    )
    {
        return read_mode_hook<SOCKET>(accept_f, "accept", e_no_timeout, s, addr, addrlen);
    }

    typedef SOCKET (WINAPI *WSAAccept_t)(
            _In_    SOCKET s,
            _Out_   struct sockaddr *addr,
            _Inout_ LPINT addrlen,
            _In_    LPCONDITIONPROC lpfnCondition,
            _In_    DWORD_PTR dwCallbackData
    );

    static WSAAccept_t WSAAccept_f = NULL;//(WSAAccept_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSAAccept");

    static SOCKET WINAPI
    hook_WSAAccept(
            _In_    SOCKET s,
            _Out_   struct sockaddr *addr,
            _Inout_ LPINT addrlen,
            _In_    LPCONDITIONPROC lpfnCondition,
            _In_    DWORD_PTR dwCallbackData
    )
    {
        return read_mode_hook<SOCKET>(
                WSAAccept_f,
                "WSAAccept",
                e_no_timeout,
                s,
                addr,
                addrlen,
                lpfnCondition,
                dwCallbackData
        );
    }

    typedef int ( WINAPI *WSARecv_t)(
            _In_    SOCKET s,
            _Inout_ LPWSABUF lpBuffers,
            _In_    DWORD dwBufferCount,
            _Out_   LPDWORD lpNumberOfBytesRecvd,
            _Inout_ LPDWORD lpFlags,
            _In_    LPWSAOVERLAPPED lpOverlapped,
            _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    );

    static WSARecv_t WSARecv_f = NULL;//(WSARecv_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSARecv");

    static int WINAPI
    hook_WSARecv(
            _In_    SOCKET s,
            _Inout_ LPWSABUF lpBuffers,
            _In_    DWORD dwBufferCount,
            _Out_   LPDWORD lpNumberOfBytesRecvd,
            _Inout_ LPDWORD lpFlags,
            _In_    LPWSAOVERLAPPED lpOverlapped,
            _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    )
    {
        return read_mode_hook<int>(
                WSARecv_f, "WSARecv", lpOverlapped ? e_nonblocking_op : 0, s,
                lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpOverlapped, lpCompletionRoutine
        );
    }

    typedef int ( WINAPI *recv_t)(
            _In_  SOCKET s,
            _Out_ char *buf,
            _In_  int len,
            _In_  int flags
    );

    static recv_t recv_f = NULL;//(recv_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "recv");

    static int WINAPI
    hook_recv(
            _In_  SOCKET s,
            _Out_ char *buf,
            _In_  int len,
            _In_  int flags
    )
    {
        return read_mode_hook<int>(recv_f, "recv", 0, s, buf, len, flags);
    }

    typedef int ( WINAPI *recvfrom_t)(
            _In_        SOCKET s,
            _Out_       char *buf,
            _In_        int len,
            _In_        int flags,
            _Out_       struct sockaddr *from,
            _Inout_opt_ int *fromlen
    );

    static recvfrom_t recvfrom_f = NULL;//(recvfrom_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "recvfrom");

    static int WINAPI
    hook_recvfrom(
            _In_        SOCKET s,
            _Out_       char *buf,
            _In_        int len,
            _In_        int flags,
            _Out_       struct sockaddr *from,
            _Inout_opt_ int *fromlen
    )
    {
        return read_mode_hook<int>(recvfrom_f, "recvfrom", 0, s, buf, len, flags, from, fromlen);
    }

    typedef int ( WINAPI *WSARecvFrom_t)(
            _In_    SOCKET s,
            _Inout_ LPWSABUF lpBuffers,
            _In_    DWORD dwBufferCount,
            _Out_   LPDWORD lpNumberOfBytesRecvd,
            _Inout_ LPDWORD lpFlags,
            _Out_   struct sockaddr *lpFrom,
            _Inout_ LPINT lpFromlen,
            _In_    LPWSAOVERLAPPED lpOverlapped,
            _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    );

    static WSARecvFrom_t
            WSARecvFrom_f = NULL;//(WSARecvFrom_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSARecvFrom");

    static int WINAPI
    hook_WSARecvFrom(
            _In_    SOCKET s,
            _Inout_ LPWSABUF lpBuffers,
            _In_    DWORD dwBufferCount,
            _Out_   LPDWORD lpNumberOfBytesRecvd,
            _Inout_ LPDWORD lpFlags,
            _Out_   struct sockaddr *lpFrom,
            _Inout_ LPINT lpFromlen,
            _In_    LPWSAOVERLAPPED lpOverlapped,
            _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    )
    {
        return read_mode_hook<int>(
                WSARecvFrom_f,
                "WSARecvFrom",
                lpOverlapped ? e_nonblocking_op : 0,
                s,
                lpBuffers,
                dwBufferCount,
                lpNumberOfBytesRecvd,
                lpFlags,
                lpFrom,
                lpFromlen,
                lpOverlapped,
                lpCompletionRoutine
        );
    }

    typedef int ( WINAPI *WSARecvMsg_t)(
            _In_    SOCKET s,
            _Inout_ LPWSAMSG lpMsg,
            _Out_   LPDWORD lpdwNumberOfBytesRecvd,
            _In_    LPWSAOVERLAPPED lpOverlapped,
            _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    );

    static WSARecvMsg_t
            WSARecvMsg_f = NULL;//(WSARecvMsg_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSARecvMsg");

    static int WINAPI
    hook_WSARecvMsg(
            _In_    SOCKET s,
            _Inout_ LPWSAMSG lpMsg,
            _Out_   LPDWORD lpdwNumberOfBytesRecvd,
            _In_    LPWSAOVERLAPPED lpOverlapped,
            _In_    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    )
    {
        return read_mode_hook<int>(
                WSARecvMsg_f, "WSARecvMsg", lpOverlapped ? e_nonblocking_op : 0,
                s, lpMsg, lpdwNumberOfBytesRecvd, lpOverlapped, lpCompletionRoutine
        );
    }

    template<typename R, typename OriginF, typename ... Args>
    static R
    write_mode_hook(OriginF fn, const char *fn_name, int flags, SOCKET s, Args &&... args)
    {
        Task *tk            = g_Scheduler.GetCurrentTask();
        bool is_nonblocking = IsNonblocking(s);
        DebugPrint(dbg_hook, "task(%s) Hook %s(s=%d)(nonblocking:%d)(flags:%d).",
                   tk ? tk->DebugInfo() : "nil", fn_name, (int) s, (int) is_nonblocking, (int) flags);
        if (!tk || is_nonblocking || (flags & e_nonblocking_op))
        {
            return fn(s, std::forward<Args>(args)...);
        }

        // async WSARecv
        if (!SetNonblocking(s, true))
        {
            return fn(s, std::forward<Args>(args)...);
        }

        R ret = fn(s, std::forward<Args>(args)...);
        if (ret != -1)
        {
            SetNonblocking(s, false);
            return ret;
        }

        // If connection is closed, the Bytes will setted 0, and ret is 0, and WSAGetLastError() returns 0.
        int err = WSAGetLastError();
        if (WSAEWOULDBLOCK != err && WSAEINPROGRESS != err)
        {
            SetNonblocking(s, false);
            WSASetLastError(err);
            return ret;
        }

        // wait data arrives.
        int timeout = 0;
        if (!(flags & e_no_timeout))
        {
            int timeoutlen = sizeof(timeout);
            getsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char *) &timeout, &timeoutlen);
        }

        timeval tm{timeout / 1000, timeout % 1000 * 1000};
        fd_set  wfds;
        FD_ZERO(&wfds);
        FD_SET(s, &wfds);
        select(1, NULL, &wfds, NULL, timeout ? &tm : NULL);

        ret = fn(s, std::forward<Args>(args)...);
        err = WSAGetLastError();
        SetNonblocking(s, false);
        WSASetLastError(err);
        return ret;
    }

    typedef int ( WINAPI *WSASend_t)(
            _In_  SOCKET s,
            _In_  LPWSABUF lpBuffers,
            _In_  DWORD dwBufferCount,
            _Out_ LPDWORD lpNumberOfBytesSent,
            _In_  DWORD dwFlags,
            _In_  LPWSAOVERLAPPED lpOverlapped,
            _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    );

    static WSASend_t WSASend_f = NULL;//(WSASend_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASend");

    static int WINAPI
    hook_WSASend(
            _In_  SOCKET s,
            _In_  LPWSABUF lpBuffers,
            _In_  DWORD dwBufferCount,
            _Out_ LPDWORD lpNumberOfBytesSent,
            _In_  DWORD dwFlags,
            _In_  LPWSAOVERLAPPED lpOverlapped,
            _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    )
    {
        return write_mode_hook<int>(
                WSASend_f, "WSASend", lpOverlapped ? e_nonblocking_op : 0,
                s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpOverlapped, lpCompletionRoutine
        );
    }

    typedef int ( WINAPI *send_t)(
            _In_       SOCKET s,
            _In_ const char *buf,
            _In_       int len,
            _In_       int flags
    );

    static send_t send_f = NULL;//(send_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "send");

    static int WINAPI
    hook_send(
            _In_       SOCKET s,
            _In_ const char *buf,
            _In_       int len,
            _In_       int flags
    )
    {
        return write_mode_hook<int>(send_f, "send", 0, s, buf, len, flags);
    }

    typedef int ( WINAPI *sendto_t)(
            _In_       SOCKET s,
            _In_ const char *buf,
            _In_       int len,
            _In_       int flags,
            _In_       const struct sockaddr *to,
            _In_       int tolen
    );

    static sendto_t sendto_f = NULL;//(sendto_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "sendto");

    static int WINAPI
    hook_sendto(
            _In_       SOCKET s,
            _In_ const char *buf,
            _In_       int len,
            _In_       int flags,
            _In_       const struct sockaddr *to,
            _In_       int tolen
    )
    {
        return write_mode_hook<int>(sendto_f, "sendto", 0, s, buf, len, flags, to, tolen);
    }

    typedef int ( WINAPI *WSASendTo_t)(
            _In_  SOCKET s,
            _In_  LPWSABUF lpBuffers,
            _In_  DWORD dwBufferCount,
            _Out_ LPDWORD lpNumberOfBytesSent,
            _In_  DWORD dwFlags,
            _In_  const struct sockaddr *lpTo,
            _In_  int iToLen,
            _In_  LPWSAOVERLAPPED lpOverlapped,
            _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    );

    static WSASendTo_t WSASendTo_f = NULL;//(WSASendTo_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASendTo");

    static int WINAPI
    hook_WSASendTo(
            _In_  SOCKET s,
            _In_  LPWSABUF lpBuffers,
            _In_  DWORD dwBufferCount,
            _Out_ LPDWORD lpNumberOfBytesSent,
            _In_  DWORD dwFlags,
            _In_  const struct sockaddr *lpTo,
            _In_  int iToLen,
            _In_  LPWSAOVERLAPPED lpOverlapped,
            _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    )
    {
        return write_mode_hook<int>(
                WSASendTo_f,
                "WSASendTo",
                lpOverlapped ? e_nonblocking_op : 0,
                s,
                lpBuffers,
                dwBufferCount,
                lpNumberOfBytesSent,
                dwFlags,
                lpTo,
                iToLen,
                lpOverlapped,
                lpCompletionRoutine
        );
    }

    typedef int ( WINAPI *WSASendMsg_t)(
            _In_  SOCKET s,
            _In_  LPWSAMSG lpMsg,
            _In_  DWORD dwFlags,
            _Out_ LPDWORD lpNumberOfBytesSent,
            _In_  LPWSAOVERLAPPED lpOverlapped,
            _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    );

    static WSASendMsg_t
            WSASendMsg_f = NULL;//(WSASendMsg_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASendMsg");

    static int WINAPI
    hook_WSASendMsg(
            _In_  SOCKET s,
            _In_  LPWSAMSG lpMsg,
            _In_  DWORD dwFlags,
            _Out_ LPDWORD lpNumberOfBytesSent,
            _In_  LPWSAOVERLAPPED lpOverlapped,
            _In_  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    )
    {
        return write_mode_hook<int>(
                WSASendMsg_f, "WSASendMsg", lpOverlapped ? e_nonblocking_op : 0,
                s, lpMsg, dwFlags, lpNumberOfBytesSent, lpOverlapped, lpCompletionRoutine
        );
    }


    typedef VOID (WINAPI *Sleep_t)(
            _In_ DWORD dwMilliseconds
    );

    static Sleep_t Sleep_f = NULL;

    VOID WINAPI hook_Sleep(
            _In_ DWORD dwMilliseconds
    )
    {
        Task* tk = g_Scheduler.GetCurrentTask();

        if (!tk)
            return Sleep_f(dwMilliseconds);

        g_Scheduler.SleepSwitch((int)dwMilliseconds);
    }


    struct MH_INIT
    {
        MH_INIT()
        {
            if (MH_Initialize() != MH_OK)
            {
                fprintf(stderr, "Hook Initialize failed!");
            }
        }

        ~MH_INIT()
        {
            if (MH_Uninitialize() != MH_OK)
            {
                fprintf(stderr, "Hook Uninitialize failed!");
            }
        }
    } init;

    template<typename D, typename O>
    inline MH_STATUS
    MH_CreateHookEx(LPVOID pTarget, D pDetour, O **ppOriginal)
    {
        return MH_CreateHook(pTarget, reinterpret_cast<LPVOID>(pDetour), reinterpret_cast<LPVOID *>(ppOriginal));
    }

    template<typename D, typename O>
    inline MH_STATUS
    MH_CreateHookApiEx(
            LPCWSTR pszModule, LPCSTR pszProcName, D pDetour, O **ppOriginal
    )
    {
        return MH_CreateHookApi(
                pszModule, pszProcName, reinterpret_cast<LPVOID>(pDetour), reinterpret_cast<LPVOID *>(ppOriginal));
    }


    void
    coroutine_hook_init()
    {
        BOOL ok = true;

        // ioctlsocket and select functions.
        //ioctlsocket_f = (ioctlsocket_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "ioctlsocket");
        ok &= MH_CreateHookApiEx(L"ws2_32", "ioctlsocket", &hook_ioctlsocket, &ioctlsocket_f) == MH_OK;
        //select_f = (select_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "select");
        ok &= MH_CreateHookApiEx(L"ws2_32", "select", &hook_select, &select_f) == MH_OK;
        //WSAIoctl_f = (WSAIoctl_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSAIoctl");
        ok &= MH_CreateHookApiEx(L"ws2_32", "WSAIoctl", &hook_WSAIoctl, &WSAIoctl_f) == MH_OK;


        // connect-like functions
        //connect_f = (connect_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "connect");
        ok &= MH_CreateHookApiEx(L"ws2_32", "connect", (LPVOID) &hook_connect, &connect_f) == MH_OK;
        //static WSAConnect_t WSAConnect_f = (WSAConnect_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSAConnect");
        ok &= MH_CreateHookApiEx(L"ws2_32", "WSAConnect", &hook_WSAConnect, &WSAConnect_f) == MH_OK;


        // accept-like functions
        //accept_f = (accept_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "accept");
        ok &= MH_CreateHookApiEx(L"ws2_32", "accept", &hook_accept, &accept_f) == MH_OK;
        //WSAAccept_f = (WSAAccept_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSAAccept");
        ok &= MH_CreateHookApiEx(L"ws2_32", "WSAAccept", &hook_WSAAccept, &WSAAccept_f) == MH_OK;

        // recv-like functions
        //recv_f = (recv_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "recv");
        ok &= MH_CreateHookApiEx(L"ws2_32", "recv", &hook_recv, &recv_f) == MH_OK;
        //recvfrom_f = (recvfrom_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "recvfrom");
        ok &= MH_CreateHookApiEx(L"ws2_32", "recvfrom", &hook_recvfrom, &recvfrom_f) == MH_OK;
        //WSARecv_f = (WSARecv_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSARecv");
        ok &= MH_CreateHookApiEx(L"ws2_32", "WSARecv", &hook_WSARecv, &WSARecv_f) == MH_OK;
        //WSARecvFrom_f = (WSARecvFrom_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSARecvFrom");
        ok &= MH_CreateHookApiEx(L"ws2_32", "WSARecvFrom", &hook_WSARecvFrom, &WSARecvFrom_f) == MH_OK;
        //WSARecvMsg_f = (WSARecvMsg_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSARecvMsg");
        if (GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSARecvMsg"))
        {
            ok &= MH_CreateHookApiEx(L"ws2_32", "WSARecvMsg", &hook_WSARecvMsg, &WSARecvMsg_f) == MH_OK;
        }

        // send-like functions
        //send_f = (send_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "send");
        ok &= MH_CreateHookApiEx(L"ws2_32", "send", &hook_send, &send_f) == MH_OK;
        //sendto_f = (sendto_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "sendto");
        ok &= MH_CreateHookApiEx(L"ws2_32", "sendto", &hook_sendto, &sendto_f) == MH_OK;
        //WSASend_f = (WSASend_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASend");
        ok &= MH_CreateHookApiEx(L"ws2_32", "WSASend", &hook_WSASend, &WSASend_f) == MH_OK;
        //WSASendTo_f = (WSASendTo_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASendTo");
        ok &= MH_CreateHookApiEx(L"ws2_32", "WSASendTo", &hook_WSASendTo, &WSASendTo_f) == MH_OK;
        //WSASendMsg_f = (WSASendMsg_t)GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASendMsg");
        if (GetProcAddress(GetModuleHandleA("Ws2_32.dll"), "WSASendMsg"))
        {
            ok &= MH_CreateHookApiEx(L"ws2_32", "WSASendMsg", &hook_WSASendMsg, &WSASendMsg_f) == MH_OK;
        }

        //Sleep
        ok &= MH_CreateHookApiEx(L"kernel32", "Sleep", &hook_Sleep, &Sleep_f) == MH_OK;

        ok &= MH_EnableHook(MH_ALL_HOOKS) == MH_OK;


        if (!ok)
        {
            fprintf(stderr, "Hook failed!");
            exit(1);
        }

    }

} //namespace co