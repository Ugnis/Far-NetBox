/*
 * winproxy.c: Windows implementation of platform_new_connection(),
 * supporting an OpenSSH-like proxy command via the winhandl.c
 * mechanism.
 */

#include <stdio.h>
#include <assert.h>

#define DEFINE_PLUG_METHOD_MACROS
#include "tree234.h"
#include "putty.h"
#include "network.h"
#include "proxy.h"

Socket make_handle_socket(HANDLE send_H, HANDLE recv_H, HANDLE stderr_H,
                          Plug plug, int overlapped);

typedef struct Socket_localproxy_tag *Local_Proxy_Socket;

#ifdef MPEXT
extern char *do_select(Plug plug, SOCKET skt, int startup);
#endif

struct Socket_localproxy_tag {
    const struct socket_function_table *fn;
    /* the above variable absolutely *must* be the first in this structure */

    HANDLE to_cmd_H, from_cmd_H;
    struct handle *to_cmd_h, *from_cmd_h;

    char *error;

    Plug plug;

    void *privptr;
};

int localproxy_gotdata(struct handle *h, void *data, int len)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) handle_get_privdata(h);

    if (len < 0) {
	return plug_closing(ps->plug, "Read error from local proxy command",
			    0, 0);
    } else if (len == 0) {
	return plug_closing(ps->plug, NULL, 0, 0);
    } else {
	return plug_receive(ps->plug, 0, data, len);
    }
}

void localproxy_sentdata(struct handle *h, int new_backlog)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) handle_get_privdata(h);
    
    plug_sent(ps->plug, new_backlog);
}

static Plug sk_localproxy_plug (Socket s, Plug p)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) s;
    Plug ret = ps->plug;
    if (p)
	ps->plug = p;
    return ret;
}

static void sk_localproxy_close (Socket s)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) s;

    #ifdef MPEXT
    // WinSCP core uses do_select as signalization of connection up/down
    do_select(ps->plug, INVALID_SOCKET, 0);
    #endif

    handle_free(ps->to_cmd_h);
    handle_free(ps->from_cmd_h);
    CloseHandle(ps->to_cmd_H);
    CloseHandle(ps->from_cmd_H);

    sfree(ps);
}

static int sk_localproxy_write (Socket s, const char *data, int len)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) s;

    return handle_write(ps->to_cmd_h, data, len);
}

static int sk_localproxy_write_oob(Socket s, const char *data, int len)
{
    /*
     * oob data is treated as inband; nasty, but nothing really
     * better we can do
     */
    return sk_localproxy_write(s, data, len);
}

static void sk_localproxy_write_eof(Socket s)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) s;

    handle_write_eof(ps->to_cmd_h);
}

static void sk_localproxy_flush(Socket s)
{
    /* Local_Proxy_Socket ps = (Local_Proxy_Socket) s; */
    /* do nothing */
}

static void sk_localproxy_set_private_ptr(Socket s, void *ptr)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) s;
    ps->privptr = ptr;
}

static void *sk_localproxy_get_private_ptr(Socket s)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) s;
    return ps->privptr;
}

static void sk_localproxy_set_frozen(Socket s, int is_frozen)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) s;

    /*
     * FIXME
     */
}

static const char *sk_localproxy_socket_error(Socket s)
{
    Local_Proxy_Socket ps = (Local_Proxy_Socket) s;
    return ps->error;
}

Socket platform_new_connection(SockAddr addr, const char *hostname,
			       int port, int privport,
			       int oobinline, int nodelay, int keepalive,
			       Plug plug, Conf *conf)
{
    char *cmd;
    HANDLE us_to_cmd, cmd_from_us;
    HANDLE us_from_cmd, cmd_to_us;
    HANDLE us_from_cmd_err, cmd_err_to_us;
    SECURITY_ATTRIBUTES sa;
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    if (conf_get_int(conf, CONF_proxy_type) != PROXY_CMD)
	return NULL;

    cmd = format_telnet_command(addr, port, conf);

    /* We are responsible for this and don't need it any more */
    sk_addr_free(addr);

    {
	char *msg = dupprintf("Starting local proxy command: %s", cmd);
	plug_log(plug, 2, NULL, 0, msg, 0);
	sfree(msg);
    }

    /*
     * Create the pipes to the proxy command, and spawn the proxy
     * command process.
     */
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;    /* default */
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&us_from_cmd, &cmd_to_us, &sa, 0)) {
	Socket ret =
            new_error_socket("Unable to create pipes for proxy command", plug);
        sfree(cmd);
	return ret;
    }

    if (!CreatePipe(&cmd_from_us, &us_to_cmd, &sa, 0)) {
	Socket ret =
            new_error_socket("Unable to create pipes for proxy command", plug);
        sfree(cmd);
	CloseHandle(us_from_cmd);
	CloseHandle(cmd_to_us);
	return ret;
    }

    if (flags & FLAG_STDERR) {
        /* If we have a sensible stderr, the proxy command can send
         * its own standard error there, so we won't interfere. */
        us_from_cmd_err = cmd_err_to_us = NULL;
    } else {
        /* If we don't have a sensible stderr, we should catch the
         * proxy command's standard error to put in our event log. */
        if (!CreatePipe(&us_from_cmd_err, &cmd_err_to_us, &sa, 0)) {
            Socket ret = new_error_socket
                ("Unable to create pipes for proxy command", plug);
            sfree(cmd);
            return ret;
        }
    }

    SetHandleInformation(us_to_cmd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(us_from_cmd, HANDLE_FLAG_INHERIT, 0);
    if (us_from_cmd_err != NULL)
        SetHandleInformation(us_from_cmd_err, HANDLE_FLAG_INHERIT, 0);

    si.cb = sizeof(si);
    si.lpReserved = NULL;
    si.lpDesktop = NULL;
    si.lpTitle = NULL;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.cbReserved2 = 0;
    si.lpReserved2 = NULL;
    si.hStdInput = cmd_from_us;
    si.hStdOutput = cmd_to_us;
    si.hStdError = cmd_err_to_us;
    CreateProcess(NULL, cmd, NULL, NULL, TRUE,
		  CREATE_NO_WINDOW | NORMAL_PRIORITY_CLASS,
		  NULL, NULL, &si, &pi);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    sfree(cmd);

    CloseHandle(cmd_from_us);
    CloseHandle(cmd_to_us);

    if (cmd_err_to_us != NULL)
        CloseHandle(cmd_err_to_us);

    return make_handle_socket(us_to_cmd, us_from_cmd, us_from_cmd_err,
                              plug, FALSE);
}
