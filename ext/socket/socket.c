/************************************************

  socket.c -

  $Author: matz $
  $Date: 1995/01/10 10:42:55 $
  created at: Thu Mar 31 12:21:29 JST 1994

************************************************/

#include "ruby.h"
#include "io.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

extern VALUE cIO;
extern VALUE cInteger;

VALUE cBasicSocket;
VALUE cTCPsocket;
VALUE cTCPserver;
#ifdef AF_UNIX
VALUE cUNIXsocket;
VALUE cUNIXserver;
#endif
VALUE cSocket;

extern VALUE eException;
static VALUE eSocket;

FILE *rb_fdopen();
char *strdup();

#ifdef NT
static void
sock_finalize(fptr)
    OpenFile *fptr;
{
    SOCKET s = fileno(fptr->f);
    free(fptr->f);
    free(fptr->f2);
    closesocket(s);
}
#endif

static VALUE
sock_new(class, fd)
    VALUE class;
    int fd;
{
    OpenFile *fp;
    NEWOBJ(sock, struct RFile);
    OBJSETUP(sock, class, T_FILE);

    MakeOpenFile(sock, fp);
#ifdef NT
    fp->finalize = sock_finalize;
#endif
    fp->f = rb_fdopen(fd, "r");
    setbuf(fp->f, NULL);
    fp->f2 = rb_fdopen(fd, "w");
    fp->mode = FMODE_READWRITE|FMODE_SYNC;

    return (VALUE)sock;
}

static VALUE
bsock_shutdown(argc, argv, sock)
    int argc;
    VALUE *argv;
    VALUE sock;
{
    VALUE howto;
    int how;
    OpenFile *fptr;

    rb_scan_args(argc, argv, "01", &howto);
    if (howto == Qnil)
	how = 2;
    else {
	how = NUM2INT(howto);
	if (how < 0 && how > 2) how = 2;
    }
    GetOpenFile(sock, fptr);
    if (shutdown(fileno(fptr->f), how) == -1)
	rb_sys_fail(0);

    return INT2FIX(0);
}

static VALUE
bsock_setsockopt(sock, lev, optname, val)
    VALUE sock, lev, optname;
    struct RString *val;
{
    int level, option;
    OpenFile *fptr;
    int i;
    char *v;
    int vlen;

    level = NUM2INT(lev);
    option = NUM2INT(optname);
    switch (TYPE(val)) {
      case T_FIXNUM:
	i = FIX2INT(val);
	goto numval;
      case T_FALSE:
	i = 0;
	goto numval;
      case T_TRUE:
	i = 1;
      numval:
	v = (char*)&i; vlen = sizeof(i);
	break;
      default:
	Check_Type(val, T_STRING);
	v = val->ptr; vlen = val->len;
    }

    GetOpenFile(sock, fptr);
    if (setsockopt(fileno(fptr->f), level, option, v, vlen) < 0)
	rb_sys_fail(fptr->path);

    return INT2FIX(0);
}

static VALUE
bsock_getsockopt(sock, lev, optname)
    VALUE sock, lev, optname;
{
#if !defined(__CYGWIN32__)
    int level, option, len;
    struct RString *val;
    OpenFile *fptr;

    level = NUM2INT(lev);
    option = NUM2INT(optname);
    len = 256;
    val = (struct RString*)str_new(0, len);
    Check_Type(val, T_STRING);

    GetOpenFile(sock, fptr);
    if (getsockopt(fileno(fptr->f), level, option, val->ptr, &len) < 0)
	rb_sys_fail(fptr->path);
    val->len = len;

    return (VALUE)val;
#else
    rb_notimplement();
#endif
}

static VALUE
bsock_getsockname(sock)
   VALUE sock;
{
    char buf[1024];
    int len = sizeof buf;
    OpenFile *fptr;

    GetOpenFile(sock, fptr);
    if (getsockname(fileno(fptr->f), (struct sockaddr*)buf, &len) < 0)
	rb_sys_fail("getsockname(2)");
    return str_new(buf, len);
}

static VALUE
bsock_getpeername(sock)
   VALUE sock;
{
    char buf[1024];
    int len = sizeof buf;
    OpenFile *fptr;

    GetOpenFile(sock, fptr);
    if (getpeername(fileno(fptr->f), (struct sockaddr*)buf, &len) < 0)
	rb_sys_fail("getpeername(2)");
    return str_new(buf, len);
}

