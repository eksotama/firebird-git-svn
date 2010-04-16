/*
 *	PROGRAM:	JRD Remote Server
 *	MODULE:		inet_server.cpp
 *	DESCRIPTION:	Internet remote server.
 *
 * The contents of this file are subject to the Interbase Public
 * License Version 1.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy
 * of the License at http://www.Inprise.com/IPL.html
 *
 * Software distributed under the License is distributed on an
 * "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
 * or implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code was created by Inprise Corporation
 * and its predecessors. Portions created by Inprise Corporation are
 * Copyright (C) Inprise Corporation.
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 *
 * 2002.10.28 Sean Leyne - Code cleanup, removed obsolete "MPEXL" port
 * 2002.10.28 Sean Leyne - Code cleanup, removed obsolete "DecOSF" port
 * 2002.10.28 Sean Leyne - Code cleanup, removed obsolete "SGI" port
 *
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 *
 * 2002.10.30 Sean Leyne - Removed support for obsolete "PC_PLATFORM" define
 *
 */

#include "firebird.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "../jrd/common.h"
#include "../jrd/isc_proto.h"
#include "../jrd/divorce.h"
#include "../jrd/ibase.h"
#include "../common/classes/init.h"
#include "../common/config/config.h"
#include <sys/param.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include <errno.h>
#include "../jrd/ibase.h"
#include "../jrd/jrd_pwd.h"

#include "../remote/remote.h"
#include "../jrd/license.h"
#include "../common/thd.h"
#include "../jrd/file_params.h"
#include "../remote/inet_proto.h"
#include "../remote/serve_proto.h"
#include "../jrd/gds_proto.h"
#include "../jrd/thread_proto.h"
#include "../common/utils_proto.h"
#include "../common/classes/fb_string.h"

#ifdef UNIX
#ifdef NETBSD
#include <signal.h>
#else
#include <sys/signal.h>
#endif
#endif

#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#include "../common/classes/semaphore.h"

const char* TEMP_DIR = "/tmp";

const char* INTERBASE_USER_NAME		= "interbase";
const char* INTERBASE_USER_SHORT	= "interbas";
const char* FIREBIRD_USER_NAME		= "firebird";

static void set_signal(int, void (*)(int));
static void signal_handler(int);

static TEXT protocol[128];
static int INET_SERVER_start = 0;

static bool serverClosing = false;


