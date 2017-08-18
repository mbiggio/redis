/* ==========================================================================
 * setproctitle.c - Linux/Darwin setproctitle.
 * --------------------------------------------------------------------------
 * Copyright (C) 2010  William Ahern
 * Copyright (C) 2013  Salvatore Sanfilippo
 * Copyright (C) 2013  Stam He
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ==========================================================================
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>	/* NULL size_t */
#include <stdarg.h>	/* va_list va_start va_end */
#include <stdlib.h>	/* malloc(3) setenv(3) clearenv(3) setproctitle(3) getprogname(3) */
#include <stdio.h>	/* vsnprintf(3) snprintf(3) */

#include <string.h>	/* strlen(3) strchr(3) strdup(3) memset(3) memcpy(3) */

#include <errno.h>	/* errno program_invocation_name program_invocation_short_name */

#if !defined(HAVE_SETPROCTITLE)
#define HAVE_SETPROCTITLE (defined __NetBSD__ || defined __FreeBSD__ || defined __OpenBSD__)
#endif


#if !HAVE_SETPROCTITLE
#if (defined __linux || defined __APPLE__)

extern char **environ;

static struct {
	/* original value */
	const char *arg0;

	/* title space available */
	char *base, *end;

	 /* pointer to original nul character within base */
	char *nul;

	_Bool reset;
	int error;
} SPT;


#ifndef SPT_MIN
#define SPT_MIN(a, b) (((a) < (b))? (a) : (b))
#endif

static inline size_t spt_min(size_t a, size_t b) {
	return SPT_MIN(a, b);
} /* spt_min() */


/*
 * For discussion on the portability of the various methods, see
 * http://lists.freebsd.org/pipermail/freebsd-stable/2008-June/043136.html
 */
static int spt_clearenv(void) {
#if __GLIBC__
	clearenv();

	return 0;
#else
	extern char **environ;
	static char **tmp;

	if (!(tmp = malloc(sizeof *tmp)))
		return errno;

	tmp[0]  = NULL;
	environ = tmp;

	return 0;
#endif
} /* spt_clearenv() */


static int spt_copyenv(char *oldenv[]) {
	extern char **environ;
	char *eq;
	int i, error;

	/*
	 * If environ is not equal to oldenv,
	 * it means that the env variables have already been
	 * moved elsewhere. This is never the case when
	 * spt_copyenv is called from spt_init.
	 */
	if (environ != oldenv)
		return 0;

	/*
	 * spt_clearenv just calls clearenv(),
	 * which basically just sets environ to NULL.
	 * Note that it does not clear the env variables
	 * memory location, which is still pointed to
	 * by oldenv.
	 */
	if ((error = spt_clearenv()))
		goto error;


	for (i = 0; oldenv[i]; i++) {

		/*
		 * Find the location of the '=' sign,
		 * pointed to by eq.
		 */

		if (!(eq = strchr(oldenv[i], '=')))
			continue;

		/*
		 * Null-terminate the env variable.
		 */
		*eq = '\0';

		/*
		 * Set the environment variable with the proper value.
		 * I don't think setenv actually stores it in the heap.
		 */
		error = (0 != setenv(oldenv[i], eq + 1, 1))? errno : 0;

		/*
		 * Restore the equal sign (not sure if it's actually useful).
		 */
		*eq = '=';

		if (error)
			goto error;
	}

	return 0;
error:
	environ = oldenv;

	return error;
} /* spt_copyenv() */


static int spt_copyargs(int argc, char *argv[]) {
	char *tmp;
	int i;

	/*
	 * Note that the loop starts from 1:
	 * the point of doing all this is to transfer all data
	 * so that we can extend as much as possible the program name
	 * in argv[0], which will remain in the same position in memory.
	 */
	for (i = 1; i < argc || (i >= argc && argv[i]); i++) {
		if (!argv[i])
			continue;

		/*
		 * Make tmp point to a copy of the argument stored in the heap
		 * (strdup uses malloc).
		 */
		if (!(tmp = strdup(argv[i])))
			return errno;

		/*
		 * Make argv[i] point to this location in the heap.
		 */
		argv[i] = tmp;
	}

	return 0;
} /* spt_copyargs() */


