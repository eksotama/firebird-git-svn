/*
 *      PROGRAM:        JRD Lock Manager
 *      MODULE:         print.cpp
 *      DESCRIPTION:    Lock Table printer
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
 * 2002.10.27 Sean Leyne - Completed removal of obsolete "DELTA" port
 * 2002.10.27 Sean Leyne - Code Cleanup, removed obsolete "Ultrix" port
 *
 * 2002.10.28 Sean Leyne - Code cleanup, removed obsolete "SGI" port
 *
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 * 
 * 2008.04.04 Roman Simakov - Added html output support
 *
 */

#include "firebird.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../jrd/common.h"
#include "../jrd/file_params.h"
#include "../jrd/jrd.h"
#include "../jrd/lck.h"
#include "../jrd/isc.h"
#include "../lock/lock.h"
#include "../jrd/gdsassert.h"
#include "../jrd/gds_proto.h"
#include "../jrd/isc_proto.h"
#include "../jrd/isc_s_proto.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <sys/stat.h>

#ifdef HAVE_SYS_PARAM_H
#  include <sys/param.h>
#endif

#ifdef WIN_NT
#include <io.h>
#endif

#ifndef FPRINTF
#define FPRINTF         fprintf
#endif

typedef FILE* OUTFILE;

const USHORT SW_I_ACQUIRE	= 1;
const USHORT SW_I_OPERATION	= 2;
const USHORT SW_I_TYPE		= 4;
const USHORT SW_I_WAIT		= 8;

#define SRQ_BASE                    ((UCHAR*) LOCK_header)

struct waitque {
	USHORT waitque_depth;
	SRQ_PTR waitque_entry[30];
};

static void prt_lock_activity(OUTFILE, const lhb*, USHORT, USHORT, USHORT);
static void prt_lock_init(void*, sh_mem*, bool);
static void prt_history(OUTFILE, const lhb*, SRQ_PTR, const SCHAR*);
static void prt_lock(OUTFILE, const lhb*, lbl*, USHORT);
static void prt_owner(OUTFILE, const lhb*, const own*, bool, bool);
static void prt_owner_wait_cycle(OUTFILE, const lhb*, const own*, USHORT, waitque*);
static void prt_request(OUTFILE, const lhb*, const lrq*);
static void prt_que(OUTFILE, const lhb*, const SCHAR*, const srq*, USHORT);
static void prt_que2(OUTFILE, const lhb*, const SCHAR*, const srq*, USHORT);

// HTML print functions
bool sw_html_format = false;

static void prt_html_begin(OUTFILE);
static void prt_html_end(OUTFILE);

static const TEXT preOwn[] = "own";
static const TEXT preRequest[] = "request";
static const TEXT preLock[] = "lock";

class HtmlLink
{
public:
	HtmlLink(const TEXT* prefix, const SLONG value)
	{
		if (sw_html_format && value)
			sprintf(strBuffer, "<a href=\"#%s%"SLONGFORMAT"\">%6"SLONGFORMAT"</a>", prefix, value, value);		
		else
			sprintf(strBuffer, "%6"SLONGFORMAT, value);		
	}
	operator const TEXT*()
	{
		return strBuffer;
	}
private:
	TEXT strBuffer[256];
	HtmlLink(const HtmlLink&){}
};


static const TEXT history_names[][10] = {
	"n/a", "ENQ", "DEQ", "CONVERT", "SIGNAL", "POST", "WAIT",
	"DEL_PROC", "DEL_LOCK", "DEL_REQ", "DENY", "GRANT", "LEAVE",
	"SCAN", "DEAD", "ENTER", "BUG", "ACTIVE", "CLEANUP", "DEL_OWNER"
};

static const TEXT valid_switches[] = "Valid switches are: -o, -p, -l, -r, \
		-a, -h, -n, -s <n>, -c, -i <n> <n>, -m \n";

// The same table is in lock.cpp, maybe worth moving to a common file?
static const UCHAR compatibility[LCK_max][LCK_max] =
{

/*							Shared	Prot	Shared	Prot
			none	null	Read	Read	Write	Write	Exclusive */

/* none */	{true,	true,	true,	true,	true,	true,	true},
/* null */	{true,	true,	true,	true,	true,	true,	true},
/* SR */	{true,	true,	true,	true,	true,	true,	false},
/* PR */	{true,	true,	true,	true,	false,	false,	false},
/* SW */	{true,	true,	true,	false,	true,	false,	false},
/* PW */	{true,	true,	true,	false,	false,	false,	false},
/* EX */	{true,	true,	false,	false,	false,	false,	false}
};

//#define COMPATIBLE(st1, st2)	compatibility [st1 * LCK_max + st2]