static VALUE
open_inet(class, h, serv, server)
    VALUE class, h, serv;
    int server;
{
    char *host;
    struct hostent *hostent, _hostent;
    struct servent *servent, _servent;
    struct protoent *protoent;
    struct sockaddr_in sockaddr;
    int fd, status;
    int hostaddr, hostaddrPtr[2];
    int servport;
    char *syscall;
    VALUE sock;

    if (h) {
	Check_Type(h, T_STRING);
	host = RSTRING(h)->ptr;
	hostent = gethostbyname(host);
	if (hostent == NULL) {
	    hostaddr = inet_addr(host);
	    if (hostaddr == -1) {
		if (server && !strlen(host))
		    hostaddr = INADDR_ANY;
		else {
#ifdef HAVE_HSTRERROR
		    extern int h_errno;
		    Raise(eSocket, (char *)hstrerror(h_errno));
#else
		    Raise(eSocket, "host not found");
#endif
		}
	    }
	    _hostent.h_addr_list = (char **)hostaddrPtr;
	    _hostent.h_addr_list[0] = (char *)&hostaddr;
	    _hostent.h_addr_list[1] = NULL;
	    _hostent.h_length = sizeof(hostaddr);
	    _hostent.h_addrtype = AF_INET;
	    hostent = &_hostent;
	}
    }
    servent = NULL;
    if (FIXNUM_P(serv)) {
	servport = FIX2UINT(serv);
	goto setup_servent;
    }
    Check_Type(serv, T_STRING);
    servent = getservbyname(RSTRING(serv)->ptr, "tcp");
    if (servent == NULL) {
	servport = strtoul(RSTRING(serv)->ptr, 0, 0);
	if (servport == -1) {
	    Raise(eSocket, "no such servce %s", RSTRING(serv)->ptr);
	}
      setup_servent:
	_servent.s_port = htons(servport);
 	_servent.s_proto = "tcp";
	servent = &_servent;
    }
    protoent = getprotobyname(servent->s_proto);
    if (protoent == NULL) {
	Raise(eSocket, "no such proto %s", servent->s_proto);
    }

    fd = socket(PF_INET, SOCK_STREAM, protoent->p_proto);

    sockaddr.sin_family = AF_INET;
    if (h) {
	memcpy((char *)&(sockaddr.sin_addr.s_addr),
	       (char *) hostent->h_addr_list[0],
	       (size_t) hostent->h_length);
    }
    else {
	sockaddr.sin_addr.s_addr = INADDR_ANY;
    }
    sockaddr.sin_port = servent->s_port;

    if (server) {
	status = bind(fd, (struct sockaddr*)&sockaddr, sizeof(sockaddr));
	syscall = "bind(2)";
    }
    else {
        status = connect(fd, (struct sockaddr*)&sockaddr, sizeof(sockaddr));
	syscall = "connect(2)";
    }

    if (status < 0) {
	close (fd);
	rb_sys_fail(syscall);
    }
    if (server) listen(fd, 5);

    /* create new instance */
    sock = sock_new(class, fd);

    return sock;
}

static VALUE
tcp_s_open(class, host, serv)
    VALUE class, host, serv;
{
    Check_Type(host, T_STRING);
    return open_inet(class, host, serv, 0);
}

static VALUE
tcp_svr_s_open(argc, argv, class)
    int argc;
    VALUE *argv;
    VALUE class;
{
    VALUE arg1, arg2;

    if (rb_scan_args(argc, argv, "11", &arg1, &arg2) == 2)
	return open_inet(class, arg1, arg2, 1);
    else
	return open_inet(class, 0, arg1, 1);
}

static VALUE
s_accept(class, fd, sockaddr, len)
    VALUE class;
    int fd;
    struct sockaddr *sockaddr;
    int *len;
{
    int fd2;

  retry:
#ifdef THREAD
    thread_wait_fd(fd);
#endif
    TRAP_BEG;
    fd2 = accept(fd, sockaddr, len);
    TRAP_END;
    if (fd2 < 0) {
	if (errno == EINTR) goto retry;
	rb_sys_fail(0);
    }
    return sock_new(class, fd2);
}