void spt_init(int argc, char *argv[]) {

	char **envp = environ;
	char *base, *end, *nul, *tmp;
	int i, error;

	if (!(base = argv[0])) {
		/*
		 * argv[0] is NULL in this case.
		 * Not sure how this could happen.
		 */
		return;
	}

	/*
	 * nul is initialized to base[len(base)],
	 * which means it points to the terminating NULL character.
	 *
	 * end is made to point to the first character following the
	 * terminating NULL character.
	 */
	nul = &base[strlen(base)];
	end = nul + 1;

	/*
	 * Loop on all the arguments.
	 * Also go beyond argc if argv[i] is not NULL.
	 */
	for (i = 0; i < argc || (i >= argc && argv[i]); i++) {
		/*
		 * Discard currend cli argument if either
		 * it is the empty string or its starting point
		 * is before end (it means we already considered this portion of memory).
		 */
		if (!argv[i] || argv[i] < end)
			continue;

		/*
		 * update end with the first character after the string.
		 */
		end = argv[i] + strlen(argv[i]) + 1;
	}

	/*
	 * Do the same as before for all the environment variables.
	 */
	for (i = 0; envp[i]; i++) {
		if (envp[i] < end)
			continue;

		/*
		 * update end with the first character after the string.
		 */
		end = envp[i] + strlen(envp[i]) + 1;
	}

	/*
	 * Copy the original process title to SPT.arg0.
	 * Note that strdup makes a new copy (it uses malloc).
	 */
	if (!(SPT.arg0 = strdup(argv[0])))
		goto syerr;

	printf("SPT.arg0 = %s\n", SPT.arg0);

#if __GLIBC__

	/*
	 * Make "program_invocation_name" and
	 * "program_invocation_short_name" point to
	 * a different location in the heap, with
	 * an exact copy of the originally pointed string.
	 */

	printf("Original program_invocation_name:       %s(%p)\n", program_invocation_name,       program_invocation_name     );
	printf("Original program_invocation_short_name: %s(%p)\n", program_invocation_short_name, program_invocation_short_name);


	if (!(tmp = strdup(program_invocation_name)))
		goto syerr;

	program_invocation_name = tmp;

	if (!(tmp = strdup(program_invocation_short_name)))
		goto syerr;

	program_invocation_short_name = tmp;

	printf("New program_invocation_name:       %s(%p)\n", program_invocation_name,       program_invocation_name     );
	printf("New program_invocation_short_name: %s(%p)\n", program_invocation_short_name, program_invocation_short_name);


#elif __APPLE__
	if (!(tmp = strdup(getprogname())))
		goto syerr;

	setprogname(tmp);
#endif


	/*
	 * Make the environment variables point to
	 * a different location (in the heap? not sure ...), with
	 * an exact copy of the originally pointed strings.
	 */

	if ((error = spt_copyenv(envp)))
		goto error;

	/*
	 * Make the arguments point to
	 * a different location in the heap, with
	 * an exact copy of the originally pointed strings.
	 */

	if ((error = spt_copyargs(argc, argv)))
		goto error;

	/*
	 * Now all the environment variables, arguments except argv[0],
	 * and program_invocation_name + program_invocation_short_name
	 * have been copied to safe memory locations.
	 * Therefore we can extend argv[0] to a location up to end.
	 * Store the nul, base and end pointers in SPT so that they can be
	 * later used by setproctitle to set the new argv[0].
	 */

	SPT.nul  = nul;
	SPT.base = base;
	SPT.end  = end;

	printf("SPT.base: %s(%p)\n", SPT.base, SPT.base);
	printf("SPT.nul : (%p)\n", SPT.nul);
	printf("SPT.end : (%p)\n", SPT.end);

	return;
syerr:
	error = errno;
	/* execution will go on to "error" */
error:
	SPT.error = error;
} /* spt_init() */


#ifndef SPT_MAXTITLE
#define SPT_MAXTITLE 255
#endif

void setproctitle(const char *fmt, ...) {
	char buf[SPT_MAXTITLE + 1]; /* use buffer in case argv[0] is passed */
	va_list ap;
	char *nul;
	int len, error;

	if (!SPT.base)
		return;

	if (fmt) {
		va_start(ap, fmt);
		len = vsnprintf(buf, sizeof buf, fmt, ap);
		va_end(ap);
	} else {
		len = snprintf(buf, sizeof buf, "%s", SPT.arg0);
	}

	/* 
	 * Here, len is the length of the string copied to buf,
	 * independently of the fact that this string may have been 
	 * truncated. Therefore, the effective length of the copied string is
	 * 
	 *            min(len, sizeof(buf)-1)
	 *
	 * excluding the terminating NULL character.
	 */

	if (len <= 0)
		{ error = errno; goto error; }

	if (!SPT.reset) {
		memset(SPT.base, 0, SPT.end - SPT.base);
		SPT.reset = 1;
	} else {
		memset(SPT.base, 0, spt_min(sizeof buf, SPT.end - SPT.base));
	}

	len = spt_min(len, spt_min(sizeof buf, SPT.end - SPT.base) - 1);
	memcpy(SPT.base, buf, len);
	nul = &SPT.base[len];

	if (nul < SPT.nul) {
		*SPT.nul = '.';
	} else if (nul == SPT.nul && &nul[1] < SPT.end) {
		*SPT.nul = ' ';
		*++nul = '\0';
	}

	return;
error:
	SPT.error = error;
} /* setproctitle() */


#endif /* __linux || __APPLE__ */
#endif /* !HAVE_SETPROCTITLE */
