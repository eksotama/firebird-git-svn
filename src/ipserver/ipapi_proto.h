/*
 * PROGRAM: JRD Access Method
 * MODULE: ipapi_proto.h
 * DESCRIPTION: Prototype header file for ipclient.c api calls
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
 */


#ifndef IPAPI_PROTO_H
#define IPAPI_PROTO_H

#ifdef __cplusplus
extern "C" {
#endif

extern ISC_STATUS DLL_EXPORT IPI_attach_database(ISC_STATUS *, SSHORT, SCHAR *,
											 struct idb **, SSHORT, SCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_blob_info(ISC_STATUS *, struct ibl **, SSHORT,
									   UCHAR *, SSHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_cancel_blob(ISC_STATUS *, struct ibl **);
extern ISC_STATUS DLL_EXPORT IPI_cancel_events(ISC_STATUS *, struct idb **, SLONG *);
extern ISC_STATUS DLL_EXPORT IPI_close_blob(ISC_STATUS *, struct ibl **);
extern ISC_STATUS DLL_EXPORT IPI_commit_transaction(ISC_STATUS *, struct itr **);
extern ISC_STATUS DLL_EXPORT IPI_commit_retaining(ISC_STATUS *, struct itr **);
extern ISC_STATUS DLL_EXPORT IPI_compile_request(ISC_STATUS *, struct idb **,
											 struct irq **, SSHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_create_blob(ISC_STATUS *, struct idb **,
										 struct itr **, struct ibl **,
										 struct bid *);
extern ISC_STATUS DLL_EXPORT IPI_create_blob2(ISC_STATUS *, struct idb **,
										  struct itr **, struct ibl **,
										  struct bid *, SSHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_create_database(ISC_STATUS *, SSHORT, SCHAR *,
											 struct idb **, SSHORT, SCHAR *,
											 SSHORT);
extern ISC_STATUS DLL_EXPORT IPI_database_info(ISC_STATUS *, struct idb **, SSHORT,
										   UCHAR *, SSHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_ddl(ISC_STATUS *, struct idb **, struct itr **,
								 SSHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_detach_database(ISC_STATUS *, struct idb **);
extern ISC_STATUS DLL_EXPORT IPI_drop_database(ISC_STATUS *, struct idb **);
extern ISC_STATUS DLL_EXPORT IPI_get_segment(ISC_STATUS *, struct ibl **, USHORT *,
										 USHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_get_slice(ISC_STATUS *, struct idb **, struct itr **,
									   struct bid *, USHORT, UCHAR *, USHORT,
									   UCHAR *, SLONG, UCHAR *, SLONG *);
extern ISC_STATUS DLL_EXPORT IPI_open_blob(ISC_STATUS *, struct idb **, struct itr **,
									   struct ibl **, struct bid *);
extern ISC_STATUS DLL_EXPORT IPI_open_blob2(ISC_STATUS *, struct idb **,
										struct itr **, struct ibl **,
										struct bid *, SSHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_prepare_transaction(ISC_STATUS *, struct itr **,
												 USHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_put_segment(ISC_STATUS *, struct ibl **, USHORT,
										 UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_put_slice(ISC_STATUS *, struct idb **, struct itr **,
									   struct bid *, USHORT, UCHAR *, USHORT,
									   UCHAR *, SLONG, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_que_events(ISC_STATUS *, struct idb **, SLONG *,
										USHORT, UCHAR *, FPTR_VOID, void *);
extern ISC_STATUS DLL_EXPORT IPI_receive(ISC_STATUS *, struct irq **, SSHORT, SSHORT,
									 UCHAR *, SSHORT
#ifdef SCROLLABLE_CURSORS
									 , USHORT, ULONG
#endif
	);
extern ISC_STATUS DLL_EXPORT IPI_reconnect_transaction(ISC_STATUS *, struct idb **,
												   struct itr **, SSHORT,
												   UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_release_request(ISC_STATUS *, struct irq **);
extern ISC_STATUS DLL_EXPORT IPI_request_info(ISC_STATUS *, struct irq **, USHORT,
										  SSHORT, UCHAR *, SSHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_rollback_transaction(ISC_STATUS *, struct itr **);
extern ISC_STATUS DLL_EXPORT IPI_rollback_retaining(ISC_STATUS *, struct itr **);
extern ISC_STATUS DLL_EXPORT IPI_seek_blob(ISC_STATUS *, struct ibl **, SSHORT, SLONG,
									   SLONG *);
extern ISC_STATUS DLL_EXPORT IPI_send(ISC_STATUS *, struct irq **, SSHORT, SSHORT,
								  UCHAR *, SSHORT);
extern ISC_STATUS DLL_EXPORT IPI_service_attach(ISC_STATUS *, USHORT, TEXT *,
											struct idb **, USHORT, SCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_service_detach(ISC_STATUS *, struct idb **);
extern ISC_STATUS DLL_EXPORT IPI_service_query(ISC_STATUS *, struct idb **, ULONG *,
										   USHORT, SCHAR *, USHORT, SCHAR *,
										   USHORT, SCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_service_start(ISC_STATUS *, struct idb **, ULONG *,
										   USHORT, SCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_start_request(ISC_STATUS *, struct irq **,
										   struct itr **, SSHORT);
extern ISC_STATUS DLL_EXPORT IPI_start_and_send(ISC_STATUS *, struct irq **,
											struct itr **, SSHORT, SSHORT,
											UCHAR *, SSHORT);
extern ISC_STATUS DLL_EXPORT IPI_start_multiple(ISC_STATUS *, struct itr **, SSHORT,
											int **);
extern ISC_STATUS DLL_EXPORT IPI_start_transaction(ISC_STATUS *, struct itr **,
											   SSHORT, ...);
extern ISC_STATUS DLL_EXPORT IPI_transaction_info(ISC_STATUS *, struct itr **, SSHORT,
											  UCHAR *, SSHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_transact_request(ISC_STATUS *, struct idb **,
											  struct itr **, USHORT, UCHAR *,
											  USHORT, UCHAR *, USHORT,
											  UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_unwind_request(ISC_STATUS *, struct irq **, SSHORT);

/* DSQL entrypoints */

extern ISC_STATUS DLL_EXPORT IPI_allocate_statement(ISC_STATUS *, struct idb **,
												struct ipserver_isr **);
extern ISC_STATUS DLL_EXPORT IPI_execute(ISC_STATUS *, struct itr **,
									 struct ipserver_isr **, USHORT, UCHAR *,
									 USHORT, USHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_execute2(ISC_STATUS *, struct itr **,
									  struct ipserver_isr **, USHORT, UCHAR *,
									  USHORT, USHORT, UCHAR *, USHORT,
									  UCHAR *, USHORT, USHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_execute_immediate(ISC_STATUS *, struct idb **,
											   struct itr **, USHORT, UCHAR *,
											   USHORT, USHORT, UCHAR *,
											   USHORT, USHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_execute_immediate2(ISC_STATUS *, struct idb **,
												struct itr **, USHORT,
												UCHAR *, USHORT, USHORT,
												UCHAR *, USHORT, USHORT,
												UCHAR *, USHORT, UCHAR *,
												USHORT, USHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_fetch(ISC_STATUS *, struct ipserver_isr **, USHORT,
								   UCHAR *, USHORT, USHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_free_statement(ISC_STATUS *, struct ipserver_isr **,
											USHORT);
extern ISC_STATUS DLL_EXPORT IPI_insert(ISC_STATUS *, struct ipserver_isr **, USHORT,
									UCHAR *, USHORT, USHORT, UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_prepare(ISC_STATUS *, struct itr **,
									 struct ipserver_isr **, USHORT, UCHAR *,
									 USHORT, USHORT, UCHAR *, USHORT,
									 UCHAR *);
extern ISC_STATUS DLL_EXPORT IPI_set_cursor_name(ISC_STATUS *, struct ipserver_isr **,
											 UCHAR *, USHORT);
extern ISC_STATUS DLL_EXPORT IPI_sql_info(ISC_STATUS *, struct ipserver_isr **,
									  SSHORT, UCHAR *, SSHORT, UCHAR *);


#ifdef __cplusplus
} /* extern "C" */
#endif


#endif /* IPAPI_PROTO_H */