int CLIB_ROUTINE main( int argc, char *argv[])
{
/**************************************
 *
 *      m a i n
 *
 **************************************
 *
 * Functional description
 *	Check switches passed in and prepare to dump the lock table
 *	to stdout.
 *
 **************************************/
	OUTFILE outfile = stdout;

/* Perform some special handling when run as a Firebird service.  The
   first switch can be "-svc" (lower case!) or it can be "-svc_re" followed
   by 3 file descriptors to use in re-directing stdin, stdout, and stderr. */

	if (argc > 1 && !strcmp(argv[1], "-svc")) {
		argv++;
		argc--;
	}
	else if (argc > 4 && !strcmp(argv[1], "-svc_re")) {
		long redir_in = atol(argv[2]);
		long redir_out = atol(argv[3]);
		long redir_err = atol(argv[4]);
#ifdef WIN_NT
		redir_in = _open_osfhandle(redir_in, 0);
		redir_out = _open_osfhandle(redir_out, 0);
		redir_err = _open_osfhandle(redir_err, 0);
#endif
		if (redir_in != 0)
			if (dup2((int) redir_in, 0))
				close((int) redir_in);
		if (redir_out != 1)
			if (dup2((int) redir_out, 1))
				close((int) redir_out);
		if (redir_err != 2)
			if (dup2((int) redir_err, 2))
				close((int) redir_err);
		argv += 4;
		argc -= 4;
	}
	//const int orig_argc = argc;
	//SCHAR** orig_argv = argv;

/* Handle switches, etc. */

	argv++;
	bool sw_consistency = false;
	bool sw_waitlist = false;
	bool sw_file = false;
	bool sw_requests = false;
	bool sw_locks = false;
	bool sw_history = false;
	bool sw_nobridge = false;
	bool sw_owners = true;
	
	USHORT sw_interactive;
	// Those variables should be signed to accept negative values from atoi
	SSHORT sw_series;
	SSHORT sw_intervals;
	SSHORT sw_seconds;
	sw_series = sw_interactive = sw_intervals = sw_seconds = 0;
	TEXT* lock_file = NULL;

	while (--argc) {
		SCHAR* p = *argv++;
		if (*p++ != '-') {
			FPRINTF(outfile, valid_switches);
			exit(FINI_OK);
		}
		SCHAR c;
		while (c = *p++)
			switch (c) {
			case 'o':
			case 'p':
				sw_owners = true;
				break;

			case 'c':
				sw_nobridge = true;
				sw_consistency = true;
				break;

			case 'l':
				sw_locks = true;
				break;

			case 'r':
				sw_requests = true;
				break;

			case 'a':
				sw_locks = true;
				sw_owners = true;
				sw_requests = true;
				sw_history = true;
				break;

			case 'h':
				sw_history = true;
				break;

			case 's':
				if (argc > 1)
					sw_series = atoi(*argv++);
				if (sw_series <= 0) {
					FPRINTF(outfile,
							"Please specify a positive value following option -s\n");
					exit(FINI_OK);
				}
				--argc;
				break;

			case 'n':
				sw_nobridge = true;
				break;

			case 'i':
				while (c = *p++)
					switch (c) {
					case 'a':
						sw_interactive |= SW_I_ACQUIRE;
						break;

					case 'o':
						sw_interactive |= SW_I_OPERATION;
						break;

					case 't':
						sw_interactive |= SW_I_TYPE;
						break;

					case 'w':
						sw_interactive |= SW_I_WAIT;
						break;

					default:
						FPRINTF(outfile,
								"Valid interactive switches are: a, o, t, w\n");
						exit(FINI_OK);
						break;
					}
				if (!sw_interactive)
					sw_interactive =
						(SW_I_ACQUIRE | SW_I_OPERATION | SW_I_TYPE | SW_I_WAIT);
				sw_nobridge = true;
				sw_seconds = sw_intervals = 1;
				if (argc > 1) {
					sw_seconds = atoi(*argv++);
					--argc;
					if (argc > 1) {
						sw_intervals = atoi(*argv++);
						--argc;
					}
					if (!(sw_seconds > 0) || (sw_intervals < 0)) {
						FPRINTF(outfile,
								"Please specify 2 positive values for option -i\n");
						exit(FINI_OK);
					}
				}
				--p;
				break;

			case 'w':
				sw_nobridge = true;
				sw_waitlist = true;
				break;

			case 'f':
				sw_nobridge = true;
				sw_file = true;
				if (argc > 1) {
					lock_file = *argv++;
					--argc;
				}
				else {
					FPRINTF(outfile, "Usage: -f <filename>\n");
					exit(FINI_OK);
				}
				break;
				
			case 'm':
				sw_html_format = true;
				break;

			default:
				FPRINTF(outfile, valid_switches);
				exit(FINI_OK);
				break;
			}
	}

	TEXT buffer[MAXPATHLEN];
	if (!sw_file) {
		gds__prefix_lock(buffer, LOCK_FILE);
		lock_file = buffer;
	}

	SH_MEM_T shmem_data;

	SLONG LOCK_size_mapped = 0;			/* Use length of existing segment */

	ISC_STATUS_ARRAY status_vector;
	
	lhb* LOCK_header = (lhb*) ISC_map_file(status_vector,
							lock_file,
							prt_lock_init,
							0,
							-LOCK_size_mapped,	/* Negative to NOT truncate file */
							&shmem_data);

	TEXT expanded_lock_filename[MAXPATHLEN];
	TEXT hostname[64];
	sprintf(expanded_lock_filename, lock_file,
			ISC_get_host(hostname, sizeof(hostname)));

/* Make sure the lock file is valid - if it's a zero length file we 
 * can't look at the header without causing a BUS error by going
 * off the end of the mapped region.
 */

	if (LOCK_header && shmem_data.sh_mem_length_mapped < (SLONG) sizeof(lhb)) {
		/* Mapped file is obviously too small to really be a lock file */
		FPRINTF(outfile,
				"Unable to access lock table - file too small.\n%s\n",
				expanded_lock_filename);
		exit(FINI_OK);
	}

	if (LOCK_header
		&& LOCK_header->lhb_length > shmem_data.sh_mem_length_mapped)
	{
#if (!(defined UNIX) || (defined HAVE_MMAP))
		SLONG length = LOCK_header->lhb_length;
		LOCK_header = (lhb*) ISC_remap_file(status_vector, &shmem_data, length,
										   FALSE);
#endif
	}

	if (!LOCK_header) {
		FPRINTF(outfile, "Unable to access lock table.\n%s\n",
				expanded_lock_filename);
		gds__print_status(status_vector);
		exit(FINI_OK);
	}

	LOCK_size_mapped = shmem_data.sh_mem_length_mapped;

/* if we can't read this version - admit there's nothing to say and return. */

	if ((LOCK_header->lhb_version != SS_LHB_VERSION) &&
		(LOCK_header->lhb_version != CLASSIC_LHB_VERSION))
	{
		if (LOCK_header->lhb_type == 0 && LOCK_header->lhb_version == 0) 
		{
			FPRINTF(outfile, "\tLock table is empty.\n");
		}
		else 
		{
			FPRINTF(outfile, "\tUnable to read lock table version %d.\n",
				LOCK_header->lhb_version);
		}
		exit(FINI_OK);
	}

/* Print lock activity report */

	if (sw_interactive) {
		sw_html_format = false;
		prt_lock_activity(outfile, LOCK_header, sw_interactive, (USHORT) sw_seconds,
						  (USHORT) sw_intervals);
		exit(FINI_OK);
	}

	lhb* header = NULL;
	
	if (sw_consistency) {
		/* To avoid changes in the lock file while we are dumping it - make
		 * a local buffer, lock the lock file, copy it, then unlock the
		 * lock file to let processing continue.  Printing of the lock file
		 * will continue from the in-memory copy.
		 */
		header = (lhb*) gds__alloc(LOCK_header->lhb_length);
		if (!header) {
			FPRINTF(outfile,
					"Insufficient memory for consistent lock statistics.\n");
			FPRINTF(outfile, "Try omitting the -c switch.\n");
			exit(FINI_OK);
		}

		ISC_mutex_lock(LOCK_header->lhb_mutex);
		memcpy(header, LOCK_header, LOCK_header->lhb_length);
		ISC_mutex_unlock(LOCK_header->lhb_mutex);
		LOCK_header = header;
	}

/* Print lock header block */
	prt_html_begin(outfile);

	FPRINTF(outfile, "LOCK_HEADER BLOCK\n");
	FPRINTF(outfile,
			"\tVersion: %d, Active owner: %s, Length: %6"SLONGFORMAT
			", Used: %6"SLONGFORMAT"\n",
			LOCK_header->lhb_version, (const TEXT*)HtmlLink(preOwn, LOCK_header->lhb_active_owner),
			LOCK_header->lhb_length, LOCK_header->lhb_used);

	FPRINTF(outfile, "\tFlags: 0x%04X\n",
			LOCK_header->lhb_flags);

	FPRINTF(outfile,
			"\tEnqs: %6"UQUADFORMAT", Converts: %6"UQUADFORMAT
			", Rejects: %6"UQUADFORMAT", Blocks: %6"UQUADFORMAT"\n",
			LOCK_header->lhb_enqs, LOCK_header->lhb_converts,
			LOCK_header->lhb_denies, LOCK_header->lhb_blocks);

	FPRINTF(outfile,
			"\tDeadlock scans: %6"UQUADFORMAT", Deadlocks: %6"UQUADFORMAT
			", Scan interval: %3"ULONGFORMAT"\n",
			LOCK_header->lhb_scans, LOCK_header->lhb_deadlocks,
			LOCK_header->lhb_scan_interval);

	FPRINTF(outfile,
			"\tAcquires: %6"UQUADFORMAT", Acquire blocks: %6"UQUADFORMAT
			", Spin count: %3"ULONGFORMAT"\n",
			LOCK_header->lhb_acquires, LOCK_header->lhb_acquire_blocks,
			LOCK_header->lhb_acquire_spins);

	if (LOCK_header->lhb_acquire_blocks) {
		// CVC: MSVC up to v6 couldn't convert FB_UINT64 to double.
		const float bottleneck =
			(float) ((100. * (SINT64) LOCK_header->lhb_acquire_blocks) /
					 (SINT64) LOCK_header->lhb_acquires);
		FPRINTF(outfile, "\tMutex wait: %3.1f%%\n", bottleneck);
	}
	else
		FPRINTF(outfile, "\tMutex wait: 0.0%%\n");

	SLONG hash_total_count = 0;
	SLONG hash_max_count = 0;
	SLONG hash_min_count = 10000000;
	USHORT i = 0;
	for (const srq* slot = LOCK_header->lhb_hash; i < LOCK_header->lhb_hash_slots;
		 slot++, i++)
	{
		SLONG hash_lock_count = 0;
		for (const srq* que_inst = (SRQ) SRQ_ABS_PTR(slot->srq_forward); que_inst != slot;
			 que_inst = (SRQ) SRQ_ABS_PTR(que_inst->srq_forward))
		{
			++hash_total_count;
			++hash_lock_count;
		}
		if (hash_lock_count < hash_min_count)
			hash_min_count = hash_lock_count;
		if (hash_lock_count > hash_max_count)
			hash_max_count = hash_lock_count;
	}

	FPRINTF(outfile, "\tHash slots: %4d, ", LOCK_header->lhb_hash_slots);

	FPRINTF(outfile, "Hash lengths (min/avg/max): %4"SLONGFORMAT"/%4"SLONGFORMAT
			"/%4"SLONGFORMAT"\n",
			hash_min_count, (hash_total_count / LOCK_header->lhb_hash_slots),
			hash_max_count);

	const shb* a_shb = (shb*) SRQ_ABS_PTR(LOCK_header->lhb_secondary);
	FPRINTF(outfile,
			"\tRemove node: %6"SLONGFORMAT", Insert queue: %6"SLONGFORMAT
			", Insert prior: %6"SLONGFORMAT"\n",
			a_shb->shb_remove_node, a_shb->shb_insert_que,
			a_shb->shb_insert_prior);

	prt_que(outfile, LOCK_header, "\tOwners", &LOCK_header->lhb_owners,
			OFFSET(own*, own_lhb_owners));
	prt_que(outfile, LOCK_header, "\tFree owners",
			&LOCK_header->lhb_free_owners, OFFSET(own*, own_lhb_owners));
	prt_que(outfile, LOCK_header, "\tFree locks",
			&LOCK_header->lhb_free_locks, OFFSET(lbl*, lbl_lhb_hash));
	prt_que(outfile, LOCK_header, "\tFree requests",
			&LOCK_header->lhb_free_requests, OFFSET(lrq*, lrq_lbl_requests));

/* Print lock ordering option */

	FPRINTF(outfile, "\tLock Ordering: %s\n",
			(LOCK_header->lhb_flags & LHB_lock_ordering) ?
				"Enabled" : "Disabled");
	FPRINTF(outfile, "\n");

/* Print known owners */

	if (sw_owners) {
		const srq* que_inst;
		SRQ_LOOP(LOCK_header->lhb_owners, que_inst) {
			prt_owner(outfile, LOCK_header,
					  (own*) ((UCHAR*) que_inst - OFFSET(own*, own_lhb_owners)),
					  sw_requests, sw_waitlist);
		}
	}

/* Print known locks */

	if (sw_locks || sw_series) {
		USHORT i2 = 0;
		for (const srq* slot = LOCK_header->lhb_hash;
			 i2 < LOCK_header->lhb_hash_slots; slot++, i2++)
		{
			for (const srq* que_inst = (SRQ) SRQ_ABS_PTR(slot->srq_forward); que_inst != slot;
				 que_inst = (SRQ) SRQ_ABS_PTR(que_inst->srq_forward))
			{
				prt_lock(outfile, LOCK_header,
						 (lbl*) ((UCHAR *) que_inst - OFFSET(lbl*, lbl_lhb_hash)),
						 sw_series);
			}
		}
	}

	if (sw_history)
		prt_history(outfile, LOCK_header, LOCK_header->lhb_history,
					"History");

	prt_history(outfile, LOCK_header, a_shb->shb_history, "Event log");

	prt_html_end(outfile);
	
	if (header)
		gds__free(header);

	return FINI_OK;
}


