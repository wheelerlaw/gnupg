/* compress.c - compress filter
 * Copyright (C) 1998, 1999, 2000, 2001, 2002,
 *               2003 Free Software Foundation, Inc.
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
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <zlib.h>
#ifdef __riscos__
# include "zlib-riscos.h"
#endif

#include "gpg.h"
#include "util.h"
#include "memory.h"
#include "packet.h"
#include "filter.h"
#include "main.h"
#include "options.h"

static void
init_compress( compress_filter_context_t *zfx, z_stream *zs )
{
    int rc;
    int level;

#ifdef __riscos__
    static int zlib_initialized = 0;

    if (!zlib_initialized)
        zlib_initialized = riscos_load_module("ZLib", zlib_path, 1);
#endif

    if( opt.compress >= 0 && opt.compress <= 9 )
	level = opt.compress;
    else if( opt.compress == -1 )
	level = Z_DEFAULT_COMPRESSION;
    else if( opt.compress == 10 ) /* remove this ! */
	level = 0;
    else {
	log_error("invalid compression level; using default level\n");
	level = Z_DEFAULT_COMPRESSION;
    }


    if( (rc = zfx->algo == 1? deflateInit2( zs, level, Z_DEFLATED,
					    -13, 8, Z_DEFAULT_STRATEGY)
			    : deflateInit( zs, level )
			    ) != Z_OK ) {
	log_fatal("zlib problem: %s\n", zs->msg? zs->msg :
			       rc == Z_MEM_ERROR ? "out of core" :
			       rc == Z_VERSION_ERROR ? "invalid lib version" :
						       "unknown error" );
    }

    zfx->outbufsize = 8192;
    zfx->outbuf = xmalloc ( zfx->outbufsize );
}

static int
do_compress( compress_filter_context_t *zfx, z_stream *zs, int flush, iobuf_t a )
{
    gpg_error_t rc;
    int zrc;
    unsigned n;

    do {
#ifndef __riscos__
	zs->next_out = zfx->outbuf;
#else /* __riscos__ */
	zs->next_out = (Bytef *) zfx->outbuf;
#endif /* __riscos__ */
	zs->avail_out = zfx->outbufsize;
	if( DBG_FILTER )
	    log_debug("enter deflate: avail_in=%u, avail_out=%u, flush=%d\n",
		    (unsigned)zs->avail_in, (unsigned)zs->avail_out, flush );
	zrc = deflate( zs, flush );
	if( zrc == Z_STREAM_END && flush == Z_FINISH )
	    ;
	else if( zrc != Z_OK ) {
	    if( zs->msg )
		log_fatal("zlib deflate problem: %s\n", zs->msg );
	    else
		log_fatal("zlib deflate problem: rc=%d\n", zrc );
	}
	n = zfx->outbufsize - zs->avail_out;
	if( DBG_FILTER )
	    log_debug("leave deflate: "
		      "avail_in=%u, avail_out=%u, n=%u, zrc=%d\n",
		(unsigned)zs->avail_in, (unsigned)zs->avail_out,
					       (unsigned)n, zrc );

	rc = iobuf_write (a, zfx->outbuf, n);
        if (rc)
          {
	    log_debug("deflate: iobuf_write failed\n");
	    return rc;
          }
    } while( zs->avail_in || (flush == Z_FINISH && zrc != Z_STREAM_END) );
    return 0;
}

static void
init_uncompress( compress_filter_context_t *zfx, z_stream *zs )
{
    int rc;

    /****************
     * PGP uses a windowsize of 13 bits. Using a negative value for
     * it forces zlib not to expect a zlib header.  This is a
     * undocumented feature Peter Gutmann told me about.
     *    
     * We must use 15 bits for the inflator because CryptoEx uses 15
     * bits thus the output would get scrambled w/o error indication
     * if we would use 13 bits.  For the uncompressing this does not
     * matter at all.
     */
    if( (rc = zfx->algo == 1? inflateInit2( zs, -15)
			    : inflateInit( zs )) != Z_OK ) {
	log_fatal("zlib problem: %s\n", zs->msg? zs->msg :
			       rc == Z_MEM_ERROR ? "out of core" :
			       rc == Z_VERSION_ERROR ? "invalid lib version" :
						       "unknown error" );
    }

    zfx->inbufsize = 2048;
    zfx->inbuf = xmalloc ( zfx->inbufsize );
    zs->avail_in = 0;
}