static VALUE
tcp_accept(sock)
    VALUE sock;
{
    OpenFile *fptr;
    struct sockaddr_in from;
    int fromlen;

    GetOpenFile(sock, fptr);
    fromlen = sizeof(struct sockaddr_in);
    return s_accept(cTCPsocket, fileno(fptr->f),
		    (struct sockaddr*)&from, &fromlen);
}

#ifdef HAVE_SYS_UN_H
static VALUE
open_unix(class, path, server)
    VALUE class;
    struct RString *path;
    int server;
{
    struct sockaddr_un sockaddr;
    int fd, status;
    char *syscall;
    VALUE sock;
    OpenFile *fptr;

    Check_Type(path, T_STRING);
    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) rb_sys_fail("socket(2)");

    sockaddr.sun_family = AF_UNIX;
    strncpy(sockaddr.sun_path, path->ptr, sizeof(sockaddr.sun_path)-1);
    sockaddr.sun_path[sizeof(sockaddr.sun_path)-1] = '\0';

    if (server) {
        status = bind(fd, (struct sockaddr*)&sockaddr, sizeof(sockaddr));
	syscall = "bind(2)";
    }
    else {
        status = connect(fd, (struct sockaddr*)&sockaddr, sizeof(sockaddr));
	syscall = "connect(2)";
    }

    if (status < 0) {
	close (fd);
	rb_sys_fail(syscall);
    }

    if (server) listen(fd, 5);

    sock = sock_new(class, fd);
    GetOpenFile(sock, fptr);
    fptr->path = strdup(path->ptr);

    return sock;
}
#endif

static void
setipaddr(name, addr)
    char *name;
    struct sockaddr_in *addr;
{
    int d1, d2, d3, d4;
    char ch;
    struct hostent *hp;
    long x;
    unsigned char *a;
    char buf[16];

    if (name[0] == 0) {
	addr->sin_addr.s_addr = INADDR_ANY;
    }
    else if (name[0] == '<' && strcmp(name, "<broadcast>") == 0) {
	addr->sin_addr.s_addr = INADDR_BROADCAST;
    }
    else if (sscanf(name, "%d.%d.%d.%d%c", &d1, &d2, &d3, &d4, &ch) == 4 &&
	     0 <= d1 && d1 <= 255 && 0 <= d2 && d2 <= 255 &&
	     0 <= d3 && d3 <= 255 && 0 <= d4 && d4 <= 255) {
	addr->sin_addr.s_addr = htonl(
	    ((long) d1 << 24) | ((long) d2 << 16) |
	    ((long) d3 << 8) | ((long) d4 << 0));
    }
    else {
	hp = gethostbyname(name);
	if (!hp) {
#ifdef HAVE_HSTRERROR
	    extern int h_errno;
	    Raise(eSocket, (char *)hstrerror(h_errno));
#else
	    Raise(eSocket, "host not found");
#endif
	}
	memcpy((char *) &addr->sin_addr, hp->h_addr, hp->h_length);
    }
}

static VALUE
mkipaddr(x)
    unsigned long x;
{
    char buf[16];

    x = ntohl(x);
    sprintf(buf, "%d.%d.%d.%d",
	    (int) (x>>24) & 0xff, (int) (x>>16) & 0xff,
	    (int) (x>> 8) & 0xff, (int) (x>> 0) & 0xff);
    return str_new2(buf);
}

static VALUE
tcpaddr(sockaddr)
    struct sockaddr_in *sockaddr;
{
    VALUE family, port, addr1, addr2;
    VALUE ary;
    struct hostent *hostent;

    family = str_new2("AF_INET");
    hostent = gethostbyaddr((char*)&sockaddr->sin_addr.s_addr,
			    sizeof(sockaddr->sin_addr),
			    AF_INET);
    addr1 = 0;
    if (hostent) {
	addr1 = str_new2(hostent->h_name);
    }
    addr2 = mkipaddr(sockaddr->sin_addr.s_addr);
    if (!addr1) addr1 = addr2;

    port = INT2FIX(ntohs(sockaddr->sin_port));
    ary = ary_new3(4, family, port, addr1, addr2);