static void prt_lock_activity(
							  OUTFILE outfile,
							  const lhb* header,
							  USHORT flag, USHORT seconds, USHORT intervals)
{
/**************************************
 *
 *	p r t _ l o c k _ a c t i v i t y
 *
 **************************************
 *
 * Functional description
 *	Print a time-series lock activity report 
 *
 **************************************/
	time_t clock = time(NULL);
	tm d = *localtime(&clock);

	FPRINTF(outfile, "%02d:%02d:%02d ", d.tm_hour, d.tm_min, d.tm_sec);

	if (flag & SW_I_ACQUIRE)
		FPRINTF(outfile,
				"acquire/s acqwait/s  %%acqwait acqrtry/s rtrysuc/s ");

	if (flag & SW_I_OPERATION)
		FPRINTF(outfile,
				"enqueue/s convert/s downgrd/s dequeue/s readata/s wrtdata/s qrydata/s ");

	if (flag & SW_I_TYPE)
		FPRINTF(outfile,
				"  dblop/s  rellop/s pagelop/s tranlop/s relxlop/s idxxlop/s misclop/s ");

	if (flag & SW_I_WAIT) {
		FPRINTF(outfile,
				"   wait/s  reject/s timeout/s blckast/s  dirsig/s  indsig/s ");
		FPRINTF(outfile, " wakeup/s dlkscan/s deadlck/s avlckwait(msec)");
	}

	FPRINTF(outfile, "\n");

	lhb base = *header;
	lhb prior = *header;
	if (intervals == 0)
		memset(&base, 0, sizeof(base));

	for (ULONG i = 0; i < intervals; i++) {
#ifdef WIN_NT
		Sleep((DWORD) seconds * 1000);
#else
		sleep(seconds);
#endif
		clock = time(NULL);
		d = *localtime(&clock);

		FPRINTF(outfile, "%02d:%02d:%02d ", d.tm_hour, d.tm_min, d.tm_sec);

		if (flag & SW_I_ACQUIRE) {
			FPRINTF(outfile, "%9"UQUADFORMAT" %9"UQUADFORMAT" %9"UQUADFORMAT
					" %9"UQUADFORMAT" %9"UQUADFORMAT" ",
					(header->lhb_acquires - prior.lhb_acquires) / seconds,
					(header->lhb_acquire_blocks -
					 prior.lhb_acquire_blocks) / seconds,
					(header->lhb_acquires -
					 prior.lhb_acquires) ? (100 *
											(header->lhb_acquire_blocks -
											 prior.lhb_acquire_blocks)) /
					(header->lhb_acquires - prior.lhb_acquires) : 0,
					(header->lhb_acquire_retries -
					 prior.lhb_acquire_retries) / seconds,
					(header->lhb_retry_success -
					 prior.lhb_retry_success) / seconds);

			prior.lhb_acquires = header->lhb_acquires;
			prior.lhb_acquire_blocks = header->lhb_acquire_blocks;
			prior.lhb_acquire_retries = header->lhb_acquire_retries;
			prior.lhb_retry_success = header->lhb_retry_success;
		}

		if (flag & SW_I_OPERATION) {
			FPRINTF(outfile, "%9"UQUADFORMAT" %9"UQUADFORMAT" %9"UQUADFORMAT
					" %9"UQUADFORMAT" %9"UQUADFORMAT" %9"UQUADFORMAT
					" %9"UQUADFORMAT" ",
					(header->lhb_enqs - prior.lhb_enqs) / seconds,
					(header->lhb_converts - prior.lhb_converts) / seconds,
					(header->lhb_downgrades - prior.lhb_downgrades) / seconds,
					(header->lhb_deqs - prior.lhb_deqs) / seconds,
					(header->lhb_read_data - prior.lhb_read_data) / seconds,
					(header->lhb_write_data - prior.lhb_write_data) / seconds,
					(header->lhb_query_data -
					 prior.lhb_query_data) / seconds);

			prior.lhb_enqs = header->lhb_enqs;
			prior.lhb_converts = header->lhb_converts;
			prior.lhb_downgrades = header->lhb_downgrades;
			prior.lhb_deqs = header->lhb_deqs;
			prior.lhb_read_data = header->lhb_read_data;
			prior.lhb_write_data = header->lhb_write_data;
			prior.lhb_query_data = header->lhb_query_data;
		}

		if (flag & SW_I_TYPE) {
			FPRINTF(outfile, "%9"UQUADFORMAT" %9"UQUADFORMAT" %9"UQUADFORMAT
					" %9"UQUADFORMAT" %9"UQUADFORMAT" %9"UQUADFORMAT
					" %9"UQUADFORMAT" ",
					(header->lhb_operations[Jrd::LCK_database] -
					 prior.lhb_operations[Jrd::LCK_database]) / seconds,
					(header->lhb_operations[Jrd::LCK_relation] -
					 prior.lhb_operations[Jrd::LCK_relation]) / seconds,
					(header->lhb_operations[Jrd::LCK_bdb] -
					 prior.lhb_operations[Jrd::LCK_bdb]) / seconds,
					(header->lhb_operations[Jrd::LCK_tra] -
					 prior.lhb_operations[Jrd::LCK_tra]) / seconds,
					(header->lhb_operations[Jrd::LCK_rel_exist] -
					 prior.lhb_operations[Jrd::LCK_rel_exist]) / seconds,
					(header->lhb_operations[Jrd::LCK_idx_exist] -
					 prior.lhb_operations[Jrd::LCK_idx_exist]) / seconds,
					(header->lhb_operations[0] -
					 prior.lhb_operations[0]) / seconds);

			prior.lhb_operations[Jrd::LCK_database] =
				header->lhb_operations[Jrd::LCK_database];
			prior.lhb_operations[Jrd::LCK_relation] =
				header->lhb_operations[Jrd::LCK_relation];
			prior.lhb_operations[Jrd::LCK_bdb] = header->lhb_operations[Jrd::LCK_bdb];
			prior.lhb_operations[Jrd::LCK_tra] = header->lhb_operations[Jrd::LCK_tra];
			prior.lhb_operations[Jrd::LCK_rel_exist] =
				header->lhb_operations[Jrd::LCK_rel_exist];
			prior.lhb_operations[Jrd::LCK_idx_exist] =
				header->lhb_operations[Jrd::LCK_idx_exist];
			prior.lhb_operations[0] = header->lhb_operations[0];
		}

		if (flag & SW_I_WAIT) {
			FPRINTF(outfile, "%9"UQUADFORMAT" %9"UQUADFORMAT" %9"UQUADFORMAT
					" %9"UQUADFORMAT" %9"UQUADFORMAT" %9"UQUADFORMAT
					" %9"UQUADFORMAT" ",
					(header->lhb_waits - prior.lhb_waits) / seconds,
					(header->lhb_denies - prior.lhb_denies) / seconds,
					(header->lhb_timeouts - prior.lhb_timeouts) / seconds,
					(header->lhb_blocks - prior.lhb_blocks) / seconds,
					(header->lhb_wakeups - prior.lhb_wakeups) / seconds,
					(header->lhb_scans - prior.lhb_scans) / seconds,
					(header->lhb_deadlocks - prior.lhb_deadlocks) / seconds);

			prior.lhb_waits = header->lhb_waits;
			prior.lhb_denies = header->lhb_denies;
			prior.lhb_timeouts = header->lhb_timeouts;
			prior.lhb_blocks = header->lhb_blocks;
			prior.lhb_wakeups = header->lhb_wakeups;
			prior.lhb_scans = header->lhb_scans;
			prior.lhb_deadlocks = header->lhb_deadlocks;
		}

		FPRINTF(outfile, "\n");
	}

	ULONG factor = seconds * intervals;
	if (factor < 1)
		factor = 1;

	FPRINTF(outfile, "\nAverage: ");
	if (flag & SW_I_ACQUIRE) {
		FPRINTF(outfile, "%9"UQUADFORMAT" %9"UQUADFORMAT" %9"UQUADFORMAT
				" %9"UQUADFORMAT" %9"UQUADFORMAT" ",
				(header->lhb_acquires - base.lhb_acquires) / (factor),
				(header->lhb_acquire_blocks -
				 base.lhb_acquire_blocks) / (factor),
				(header->lhb_acquires -
				 base.lhb_acquires) ? (100 * (header->lhb_acquire_blocks -
											  base.lhb_acquire_blocks)) /
				(header->lhb_acquires - base.lhb_acquires) : 0,
				(header->lhb_acquire_retries -
				 base.lhb_acquire_retries) / (factor),
				(header->lhb_retry_success -
				 base.lhb_retry_success) / (factor));
	}

	if (flag & SW_I_OPERATION) {
		FPRINTF(outfile, "%9"UQUADFORMAT" %9"UQUADFORMAT" %9"UQUADFORMAT
				" %9"UQUADFORMAT" %9"UQUADFORMAT" %9"UQUADFORMAT" %9"
				UQUADFORMAT" ",
				(header->lhb_enqs - base.lhb_enqs) / (factor),
				(header->lhb_converts - base.lhb_converts) / (factor),
				(header->lhb_downgrades - base.lhb_downgrades) / (factor),
				(header->lhb_deqs - base.lhb_deqs) / (factor),
				(header->lhb_read_data - base.lhb_read_data) / (factor),
				(header->lhb_write_data - base.lhb_write_data) / (factor),
				(header->lhb_query_data - base.lhb_query_data) / (factor));
	}

	if (flag & SW_I_TYPE) {
		FPRINTF(outfile, "%9"UQUADFORMAT" %9"UQUADFORMAT" %9"UQUADFORMAT
				" %9"UQUADFORMAT" %9"UQUADFORMAT" %9"UQUADFORMAT
				" %9"UQUADFORMAT" ",
				(header->lhb_operations[Jrd::LCK_database] -
				 base.lhb_operations[Jrd::LCK_database]) / (factor),
				(header->lhb_operations[Jrd::LCK_relation] -
				 base.lhb_operations[Jrd::LCK_relation]) / (factor),
				(header->lhb_operations[Jrd::LCK_bdb] -
				 base.lhb_operations[Jrd::LCK_bdb]) / (factor),
				(header->lhb_operations[Jrd::LCK_tra] -
				 base.lhb_operations[Jrd::LCK_tra]) / (factor),
				(header->lhb_operations[Jrd::LCK_rel_exist] -
				 base.lhb_operations[Jrd::LCK_rel_exist]) / (factor),
				(header->lhb_operations[Jrd::LCK_idx_exist] -
				 base.lhb_operations[Jrd::LCK_idx_exist]) / (factor),
				(header->lhb_operations[0] -
				 base.lhb_operations[0]) / (factor));
	}

	if (flag & SW_I_WAIT) {
		FPRINTF(outfile, "%9"UQUADFORMAT" %9"UQUADFORMAT" %9"UQUADFORMAT
				" %9"UQUADFORMAT" %9"UQUADFORMAT" %9"UQUADFORMAT
				" %9"UQUADFORMAT" ",
				(header->lhb_waits - base.lhb_waits) / (factor),
				(header->lhb_denies - base.lhb_denies) / (factor),
				(header->lhb_timeouts - base.lhb_timeouts) / (factor),
				(header->lhb_blocks - base.lhb_blocks) / (factor),
				(header->lhb_wakeups - base.lhb_wakeups) / (factor),
				(header->lhb_scans - base.lhb_scans) / (factor),
				(header->lhb_deadlocks - base.lhb_deadlocks) / (factor));
	}

	FPRINTF(outfile, "\n");
}