extern "C" {

int FB_EXPORTED server_main( int argc, char** argv)
{
/**************************************
 *
 *	m a i n
 *
 **************************************
 *
 * Functional description
 *	Run the server with apollo mailboxes.
 *
 **************************************/
	RemPortPtr port;

// 01 Sept 2003, Nickolay Samofatov
// In GCC version 3.1-3.3 we need to install special error handler
// in order to get meaningful terminate() error message on stderr.
// In GCC 3.4 or later this is the default.
//#if __GNUC__ == 3 && __GNUC_MINOR__ >= 1 && __GNUC_MINOR__ < 4
//    std::set_terminate (__gnu_cxx::__verbose_terminate_handler);
//#endif

	// We should support 3 modes:
	// 1. Standalone single-process listener (like SS).
	// 2. Standalone listener, forking on each packet accepted (look -s switch in CS).
	// 3. Process spawned by (x)inetd (like CS).
	bool classic = false;
	bool standaloneClassic = false;
	bool super = false;

	// It's very easy to detect that we are spawned - just check fd 0 to be a socket.
	const int channel = 0;
	struct stat stat0;
	if (fstat(channel, &stat0) == 0 && S_ISSOCK(stat0.st_mode))
	{
		// classic server mode
		classic = true;
	}

	const TEXT* const* const end = argc + argv;
	argv++;
	bool debug = false;
	USHORT INET_SERVER_flag = 0;
	protocol[0] = 0;

	bool done = false;

	while (argv < end)
	{
		TEXT c;
		const TEXT* p = *argv++;
		if (*p++ == '-')
			while (c = *p++)
			{
				switch (UPPER(c))
				{
				case 'D':
					debug = true;
					break;

				case 'S':
					if (!classic)
					{
						standaloneClassic = true;
					}
					break;

				case 'I':
					if (!classic)
					{
						standaloneClassic = false;
					}
					break;

				case 'E':
					if (argv < end)
					{
						if (ISC_set_prefix(p, *argv) == -1)
							printf("Invalid argument Ignored\n");
						else
							argv++;	// do not skip next argument if this one is invalid
					}
					else
					{
						printf("Missing argument, switch -E ignored\n");
					}
					done = true;
					break;

				case 'P':
					if (argv < end)
					{
						if (!classic)
						{
							fb_utils::snprintf(protocol, sizeof(protocol), "/%s", *argv++);
						}
						else
						{
							gds__log("Switch -P ignored in CS mode\n");
						}
					}
					else
					{
						printf("Missing argument, switch -P ignored\n");
					}
					break;

                case 'H':
				case '?':
					printf("Firebird TCP/IP server options are:\n");
					printf("  -d        : debug on\n");
					printf("  -s        : standalone - true\n");
					printf("  -i        : standalone - false\n");
					printf("  -p <port> : specify port to listen on\n");
					printf("  -z        : print version and exit\n");
					printf("  -h|?      : print this help\n");
                    printf("\n");
                    printf("  (The following -e options used to be -h options)\n");
					printf("  -e <firebird_root_dir>   : set firebird_root path\n");
					printf("  -el <firebird_lock_dir>  : set runtime firebird_lock dir\n");
					printf("  -em <firebird_msg_dir>   : set firebird_msg dir path\n");

					exit(FINI_OK);

				case 'Z':
					printf("Firebird TCP/IP server version %s\n", GDS_VERSION);
					exit(FINI_OK);

				default:
					printf("Unknown switch '%c', ignored\n", c);
					break;
				}
				if (done)
					break;
			}
	}

	if (!(classic || standaloneClassic))
	{
		INET_SERVER_flag |= SRVR_multi_client;
		super = true;
	}
	if (debug)
	{
		INET_SERVER_flag |= SRVR_debug;
	}

	gds__log("modes are c=%d sac=%d s=%d", classic, standaloneClassic, super);

	// activate paths set with -e family of switches
	ISC_set_prefix(0, 0);

	// ignore some signals
	set_signal(SIGPIPE, signal_handler);
	set_signal(SIGUSR1, signal_handler);
	set_signal(SIGUSR2, signal_handler);

	// First of all change directory to tmp
	if (chdir(TEMP_DIR))
	{
		// error on changing the directory
		gds__log("Could not change directory to %s due to errno %d", TEMP_DIR, errno);
	}

#if defined(HAVE_SETRLIMIT) && defined(HAVE_GETRLIMIT)
#if !(defined(DEV_BUILD))
	if (Config::getBugcheckAbort())
#endif
	{
		// try to force core files creation
		struct rlimit core;
		if (getrlimit(RLIMIT_CORE, &core) == 0)
		{
			core.rlim_cur = core.rlim_max;
			if (setrlimit(RLIMIT_CORE, &core) != 0)
			{
				gds__log("setrlimit() failed, errno=%d", errno);
			}
		}
		else
		{
			gds__log("getrlimit() failed, errno=%d", errno);
		}
	}

#if (defined SOLARIS || defined HPUX || defined LINUX)
	if (super)
	{
		// Increase max open files to hard limit for Unix
		// platforms which are known to have low soft limits.

		struct rlimit old;

		if (getrlimit(RLIMIT_NOFILE, &old) == 0 && old.rlim_cur < old.rlim_max)
		{
			struct rlimit new_max;
			new_max.rlim_cur = new_max.rlim_max = old.rlim_max;
			if (setrlimit(RLIMIT_NOFILE, &new_max) == 0)
			{
#if _FILE_OFFSET_BITS == 64
				gds__log("64 bit i/o support is on.");
				gds__log("Open file limit increased from %lld to %lld",
						 old.rlim_cur, new_max.rlim_cur);

#else
				gds__log("Open file limit increased from %d to %d",
						 old.rlim_cur, new_max.rlim_cur);
#endif
			}
		}
	}
#endif // Unix platforms

#endif // HAVE_*ETRLIMIT

	// Remove restriction on username, for DEV builds
	// restrict only for production builds.  MOD 21-July-2002
#ifndef DEV_BUILD
	Firebird::string user_name;	// holds the user name
	// check user id
	ISC_get_user(&user_name, NULL, NULL, NULL);

	if (user_name != "root" &&
		user_name != FIREBIRD_USER_NAME &&
		user_name != INTERBASE_USER_NAME &&
		user_name != INTERBASE_USER_SHORT)
	{
		// invalid user -- bail out
		fprintf(stderr, "%s: Invalid user (must be %s, %s, %s or root).\n",
				   "fbserver", FIREBIRD_USER_NAME,
				   INTERBASE_USER_NAME, INTERBASE_USER_SHORT);
		exit(STARTUP_ERROR);
	}
#endif

	if (!(debug || classic))
	{
		int mask = 0; // FD_ZERO(&mask);
		mask |= 1 << 2; // FD_SET(2, &mask);
		divorce_terminal(mask);
	}

	if (super || standaloneClassic)
	{
		ISC_STATUS_ARRAY status_vector;
		port = INET_connect(protocol, 0, status_vector, INET_SERVER_flag, 0);
		if (!port)
		{
			gds__print_status(status_vector);
			exit(STARTUP_ERROR);
		}
	}

	if (classic)
	{
		port = INET_server(channel);
		if (!port)
		{
			fprintf(stderr, "fbserver: Unable to start INET_server\n");
			exit(STARTUP_ERROR);
		}
	}

	if (super)
	{
		// Server tries to attach to security2.fdb to make sure everything is OK
		// This code fixes bug# 8429 + all other bug of that kind - from
		// now on the server exits if it cannot attach to the database
		// (wrong or no license, not enough memory, etc.

		TEXT path[MAXPATHLEN];
		ISC_STATUS_ARRAY status;
		isc_db_handle db_handle = 0L;

		Auth::SecurityDatabase::getPath(path);
		const char dpb[] = {isc_dpb_version1, isc_dpb_gsec_attach, 1, 1};
		isc_attach_database(status, strlen(path), path, &db_handle, sizeof dpb, dpb);
		if (status[0] == 1 && status[1] > 0)
		{
			gds__log_status(path, status);
			isc_print_status(status);
			exit(STARTUP_ERROR);
		}
		isc_detach_database(status, &db_handle);
		if (status[0] == 1 && status[1] > 0)
		{
			gds__log_status(path, status);
			isc_print_status(status);
			exit(STARTUP_ERROR);
		}
	} // end scope

	SRVR_multi_thread(port, INET_SERVER_flag);

#ifdef DEBUG_GDS_ALLOC
	// In Debug mode - this will report all server-side memory leaks due to remote access

	//gds_alloc_report(0, __FILE__, __LINE__);
	Firebird::PathName name = fb_utils::getPrefix(fb_utils::FB_DIR_LOG, "memdebug.log");
	FILE* file = fopen(name.c_str(), "w+t");
	if (file)
	{
	  fprintf(file, "Global memory pool allocated objects\n");
	  getDefaultMemoryPool()->print_contents(file);
	  fclose(file);
	}
#endif

	// perform atexit shutdown here when all globals in embedded library are active
	// also sync with possibly already running shutdown in dedicated thread
	fb_shutdown(10000, fb_shutrsn_exit_called);

	return FINI_OK;
}

} // extern "C"


static void set_signal( int signal_number, void (*handler) (int))
{
/**************************************
 *
 *	s e t _ s i g n a l
 *
 **************************************
 *
 * Functional description
 *	Establish signal handler.
 *
 **************************************/
#ifdef UNIX
	struct sigaction vec, old_vec;

	vec.sa_handler = handler;
	sigemptyset(&vec.sa_mask);
	vec.sa_flags = 0;
	sigaction(signal_number, &vec, &old_vec);
#endif
}


static void signal_handler(int)
{
/**************************************
 *
 *	s i g n a l _ h a n d l e r
 *
 **************************************
 *
 * Functional description
 *	Dummy signal handler.
 *
 **************************************/

	++INET_SERVER_start;
}