    return ary;
}

static VALUE
tcp_addr(sock)
    VALUE sock;
{
    OpenFile *fptr;
    struct sockaddr_in addr;
    int len = sizeof addr;

    GetOpenFile(sock, fptr);

    if (getsockname(fileno(fptr->f), (struct sockaddr*)&addr, &len) < 0)
	rb_sys_fail("getsockname(2)");
    return tcpaddr(&addr);
}

static VALUE
tcp_peeraddr(sock)
    VALUE sock;
{
    OpenFile *fptr;
    struct sockaddr_in addr;
    int len = sizeof addr;

    GetOpenFile(sock, fptr);

    if (getpeername(fileno(fptr->f), (struct sockaddr*)&addr, &len) < 0)
	rb_sys_fail("getpeername(2)");
    return tcpaddr(&addr);
}

static VALUE
tcp_s_getaddress(obj, host)
    VALUE obj, host;
{
    struct sockaddr_in addr;
    struct hostent *h;

    if (obj_is_kind_of(host, cInteger)) {
	int i = NUM2INT(host);
	addr.sin_addr.s_addr = htonl(i);
    }
    else {
	Check_Type(host, T_STRING);
	setipaddr(RSTRING(host)->ptr, &addr);
    }

    return mkipaddr(addr.sin_addr.s_addr);
}

#ifdef HAVE_SYS_UN_H
static VALUE
unix_s_sock_open(sock, path)
    VALUE sock, path;
{
    return open_unix(sock, path, 0);
}

static VALUE
unix_path(sock)
    VALUE sock;
{
    OpenFile *fptr;

    GetOpenFile(sock, fptr);
    if (fptr->path == 0) {
	struct sockaddr_un addr;
	int len = sizeof(addr);
	if (getsockname(fileno(fptr->f), (struct sockaddr*)&addr, &len) < 0)
	    rb_sys_fail(0);
	fptr->path = strdup(addr.sun_path);
    }
    return str_new2(fptr->path);
}

static VALUE
unix_svr_s_open(class, path)
    VALUE class, path;
{
    return open_unix(class, path, 1);
}

static VALUE
unix_accept(sock)
    VALUE sock;
{
    OpenFile *fptr;
    struct sockaddr_un from;
    int fromlen;

    GetOpenFile(sock, fptr);
    fromlen = sizeof(struct sockaddr_un);
    return s_accept(cUNIXsocket, fileno(fptr->f),
		    (struct sockaddr*)&from, &fromlen);
}

static VALUE
unixaddr(sockaddr)
    struct sockaddr_un *sockaddr;
{
    return assoc_new(str_new2("AF_UNIX"),str_new2(sockaddr->sun_path));
}

static VALUE
unix_addr(sock)
    VALUE sock;
{
    OpenFile *fptr;
    struct sockaddr_un addr;
    int len = sizeof addr;

    GetOpenFile(sock, fptr);

    if (getsockname(fileno(fptr->f), (struct sockaddr*)&addr, &len) < 0)
	rb_sys_fail("getsockname(2)");
    return unixaddr(&addr);
}

static VALUE
unix_peeraddr(sock)
    VALUE sock;
{
    OpenFile *fptr;
    struct sockaddr_un addr;
    int len = sizeof addr;

    GetOpenFile(sock, fptr);

    if (getpeername(fileno(fptr->f), (struct sockaddr*)&addr, &len) < 0)
	rb_sys_fail("getsockname(2)");
    return unixaddr(&addr);
}
#endif