static int
do_uncompress( compress_filter_context_t *zfx, z_stream *zs,
	       iobuf_t a, size_t *ret_len )
{
    int zrc;
    int rc=0;
    size_t n;
    int nread, count;
    int refill = !zs->avail_in;

    if( DBG_FILTER )
	log_debug("begin inflate: avail_in=%u, avail_out=%u, inbuf=%u\n",
		(unsigned)zs->avail_in, (unsigned)zs->avail_out,
		(unsigned)zfx->inbufsize );
    do {
	if( zs->avail_in < zfx->inbufsize && refill ) {
	    n = zs->avail_in;
	    if( !n )
#ifndef __riscos__
		zs->next_in = zfx->inbuf;
#else /* __riscos__ */
		zs->next_in = (Bytef *) zfx->inbuf;
#endif /* __riscos__ */
	    count = zfx->inbufsize - n;
	    nread = iobuf_read( a, zfx->inbuf + n, count );
	    if( nread == -1 ) nread = 0;
	    n += nread;
	    /* If we use the undocumented feature to suppress
	     * the zlib header, we have to give inflate an
	     * extra dummy byte to read */
	    if( nread < count && zfx->algo == 1 ) {
		*(zfx->inbuf + n) = 0xFF; /* is it really needed ? */
		zfx->algo1hack = 1;
		n++;
	    }
	    zs->avail_in = n;
	}
	refill = 1;
	if( DBG_FILTER )
	    log_debug("enter inflate: avail_in=%u, avail_out=%u\n",
		    (unsigned)zs->avail_in, (unsigned)zs->avail_out);
#ifdef Z_SYNC_FLUSH
	zrc = inflate( zs, Z_SYNC_FLUSH );
#else
	zrc = inflate( zs, Z_PARTIAL_FLUSH );
#endif
	if( DBG_FILTER )
	    log_debug("leave inflate: avail_in=%u, avail_out=%u, zrc=%d\n",
		   (unsigned)zs->avail_in, (unsigned)zs->avail_out, zrc);
	if( zrc == Z_STREAM_END )
	    rc = -1; /* eof */
	else if( zrc != Z_OK && zrc != Z_BUF_ERROR ) {
	    if( zs->msg )
		log_fatal("zlib inflate problem: %s\n", zs->msg );
	    else
		log_fatal("zlib inflate problem: rc=%d\n", zrc );
	}
    } while( zs->avail_out && zrc != Z_STREAM_END  && zrc != Z_BUF_ERROR );
    *ret_len = zfx->outbufsize - zs->avail_out;
    if( DBG_FILTER )
	log_debug("do_uncompress: returning %u bytes\n", (unsigned)*ret_len );
    return rc;
}

int
compress_filter( void *opaque, int control,
		 iobuf_t a, byte *buf, size_t *ret_len)
{
    size_t size = *ret_len;
    compress_filter_context_t *zfx = opaque;
    z_stream *zs = zfx->opaque;
    int rc=0;

    if( control == IOBUFCTRL_UNDERFLOW ) {
	if( !zfx->status ) {
	    zs = zfx->opaque = xcalloc (1, sizeof *zs );
	    init_uncompress( zfx, zs );
	    zfx->status = 1;
	}

#ifndef __riscos__
	zs->next_out = buf;
#else /* __riscos__ */
	zs->next_out = (Bytef *) buf;
#endif /* __riscos__ */
	zs->avail_out = size;
	zfx->outbufsize = size; /* needed only for calculation */
	rc = do_uncompress( zfx, zs, a, ret_len );
    }
    else if( control == IOBUFCTRL_FLUSH ) {
	if( !zfx->status ) {
	    PACKET pkt;
	    PKT_compressed cd;

	    if( !zfx->algo )
	        zfx->algo = DEFAULT_COMPRESS_ALGO;
	    if( zfx->algo != 1 && zfx->algo != 2 )
	      BUG();
	    memset( &cd, 0, sizeof cd );
	    cd.len = 0;
	    cd.algorithm = zfx->algo;
	    init_packet( &pkt );
	    pkt.pkttype = PKT_COMPRESSED;
	    pkt.pkt.compressed = &cd;
	    if( build_packet( a, &pkt ))
		log_bug("build_packet(PKT_COMPRESSED) failed\n");
	    zs = zfx->opaque = xcalloc (1, sizeof *zs );
	    init_compress( zfx, zs );
	    zfx->status = 2;
	}

#ifndef __riscos__
	zs->next_in = buf;
#else /* __riscos__ */
	zs->next_in = (Bytef *) buf;
#endif /* __riscos__ */
	zs->avail_in = size;
	rc = do_compress( zfx, zs, Z_NO_FLUSH, a );
    }
    else if( control == IOBUFCTRL_FREE ) {
	if( zfx->status == 1 ) {
	    inflateEnd(zs);
	    xfree (zs);
	    zfx->opaque = NULL;
	    xfree (zfx->outbuf); zfx->outbuf = NULL;
	}
	else if( zfx->status == 2 ) {
#ifndef __riscos__
	    zs->next_in = buf;
#else /* __riscos__ */
	    zs->next_in = (Bytef *) buf;
#endif /* __riscos__ */
	    zs->avail_in = 0;
	    do_compress( zfx, zs, Z_FINISH, a );
	    deflateEnd(zs);
	    xfree (zs);
	    zfx->opaque = NULL;
	    xfree (zfx->outbuf); zfx->outbuf = NULL;
	}
        if (zfx->release)
          zfx->release (zfx);
    }
    else if( control == IOBUFCTRL_DESC )
	*(char**)buf = "compress_filter";
    return rc;
}


static void
release_context (compress_filter_context_t *ctx)
{
  xfree (ctx);
}

/****************
 * Handle a compressed packet
 */
int
handle_compressed( void *procctx, PKT_compressed *cd,
		   int (*callback)(iobuf_t, void *), void *passthru )
{
    compress_filter_context_t *cfx;
    int rc;

    if( cd->algorithm < 1 || cd->algorithm > 2	)
	return GPG_ERR_COMPR_ALGO;
    cfx = xcalloc (1,sizeof *cfx);
    cfx->algo = cd->algorithm;
    cfx->release = release_context;
    iobuf_push_filter( cd->buf, compress_filter, cfx );
    if( callback )
	rc = callback(cd->buf, passthru );
    else
	rc = proc_packets(procctx, cd->buf);
    cd->buf = NULL;
    return rc;
}

