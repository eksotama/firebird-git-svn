/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		pio_proto.h
 *	DESCRIPTION:	Prototype header file for unix.cpp, vms.cpp & winnt.cpp
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

#ifndef JRD_PIO_PROTO_H
#define JRD_PIO_PROTO_H

class jrd_file;
class Database;
struct blk;

int		PIO_add_file(Database*, jrd_file*, const TEXT*, SLONG);
void	PIO_close(jrd_file*);
jrd_file*	PIO_create(Database*, const TEXT*, SSHORT, bool);
int		PIO_connection(const TEXT*, USHORT*);
int		PIO_expand(const TEXT*, USHORT, TEXT*);
void	PIO_flush(jrd_file*);
void	PIO_force_write(jrd_file*, bool);
void	PIO_header(Database*, SCHAR*, int);
SLONG	PIO_max_alloc(Database*);
SLONG	PIO_act_alloc(Database*);
jrd_file*	PIO_open(Database*, const TEXT*, SSHORT, bool,
							blk*, const TEXT*, USHORT);
bool	PIO_read(jrd_file*, class BufferDesc*, struct pag*, ISC_STATUS*);

#ifdef SUPERSERVER_V2
bool	PIO_read_ahead(Database*, SLONG, SCHAR*, SLONG, struct phys_io_blk*,
						  ISC_STATUS*);
bool		PIO_status(struct phys_io_blk*, ISC_STATUS*);
#endif

int		PIO_unlink(const TEXT*);
bool	PIO_write(jrd_file*, class BufferDesc*, struct pag*, ISC_STATUS*);

#endif // JRD_PIO_PROTO_H