static void
setup_domain_and_type(domain, dv, type, tv)
    VALUE domain, type;
    int *dv, *tv;
{
    char *ptr;

    if (TYPE(domain) == T_STRING) {
	ptr = RSTRING(domain)->ptr;
	if (strcmp(ptr, "PF_INET") == 0)
	    *dv = PF_INET;
#ifdef PF_UNIX
	else if (strcmp(ptr, "PF_UNIX") == 0)
	    *dv = PF_UNIX;
#endif
#ifdef PF_IMPLINK
	else if (strcmp(ptr, "PF_IMPLINK") == 0)
	    *dv = PF_IMPLINK;
#endif
#ifdef PF_AX25
	else if (strcmp(ptr, "PF_AX25") == 0)
	    *dv = PF_AX25;
#endif
#ifdef PF_IPX
	else if (strcmp(ptr, "PF_IPX") == 0)
	    *dv = PF_IPX;
#endif
	else
	    Raise(eSocket, "Unknown socket domain %s", ptr);
    }
    else {
	*dv = NUM2INT(domain);
    }
    if (TYPE(type) == T_STRING) {
	ptr = RSTRING(type)->ptr;
	if (strcmp(ptr, "SOCK_STREAM") == 0)
	    *tv = SOCK_STREAM;
	else if (strcmp(ptr, "SOCK_DGRAM") == 0)
	    *tv = SOCK_DGRAM;
#ifdef SOCK_RAW
	else if (strcmp(ptr, "SOCK_RAW") == 0)
	    *tv = SOCK_RAW;
#endif
#ifdef SOCK_SEQPACKET
	else if (strcmp(ptr, "SOCK_SEQPACKET") == 0)
	    *tv = SOCK_SEQPACKET;
#endif
#ifdef SOCK_RDM
	else if (strcmp(ptr, "SOCK_RDM") == 0)
	    *tv = SOCK_RDM;
#endif
#ifdef SOCK_PACKET
	else if (strcmp(ptr, "SOCK_PACKET") == 0)
	    *tv = SOCK_PACKET;
#endif
	else
	    Raise(eSocket, "Unknown socket type %s", ptr);
    }
    else {
	*tv = NUM2INT(type);
    }
}

static VALUE
sock_s_open(class, domain, type, protocol)
    VALUE class, domain, type, protocol;
{
    int fd;
    int d, t;

    setup_domain_and_type(domain, &d, type, &t);
    fd = socket(d, t, NUM2INT(protocol));
    if (fd < 0) rb_sys_fail("socket(2)");
    return sock_new(class, fd);
}

static VALUE
sock_s_for_fd(class, fd)
    VALUE class, fd;
{
    return sock_new(class, NUM2INT(fd));
}

static VALUE
sock_s_socketpair(class, domain, type, protocol)
    VALUE class, domain, type, protocol;
{
#if !defined(__CYGWIN32__)
    int fd;
    int d, t, sp[2];

    setup_domain_and_type(domain, &d, type, &t);
    if (socketpair(d, t, NUM2INT(protocol), sp) < 0)
	rb_sys_fail("socketpair(2)");

    return assoc_new(sock_new(class, sp[0]), sock_new(class, sp[1]));
#else
    rb_notimplement();
#endif
}

static VALUE
sock_connect(sock, addr)
    VALUE sock;
    struct RString *addr;
{
    OpenFile *fptr;

    Check_Type(addr, T_STRING);
    str_modify(addr);

    GetOpenFile(sock, fptr);
    if (connect(fileno(fptr->f), (struct sockaddr*)addr->ptr, addr->len) < 0)
	rb_sys_fail("connect(2)");

    return INT2FIX(0);
}

static VALUE
sock_bind(sock, addr)
    VALUE sock;
    struct RString *addr;
{
    OpenFile *fptr;

    Check_Type(addr, T_STRING);
    str_modify(addr);

    GetOpenFile(sock, fptr);
    if (bind(fileno(fptr->f), (struct sockaddr*)addr->ptr, addr->len) < 0)
	rb_sys_fail("bind(2)");

    return INT2FIX(0);
}

static VALUE
sock_listen(sock, log)
   VALUE sock, log;
{
    OpenFile *fptr;

    GetOpenFile(sock, fptr);
    if (listen(fileno(fptr->f), NUM2INT(log)) < 0)
	rb_sys_fail("listen(2)");

    return INT2FIX(0);
}

static VALUE
sock_accept(sock)
   VALUE sock;
{
    OpenFile *fptr;
    VALUE addr, sock2;
    char buf[1024];
    int len = sizeof buf;

    GetOpenFile(sock, fptr);
    sock2 = s_accept(cSocket,fileno(fptr->f),(struct sockaddr*)buf,&len);

    return assoc_new(sock2, str_new(buf, len));
}

