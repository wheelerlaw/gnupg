/* comment.c - write comment stuff
 *	Copyright (C) 1998, 2003 Free Software Foundation, Inc.
 *
 * This file is part of GnuPG.
 *
 * GnuPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GnuPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "options.h"
#include "packet.h"
#include "errors.h"
#include "iobuf.h"
#include "memory.h"
#include "util.h"
#include "main.h"
#include "keydb.h"



int
write_comment( iobuf_t out, const char *s )
{
    PACKET pkt;
    size_t n = strlen(s);
    int rc=0;

    pkt.pkttype = PKT_COMMENT;
    if( *s != '#' ) {
       pkt.pkt.comment = xmalloc ( sizeof *pkt.pkt.comment + n );
       pkt.pkt.comment->len = n+1;
       *pkt.pkt.comment->data = '#';
       strcpy(pkt.pkt.comment->data+1, s);
    }
    else {
       pkt.pkt.comment = xmalloc ( sizeof *pkt.pkt.comment + n - 1 );
       pkt.pkt.comment->len = n;
       strcpy(pkt.pkt.comment->data, s);
    }
    if( (rc = build_packet( out, &pkt )) )
	log_error("build_packet(comment) failed: %s\n", gpg_strerror (rc) );
    free_packet( &pkt );
    return rc;
}


KBNODE
make_comment_node( const char *s )
{
    PACKET *pkt;
    size_t n = strlen(s);

    pkt = xcalloc (1, sizeof *pkt );
    pkt->pkttype = PKT_COMMENT;
    pkt->pkt.comment = xmalloc ( sizeof *pkt->pkt.comment + n - 1 );
    pkt->pkt.comment->len = n;
    strcpy(pkt->pkt.comment->data, s);
    return new_kbnode( pkt );
}


KBNODE
make_mpi_comment_node( const char *s, gcry_mpi_t a )
{
    PACKET *pkt;
    byte *buf, *pp;
    size_t n1, nb1;
    size_t n = strlen(s);

    nb1 = mpi_get_nbits( a );
    if (gcry_mpi_print (GCRYMPI_FMT_PGP, NULL, &n1, a))
      BUG ();
    /* fixme: allocate it on the stack */
    buf = xmalloc (n1);
    if (gcry_mpi_print (GCRYMPI_FMT_PGP, buf, &n1, a))
      BUG ();

    pkt = xcalloc (1, sizeof *pkt );
    pkt->pkttype = PKT_COMMENT;
    pkt->pkt.comment = xmalloc ( sizeof *pkt->pkt.comment + n + 2 + n1 );
    pkt->pkt.comment->len = n+1+2+n1;
    pp = pkt->pkt.comment->data;
    memcpy(pp, s, n+1);
    memcpy(pp+n+1, buf, n1 );
    xfree (buf);
    return new_kbnode( pkt );
}


