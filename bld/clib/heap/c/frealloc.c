/****************************************************************************
*
*                            Open Watcom Project
*
*    Portions Copyright (c) 1983-2002 Sybase, Inc. All Rights Reserved.
*
*  ========================================================================
*
*    This file contains Original Code and/or Modifications of Original
*    Code as defined in and that are subject to the Sybase Open Watcom
*    Public License version 1.0 (the 'License'). You may not use this file
*    except in compliance with the License. BY USING THIS FILE YOU AGREE TO
*    ALL TERMS AND CONDITIONS OF THE LICENSE. A copy of the License is
*    provided with the Original Code and Modifications, and is also
*    available at www.sybase.com/developer/opensource.
*
*    The Original Code and all software distributed under the License are
*    distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
*    EXPRESS OR IMPLIED, AND SYBASE AND ALL CONTRIBUTORS HEREBY DISCLAIM
*    ALL SUCH WARRANTIES, INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF
*    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR
*    NON-INFRINGEMENT. Please see the License for the specific language
*    governing rights and limitations under the License.
*
*  ========================================================================
*
* Description:  Far heap reallocation routines.
*               (16-bit code only)
*
****************************************************************************/


#include "dll.h"        // needs to be first
#include "variety.h"
#include <stddef.h>
#include <malloc.h>
#include <string.h>
#include "heap.h"


extern void _WCNEAR *_mymemcpy( void _WCFAR *, void _WCFAR *, size_t );
#pragma aux _mymemcpy = \
        "push ds"       \
        "mov ds,dx"     \
        "shr cx,1"      \
        "rep movsw"     \
        "adc cx,cx"     \
        "rep movsb"     \
        "pop ds"        \
    parm caller [es di] [dx si] [cx] \
        value [si] modify exact [si di cx]

#if defined(__BIG_DATA__)

_WCRTLINK void *realloc( void *cstg, size_t amount )
{
    return( _frealloc( cstg, amount ) );
}

#endif


_WCRTLINK void _WCFAR *_frealloc( void _WCFAR *cstg_old, size_t req_size )
{
    size_t  old_size;
    void    _WCFAR *cstg_new;

    if( cstg_old == FAR_NULL ) {
        return( _fmalloc( req_size ) );
    }
    if( req_size == 0 ) {
        _ffree( cstg_old );
        return( FAR_NULL );
    }
    old_size = _fmsize( cstg_old );
    if( FP_SEG( cstg_old ) == _DGroup() ) {
        cstg_new = cstg_old;
        if( _nexpand( (void _WCNEAR *)cstg_old, req_size ) == NEAR_NULL ) {
            cstg_new = FAR_NULL;
        }
    } else {
        cstg_new = _fexpand( cstg_old, req_size );
    }
    if( cstg_new == FAR_NULL ) {        /* couldn't be expanded inline */
        cstg_new = _fmalloc( req_size );
        if( cstg_new != FAR_NULL ) {
            _mymemcpy( cstg_new, cstg_old, old_size );
            _ffree( cstg_old );
        } else {
            if( FP_SEG( cstg_old ) == _DGroup() ) {
                _nexpand( (void _WCNEAR *)FP_OFF( cstg_old ), old_size );
            } else {
                _fexpand( cstg_old, old_size );
            }
        }
    }
    return( cstg_new );
}