static VALUE
sock_send(argc, argv, sock)
    int argc;
    VALUE *argv;
    VALUE sock;
{
    struct RString *msg, *to;
    VALUE flags;
    OpenFile *fptr;
    FILE *f;
    int fd, n;

    rb_scan_args(argc, argv, "21", &msg, &flags, &to);

    Check_Type(msg, T_STRING);

    GetOpenFile(sock, fptr);
    f = fptr->f2?fptr->f2:fptr->f;
    fd = fileno(f);
#ifdef THREAD
    thread_fd_writable(fd);
#endif
    if (to) {
	Check_Type(to, T_STRING);
	n = sendto(fd, msg->ptr, msg->len, NUM2INT(flags),
		   (struct sockaddr*)to->ptr, to->len);
    }
    else {
	n = send(fd, msg->ptr, msg->len, NUM2INT(flags));
    }
    if (n < 0) {
	rb_sys_fail("send(2)");
    }
    return INT2FIX(n);
}

static VALUE
s_recv(sock, argc, argv, from)
    VALUE sock;
    int argc;
    VALUE *argv;
    int from;
{
    OpenFile *fptr;
    FILE f;
    struct RString *str;
    char buf[1024];
    int fd, alen = sizeof buf;
    VALUE len, flg;
    int flags;

    rb_scan_args(argc, argv, "11", &len, &flg);

    if (flg == Qnil) flags = 0;
    else             flags = NUM2INT(flg);

    str = (struct RString*)str_new(0, NUM2INT(len));

    GetOpenFile(sock, fptr);
    fd = fileno(fptr->f);
#ifdef THREAD
    thread_wait_fd(fd);
#endif
    TRAP_BEG;
    str->len = recvfrom(fd, str->ptr, str->len, flags,
			(struct sockaddr*)buf, &alen);
    TRAP_END;

    if (str->len < 0) {
	rb_sys_fail("recvfrom(2)");
    }

    if (from)
	return assoc_new(str, str_new(buf, alen));
    else
	return (VALUE)str;
}

static VALUE
sock_recv(argc, argv, sock)
    int argc;
    VALUE *argv;
    VALUE sock;
{
    return s_recv(sock, argc, argv, 0);
}

static VALUE
sock_recvfrom(argc, argv, sock)
    int argc;
    VALUE *argv;
    VALUE sock;
{
    return s_recv(sock, argc, argv, 1);
}

#ifdef HAVE_GETHOSTNAME
static VALUE
sock_gethostname(obj)
    VALUE obj;
{
    char buf[1024];

    if (gethostname(buf, (int)sizeof buf - 1) < 0)
	rb_sys_fail("gethostname");

    buf[sizeof buf - 1] = '\0';
    return str_new2(buf);
}
#else
#ifdef HAVE_UNAME

#include <sys/utsname.h>

static VALUE
sock_gethostname(obj)
    VALUE obj;
{
  struct utsname un;

  uname(&un);
  return str_new2(un.nodename);
}
#else
static VALUE
sock_gethostname(obj)
    VALUE obj;
{
    rb_notimplement();
}
#endif
#endif

static VALUE
mkhostent(h)
    struct hostent *h;
{
    struct sockaddr_in addr;
    char **pch;
    VALUE ary, names;

    if (h == NULL) {
#ifdef HAVE_HSTRERROR
	extern int h_errno;
	Raise(eSocket, (char *)hstrerror(h_errno));
#else
	Raise(eSocket, "host not found");
#endif
    }
    ary = ary_new();
    ary_push(ary, str_new2(h->h_name));
    names = ary_new();
    ary_push(ary, names);
    for (pch = h->h_aliases; *pch; pch++) {
	ary_push(names, str_new2(*pch));
    }
    ary_push(ary, INT2FIX(h->h_length));
#ifdef h_addr
    for (pch = h->h_addr_list; *pch; pch++) {
	ary_push(ary, str_new(*pch, h->h_length));
    }
#else
    ary_push(ary, str_new(h->h_addr, h->h_length));
#endif

    return ary;
}

static VALUE
sock_s_gethostbyname(obj, host)
    VALUE obj, host;
{
    struct sockaddr_in addr;
    struct hostent *h;

    if (obj_is_kind_of(host, cInteger)) {
	int i = NUM2INT(host);
	addr.sin_addr.s_addr = htonl(i);
    }
    else {
	Check_Type(host, T_STRING);
	setipaddr(RSTRING(host)->ptr, &addr);
    }
    h = gethostbyaddr((char *)&addr.sin_addr,
		      sizeof(addr.sin_addr),
		      AF_INET);

    return mkhostent(h);
}