static void prt_lock_init(void*, sh_mem*, bool)
{
/**************************************
 *
 *      l o c k _ i n i t
 *
 **************************************
 *
 * Functional description
 *      Initialize a lock table to looking -- i.e. don't do
 *      nuthin.
 *
 **************************************/
}


static void prt_history(
						OUTFILE outfile,
						const lhb* LOCK_header,
						SRQ_PTR history_header,
						const SCHAR* title)
{
/**************************************
 *
 *      p r t _ h i s t o r y
 *
 **************************************
 *
 * Functional description
 *      Print history list of lock table.
 *
 **************************************/
	FPRINTF(outfile, "%s:\n", title);

	for (const his* history = (his*) SRQ_ABS_PTR(history_header); true;
		 history = (his*) SRQ_ABS_PTR(history->his_next))
	{
		if (history->his_operation)
			FPRINTF(outfile,
					"    %s:\towner = %s, lock = %s, request = %s\n",
					history_names[history->his_operation],
					(const TEXT*)HtmlLink(preOwn, history->his_process), 
					(const TEXT*)HtmlLink(preLock, history->his_lock),
					(const TEXT*)HtmlLink(preRequest, history->his_request));
		if (history->his_next == history_header)
			break;
	}
}


static void prt_lock(
					 OUTFILE outfile,
					 const lhb* LOCK_header, lbl* lock, USHORT sw_series)
{
/**************************************
 *
 *      p r t _ l o c k
 *
 **************************************
 *
 * Functional description
 *      Print a formatted lock block 
 *
 **************************************/
	if (sw_series && lock->lbl_series != sw_series)
		return;

	if (!sw_html_format)
		FPRINTF(outfile, "LOCK BLOCK %6"SLONGFORMAT"\n", SRQ_REL_PTR(lock));
	else
	{
		const SLONG rel_lock = SRQ_REL_PTR(lock);
		FPRINTF(outfile, "<a name=\"%s%"SLONGFORMAT"\">LOCK BLOCK %6"SLONGFORMAT"</a>\n",
				preLock, rel_lock, rel_lock);
	}
	FPRINTF(outfile,
			"\tSeries: %d, Parent: %s, State: %d, size: %d length: %d data: %"ULONGFORMAT"\n",
			lock->lbl_series, (const TEXT*)HtmlLink(preLock, lock->lbl_parent), lock->lbl_state,
			lock->lbl_size, lock->lbl_length, lock->lbl_data);

	if ((lock->lbl_series == Jrd::LCK_bdb || lock->lbl_series == Jrd::LCK_btr_dont_gc) && 
		lock->lbl_length == Jrd::PageNumber::getLockLen()) 
	{
		// Since fb 2.1 lock keys for page numbers (series == 3) contains
		// page space number in high long of two-longs key. Lets print it
		// in <page_space>:<page_number> format
		const UCHAR* q = lock->lbl_key;
		
		SLONG key;
		memcpy(&key, q, sizeof(SLONG));
		q += sizeof(SLONG);

		ULONG pg_space;
		memcpy(&pg_space, q, sizeof(SLONG));

		FPRINTF(outfile, "\tKey: %04"ULONGFORMAT":%06"SLONGFORMAT",", pg_space, key);
	}
	else if (lock->lbl_length == 4) {
		SLONG key;
		memcpy(&key, lock->lbl_key, 4);

		FPRINTF(outfile, "\tKey: %06"SLONGFORMAT",", key);
	}
	else {
		UCHAR temp[512];
		fb_assert(sizeof(temp) >= lock->lbl_length + 1); // Not enough, see <%d> below.
		UCHAR* p = temp;
		const UCHAR* end_temp = p + sizeof(temp) - 1;
  		const UCHAR* q = lock->lbl_key;
  		const UCHAR* const end = q + lock->lbl_length;
		for (; q < end && p < end_temp; q++) {
			const UCHAR c = *q;
			if ((c >= 'a' && c <= 'z') ||
				(c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/')
			{
				*p++ = c;
			}
			else
			{
				char buf[6] = "";
				int n = sprintf(buf, "<%d>", c);
				if (n < 1 || p + n >= end_temp)
				{
					while (p < end_temp)
						*p++ = '.';

					break;
				}
				memcpy(p, buf, n);
				p += n;
			}
		}
		*p = 0;
		FPRINTF(outfile, "\tKey: %s,", temp);
	}

	FPRINTF(outfile, " Flags: 0x%02X, Pending request count: %6d\n",
			lock->lbl_flags, lock->lbl_pending_lrq_count);

	prt_que(outfile, LOCK_header, "\tHash que", &lock->lbl_lhb_hash,
			OFFSET(lbl*, lbl_lhb_hash));

	prt_que(outfile, LOCK_header, "\tRequests", &lock->lbl_requests,
			OFFSET(lrq*, lrq_lbl_requests));

	const srq* que_inst;
	SRQ_LOOP(lock->lbl_requests, que_inst) {
		const lrq* request = (lrq*) ((UCHAR*) que_inst - OFFSET(lrq*, lrq_lbl_requests));
		FPRINTF(outfile,
				"\t\tRequest %s, Owner: %s, State: %d (%d), Flags: 0x%02X\n",
				(const TEXT*)HtmlLink(preRequest, SRQ_REL_PTR(request)), 
				(const TEXT*)HtmlLink(preOwn, request->lrq_owner), request->lrq_state,
				request->lrq_requested, request->lrq_flags);
	}

	FPRINTF(outfile, "\n");
}


static void prt_owner(OUTFILE outfile,
					  const lhb* LOCK_header,
					  const own* owner,
					  bool sw_requests,
					  bool sw_waitlist)
{
/**************************************
 *
 *      p r t _ o w n e r
 *
 **************************************
 *
 * Functional description
 *      Print a formatted owner block.
 *
 **************************************/
	const prc* process = (prc*) SRQ_ABS_PTR(owner->own_process);

	if (!sw_html_format)
		FPRINTF(outfile, "OWNER BLOCK %6"SLONGFORMAT"\n", SRQ_REL_PTR(owner));
	else
	{
		const SLONG rel_owner = SRQ_REL_PTR(owner);
		FPRINTF(outfile, "<a name=\"%s%"SLONGFORMAT"\">OWNER BLOCK %6"SLONGFORMAT"</a>\n", 
				preOwn, rel_owner, rel_owner);
	}
	FPRINTF(outfile, "\tOwner id: %6"QUADFORMAT"d, type: %1d, pending: %s\n",
			owner->own_owner_id, owner->own_owner_type,
			(const TEXT*)HtmlLink(preRequest, owner->own_pending_request));
	
	FPRINTF(outfile, "\tProcess id: %6d (%s), thread id: %6d\n",
			process->prc_process_id,
			ISC_check_process_existence(process->prc_process_id) ? "Alive" : "Dead",
			owner->own_thread_id);
	{
		const USHORT flags = owner->own_flags;
		FPRINTF(outfile, "\tFlags: 0x%02X ", flags);
		FPRINTF(outfile, " %s", (flags & OWN_blocking) ? "blkg" : "    ");
		FPRINTF(outfile, " %s", (flags & OWN_wakeup) ? "wake" : "    ");
		FPRINTF(outfile, " %s", (flags & OWN_scanned) ? "scan" : "    ");
		FPRINTF(outfile, " %s", (flags & OWN_waiting) ? "wait" : "    ");
		FPRINTF(outfile, " %s", (flags & OWN_signaled) ? "sgnl" : "    ");
		FPRINTF(outfile, "\n");
	}

	prt_que(outfile, LOCK_header, "\tRequests", &owner->own_requests,
			OFFSET(lrq*, lrq_own_requests));
	prt_que(outfile, LOCK_header, "\tBlocks", &owner->own_blocks,
			OFFSET(lrq*, lrq_own_blocks));

	if (sw_waitlist) {
		waitque owner_list;
		owner_list.waitque_depth = 0;
		prt_owner_wait_cycle(outfile, LOCK_header, owner, 8, &owner_list);
	}

	FPRINTF(outfile, "\n");

	if (sw_requests) {
		const srq* que_inst;
		SRQ_LOOP(owner->own_requests, que_inst)
			prt_request(outfile, LOCK_header,
						(lrq*) ((UCHAR *) que_inst -
							   OFFSET(lrq*, lrq_own_requests)));
	}
}


static void prt_owner_wait_cycle(
								 OUTFILE outfile,
								 const lhb* LOCK_header,
								 const own* owner,
								 USHORT indent, waitque *waiters)
{
/**************************************
 *
 *      p r t _ o w n e r _ w a i t _ c y c l e
 *
 **************************************
 *
 * Functional description
 *	For the given owner, print out the list of owners
 *	being waited on.  The printout is recursive, up to
 *	a limit.  It is recommended this be used with 
 *	the -c consistency mode.
 *
 **************************************/
	USHORT i;

	for (i = indent; i; i--)
		FPRINTF(outfile, " ");

/* Check to see if we're in a cycle of owners - this might be
   a deadlock, or might not, if the owners haven't processed
   their blocking queues */

	for (i = 0; i < waiters->waitque_depth; i++)
		if (SRQ_REL_PTR(owner) == waiters->waitque_entry[i]) {
			FPRINTF(outfile, "%s (potential deadlock).\n",
					(const TEXT*)HtmlLink(preOwn, SRQ_REL_PTR(owner)));
			return;
		}

	FPRINTF(outfile, "%s waits on ", (const TEXT*)HtmlLink(preOwn, SRQ_REL_PTR(owner)));

	if (!owner->own_pending_request)
		FPRINTF(outfile, "nothing.\n");
	else {
		if (waiters->waitque_depth > FB_NELEM(waiters->waitque_entry)) {
			FPRINTF(outfile, "Dependency too deep\n");
			return;
		}

		waiters->waitque_entry[waiters->waitque_depth++] = SRQ_REL_PTR(owner);

		FPRINTF(outfile, "\n");
		const lrq* owner_request = (lrq*) SRQ_ABS_PTR(owner->own_pending_request);
		fb_assert(owner_request->lrq_type == type_lrq);
		const bool owner_conversion = (owner_request->lrq_state > LCK_null);

		const lbl* lock = (lbl*) SRQ_ABS_PTR(owner_request->lrq_lock);
		fb_assert(lock->lbl_type == type_lbl);

		int counter = 0;
		const srq* que_inst;
		SRQ_LOOP(lock->lbl_requests, que_inst) {

			if (counter++ > 50) {
				for (i = indent + 6; i; i--)
					FPRINTF(outfile, " ");
				FPRINTF(outfile, "printout stopped after %d owners\n",
						counter - 1);
				break;
			}

			const lrq* lock_request =
				(lrq*) ((UCHAR *) que_inst - OFFSET(lrq*, lrq_lbl_requests));
			fb_assert(lock_request->lrq_type == type_lrq);


			if (LOCK_header->lhb_flags & LHB_lock_ordering && !owner_conversion) {

				/* Requests AFTER our request can't block us */
				if (owner_request == lock_request)
					break;

				if (compatibility[owner_request->lrq_requested]
								[MAX(lock_request->lrq_state, lock_request->lrq_requested)])
				{
					continue;
				}
			}
			else {

				/* Requests AFTER our request CAN block us */
				if (lock_request == owner_request)
					continue;

				if (compatibility[owner_request->lrq_requested][lock_request->lrq_state])
					continue;
			}
			const own* lock_owner = (own*) SRQ_ABS_PTR(lock_request->lrq_owner);
			prt_owner_wait_cycle(outfile, LOCK_header, lock_owner, indent + 4,
								 waiters);
		}
		waiters->waitque_depth--;
	}
}


static void prt_request(OUTFILE outfile, const lhb* LOCK_header, const lrq* request)
{
/**************************************
 *
 *      p r t _ r e q u e s t
 *
 **************************************
 *
 * Functional description
 *      Print a format request block.
 *
 **************************************/

	if (!sw_html_format)
		FPRINTF(outfile, "REQUEST BLOCK %6"SLONGFORMAT"\n", SRQ_REL_PTR(request));
	else
	{
		const SLONG rel_request = SRQ_REL_PTR(request);
		FPRINTF(outfile, "<a name=\"%s%"SLONGFORMAT"\">REQUEST BLOCK %6"SLONGFORMAT"</a>\n", 
				preRequest, rel_request, rel_request);
	}
	FPRINTF(outfile, "\tOwner: %s, Lock: %s, State: %d, Mode: %d, Flags: 0x%02X\n",
			(const TEXT*)HtmlLink(preOwn, request->lrq_owner), 
			(const TEXT*)HtmlLink(preLock, request->lrq_lock), request->lrq_state,
			request->lrq_requested, request->lrq_flags);
	FPRINTF(outfile, "\tAST: 0x%p, argument: 0x%p\n",
			request->lrq_ast_routine, request->lrq_ast_argument);
	prt_que2(outfile, LOCK_header, "\tlrq_own_requests",
			 &request->lrq_own_requests, OFFSET(lrq*, lrq_own_requests));
	prt_que2(outfile, LOCK_header, "\tlrq_lbl_requests",
			 &request->lrq_lbl_requests, OFFSET(lrq*, lrq_lbl_requests));
	prt_que2(outfile, LOCK_header, "\tlrq_own_blocks  ",
			 &request->lrq_own_blocks, OFFSET(lrq*, lrq_own_blocks));
	FPRINTF(outfile, "\n");
}


static void prt_que(
					OUTFILE outfile,
					const lhb* LOCK_header,
					const SCHAR* string, const srq* que_inst, USHORT que_offset)
{
/**************************************
 *
 *      p r t _ q u e
 *
 **************************************
 *
 * Functional description
 *      Print the contents of a self-relative que.
 *
 **************************************/
	const SLONG offset = SRQ_REL_PTR(que_inst);

	if (offset == que_inst->srq_forward && offset == que_inst->srq_backward) {
		FPRINTF(outfile, "%s: *empty*\n", string);
		return;
	}

	SLONG count = 0;
	const srq* next;
	SRQ_LOOP((*que_inst), next)
		++count;

	FPRINTF(outfile, "%s (%ld):\tforward: %6"SLONGFORMAT
			", backward: %6"SLONGFORMAT"\n", string, count,
			que_inst->srq_forward - que_offset, que_inst->srq_backward - que_offset);
}


static void prt_que2(
					 OUTFILE outfile,
					 const lhb* LOCK_header,
					 const SCHAR* string, const srq* que_inst, USHORT que_offset)
{
/**************************************
 *
 *      p r t _ q u e 2
 *
 **************************************
 *
 * Functional description
 *      Print the contents of a self-relative que.
 *      But don't try to count the entries, as they might be invalid
 *
 **************************************/
	const SLONG offset = SRQ_REL_PTR(que_inst);

	if (offset == que_inst->srq_forward && offset == que_inst->srq_backward) {
		FPRINTF(outfile, "%s: *empty*\n", string);
		return;
	}

	FPRINTF(outfile, "%s:\tforward: %6"SLONGFORMAT
			", backward: %6"SLONGFORMAT"\n", string,
			que_inst->srq_forward - que_offset, que_inst->srq_backward - que_offset);
}

static void prt_html_begin(OUTFILE outfile)
{
	/**************************************
	 *
	 *      p r t _ h t m l _ b e g i n 
	 *
	 **************************************
	 *
	 * Functional description
	 *      Print the html header if heeded
	 *
	 **************************************/
	if (!sw_html_format)
		return;
	
	FPRINTF(outfile, "<html><head><title>%s</title></head><body>", "Lock table");
	FPRINTF(outfile, "<pre>");
	
}

static void prt_html_end(OUTFILE outfile)
{
	/**************************************
	 *
	 *      p r t _ h t m l _ e n d
	 *
	 **************************************
	 *
	 * Functional description
	 *      Print the html finishing items
	 *
	 **************************************/
	if (!sw_html_format)
		return;
	
	FPRINTF(outfile, "</pre>");
	FPRINTF(outfile, "</body></html>");
}