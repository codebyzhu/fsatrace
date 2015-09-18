#define open Oopen
#define rename Orename
#define unlink Ounlink
#define fopen Ofopen

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include "fsatraceunix.h"

#undef open
#undef rename
#undef unlink
#undef fopen

static int	s_fd;
static char    *s_buf;

static int      (*orename) (const char *, const char *);

#define HOOKn(rt, n, args) static rt (*o##n) args;
#define HOOK1(rt, n, t0, c, e) HOOKn (rt, n, (t0))
#define HOOK2(rt, n, t0, t1, c, e) HOOKn (rt, n, (t0, t1))
#define HOOK3(rt, n, t0, t1, t2, c, e) HOOKn (rt, n, (t0, t1, t2))
#include "hooks.h"
#undef HOOK3
#undef HOOK2
#undef HOOK1
#undef HOOKn

static int
good(const char *s, int sz)
{
	int		i;
	int		bad = 0;

	for (i = 0; i < sz; i++)
		bad += s[i] == 0;
	return !bad;
}

static void
swrite(const char *p, int sz)
{
	int		g;
	char           *dst = s_buf + sizeof(size_t);
	size_t         *psofar = (size_t *) s_buf;
	size_t		sofar = __sync_fetch_and_add(psofar, sz);
	memcpy(dst + sofar, p, sz);
	g = good(p, sz);
	if (!g)
		fprintf(stderr, "BAD: %s\n", p);
	assert(g);
}

static void
__attribute((constructor(101)))
init()
{
	const char     *libcname =
#if defined __APPLE__
	"libc.dylib"
#elif defined __NetBSD__
	"libc.so"
#else
	"libc.so.6"
#endif
	               ;
	void           *libc = dlopen(libcname, RTLD_LAZY | RTLD_GLOBAL);
	const char     *shname = getenv(ENVOUT);
	s_fd = shm_open(shname, O_CREAT | O_RDWR, 0666);
	s_buf = mmap(0, LOGSZ, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, 0);
	assert(s_fd >= 0);

	orename = dlsym(libc, "rename");

#define HOOKn(n) o##n = dlsym(libc, #n);
#define HOOK1(rt, n, t0, c, e) HOOKn(n)
#define HOOK2(rt, n, t0, t1, c, e) HOOKn(n)
#define HOOK3(rt, n, t0, t1, t2, c, e) HOOKn(n)
#include "hooks.h"
#undef HOOK1
#undef HOOK2
#undef HOOK3
#undef HOOKn
}

static void
__attribute((destructor(101)))
term()
{
	munmap(s_buf, LOGSZ);
	close(s_fd);
}

static inline void
iemit(int c, const char *p1, const char *p2)
{
	char		buf       [10000];
	int		sz = 0;

	sz += snprintf(buf, sizeof(buf) - 1 - sz, "%c|%s", c, p1);
	if (p2)
		sz += snprintf(buf + sz, sizeof(buf) - 1 - sz, "|%s", p2);
	sz += snprintf(buf + sz, sizeof(buf) - 1 - sz, "\n");
	assert(sz < sizeof(buf) - 1);
	buf[sz] = 0;
	swrite(buf, sz);
}

static void
emit(int c, const char *p1)
{
	char		ap        [PATH_MAX];
	iemit(c, realpath(p1, ap), 0);
}

int
rename(const char *p1, const char *p2)
{
	int		r;
	char		b1        [PATH_MAX];
	char		b2        [PATH_MAX];
	char           *rp1 = realpath(p1, b1);
	r = orename(p1, p2);
	if (!r)
		iemit('m', realpath(p2, b2), rp1);
	return r;
}

#define HOOKn(rt, n, args, cargs, c, e)			\
	rt n args {					\
		rt r = o##n cargs;			\
			if (c)				\
				e;			\
			return r;			\
	}
#define HOOK1(rt, n, t0, c, e)				\
  HOOKn(rt, n, (t0 a0), (a0), c, e)
#define HOOK2(rt, n, t0, t1, c, e)			\
  HOOKn(rt, n, (t0 a0, t1 a1), (a0, a1), c, e)
#define HOOK3(rt, n, t0, t1, t2, c, e)			\
  HOOKn(rt, n, (t0 a0, t1 a1, t2 a2), (a0, a1, a2), c, e)
#include "hooks.h"
#undef HOOK1
#undef HOOK2
#undef HOOK3