sock_s_gethostbyaddr(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE vaddr, vtype;
    int type;

    struct sockaddr_in *addr;
    struct hostent *h;

    rb_scan_args(argc, argv, "11", &addr, &type);
    Check_Type(addr, T_STRING);
    if (!NIL_P(type)) {
	type = NUM2INT(vtype);
    }
    else {
	type = AF_INET;
    }

    h = gethostbyaddr(RSTRING(addr)->ptr, RSTRING(addr)->len, type);

    return mkhostent(h);
}

static VALUE
sock_s_getservbyaname(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE service, protocol;
    char *name, *proto;
    struct servent *sp;
    int port;

    rb_scan_args(argc, argv, "11", &service, &protocol);
    Check_Type(service, T_STRING);
    if (NIL_P(protocol)) proto = "tcp";
    else proto = RSTRING(protocol)->ptr;

    sp = getservbyname(RSTRING(service)->ptr, proto);
    if (!sp) {
	Raise(eSocket, "service/proto not found");
    }
    port = ntohs(sp->s_port);
    
    return INT2FIX(port);
}

Init_socket ()
{
    eSocket = rb_define_class("SocketError", eException);

    cBasicSocket = rb_define_class("BasicSocket", cIO);
    rb_undef_method(cBasicSocket, "new");
    rb_define_method(cBasicSocket, "shutdown", bsock_shutdown, -1);
    rb_define_method(cBasicSocket, "setsockopt", bsock_setsockopt, 3);
    rb_define_method(cBasicSocket, "getsockopt", bsock_getsockopt, 2);
    rb_define_method(cBasicSocket, "getsockname", bsock_getsockname, 0);
    rb_define_method(cBasicSocket, "getpeername", bsock_getpeername, 0);

    cTCPsocket = rb_define_class("TCPsocket", cBasicSocket);
    rb_define_singleton_method(cTCPsocket, "open", tcp_s_open, 2);
    rb_define_singleton_method(cTCPsocket, "new", tcp_s_open, 2);
    rb_define_method(cTCPsocket, "addr", tcp_addr, 0);
    rb_define_method(cTCPsocket, "peeraddr", tcp_peeraddr, 0);
    rb_define_singleton_method(cTCPsocket, "getaddress", tcp_s_getaddress, 1);

    cTCPserver = rb_define_class("TCPserver", cTCPsocket);
    rb_define_singleton_method(cTCPserver, "open", tcp_svr_s_open, -1);
    rb_define_singleton_method(cTCPserver, "new", tcp_svr_s_open, -1);
    rb_define_method(cTCPserver, "accept", tcp_accept, 0);

#ifdef HAVE_SYS_UN_H
    cUNIXsocket = rb_define_class("UNIXsocket", cBasicSocket);
    rb_define_singleton_method(cUNIXsocket, "open", unix_s_sock_open, 1);
    rb_define_singleton_method(cUNIXsocket, "new", unix_s_sock_open, 1);
    rb_define_method(cUNIXsocket, "path", unix_path, 0);
    rb_define_method(cUNIXsocket, "addr", unix_addr, 0);
    rb_define_method(cUNIXsocket, "peeraddr", unix_peeraddr, 0);

    cUNIXserver = rb_define_class("UNIXserver", cUNIXsocket);
    rb_define_singleton_method(cUNIXserver, "open", unix_svr_s_open, 1);
    rb_define_singleton_method(cUNIXserver, "new", unix_svr_s_open, 1);
    rb_define_method(cUNIXserver, "accept", unix_accept, 0);
#endif

    cSocket = rb_define_class("Socket", cBasicSocket);
    rb_define_singleton_method(cSocket, "open", sock_s_open, 3);
    rb_define_singleton_method(cSocket, "new", sock_s_open, 3);
    rb_define_singleton_method(cSocket, "for_fd", sock_s_for_fd, 1);

    rb_define_method(cSocket, "connect", sock_connect, 1);
    rb_define_method(cSocket, "bind", sock_bind, 1);
    rb_define_method(cSocket, "listen", sock_listen, 1);
    rb_define_method(cSocket, "accept", sock_accept, 0);

    rb_define_method(cSocket, "send", sock_send, -1);
    rb_define_method(cSocket, "recv", sock_recv, -1);
    rb_define_method(cSocket, "recvfrom", sock_recv, -1);

    rb_define_singleton_method(cSocket, "socketpair", sock_s_socketpair, 3);
    rb_define_singleton_method(cSocket, "pair", sock_s_socketpair, 3);
    rb_define_singleton_method(cSocket, "gethostname", sock_gethostname, 0);
    rb_define_singleton_method(cSocket, "gethostbyname", sock_s_gethostbyname, 1);
    rb_define_singleton_method(cSocket, "gethostbyaddr", sock_s_gethostbyaddr, -1);
    rb_define_singleton_method(cSocket, "getservbyname", sock_s_getservbyaname, -1);

    /* constants */
    rb_define_const(cSocket, "AF_INET", INT2FIX(AF_INET));
    rb_define_const(cSocket, "PF_INET", INT2FIX(PF_INET));
#ifdef AF_UNIX
    rb_define_const(cSocket, "AF_UNIX", INT2FIX(AF_UNIX));
    rb_define_const(cSocket, "PF_UNIX", INT2FIX(PF_UNIX));
#endif
#ifdef AF_IPX
    rb_define_const(cSocket, "AF_IPX", INT2FIX(AF_IPX));
    rb_define_const(cSocket, "PF_IPX", INT2FIX(PF_IPX));
#endif
#ifdef AF_APPLETALK
    rb_define_const(cSocket, "AF_APPLETALK", INT2FIX(AF_APPLETALK));
    rb_define_const(cSocket, "PF_APPLETALK", INT2FIX(PF_APPLETALK));
#endif

    rb_define_const(cSocket, "MSG_OOB", INT2FIX(MSG_OOB));
    rb_define_const(cSocket, "MSG_PEEK", INT2FIX(MSG_PEEK));
    rb_define_const(cSocket, "MSG_DONTROUTE", INT2FIX(MSG_DONTROUTE));

    rb_define_const(cSocket, "SOCK_STREAM", INT2FIX(SOCK_STREAM));
    rb_define_const(cSocket, "SOCK_DGRAM", INT2FIX(SOCK_DGRAM));
    rb_define_const(cSocket, "SOCK_RAW", INT2FIX(SOCK_RAW));
#ifdef SOCK_RDM
    rb_define_const(cSocket, "SOCK_RDM", INT2FIX(SOCK_RDM));
#endif
#ifdef SOCK_SEQPACKET
    rb_define_const(cSocket, "SOCK_SEQPACKET", INT2FIX(SOCK_SEQPACKET));
#endif
#ifdef SOCK_PACKET
    rb_define_const(cSocket, "SOCK_PACKET", INT2FIX(SOCK_PACKET));
#endif

    rb_define_const(cSocket, "SOL_SOCKET", INT2FIX(SOL_SOCKET));
#ifdef SOL_IP
    rb_define_const(cSocket, "SOL_IP", INT2FIX(SOL_IP));
#endif
#ifdef SOL_IPX
    rb_define_const(cSocket, "SOL_IPX", INT2FIX(SOL_IPX));
#endif
#ifdef SOL_ATALK
    rb_define_const(cSocket, "SOL_ATALK", INT2FIX(SOL_ATALK));
#endif
#ifdef SOL_TCP
    rb_define_const(cSocket, "SOL_TCP", INT2FIX(SOL_TCP));
#endif
#ifdef SOL_UDP
    rb_define_const(cSocket, "SOL_UDP", INT2FIX(SOL_UDP));
#endif

    rb_define_const(cSocket, "SO_DEBUG", INT2FIX(SO_DEBUG));
    rb_define_const(cSocket, "SO_REUSEADDR", INT2FIX(SO_REUSEADDR));
    rb_define_const(cSocket, "SO_KEEPALIVE", INT2FIX(SO_KEEPALIVE));
    rb_define_const(cSocket, "SO_LINGER", INT2FIX(SO_LINGER));
}
