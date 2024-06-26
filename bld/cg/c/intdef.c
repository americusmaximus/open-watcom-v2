/****************************************************************************
*
*                            Open Watcom Project
*
* Copyright (c) 2023-2024 The Open Watcom Contributors. All Rights Reserved.
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
* Description:  Order ints from host into 8086 format
*
****************************************************************************/


uint_16 TargetShort( uint_16 value )
{
    struct {
        union {
            char        b[2];
            uint_16     val;
        } u;
    } in, out;

    in.u.val = value;
    out.u.b[0] = in.u.b[1];
    out.u.b[1] = in.u.b[0];
    return( out.u.val );
}

int TargetOffset( int value )
{
    return( value );
}

uint_32 TargetBigInt( uint_32 value )
{
    struct {
        union {
            char        b[4];
            uint_32     val;
        } u;
    } in, out;

    in.u.val = value;
    out.u.b[0] = in.u.b[3];
    out.u.b[1] = in.u.b[2];
    out.u.b[2] = in.u.b[1];
    out.u.b[3] = in.u.b[0];
    return( out.u.val );
}

void TargAddL( uint_32 *targetw, uint_32 value )
{
    uint_32 out;

    out = TargetBigInt( *targetw ) + value;
    *targetw = TargetBigInt( out );
}

void TargAddW( uint_16 *targetw, uint_16 value )
{
    uint_16 out;

    out = TargetShort( *targetw ) + value;
    *targetw = TargetShort( out );
}
