/****************************************************************************
*
*                            Open Watcom Project
*
* Copyright (c) 2002-2024 The Open Watcom Contributors. All Rights Reserved.
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
* Description:  WHEN YOU FIGURE OUT WHAT THIS FILE DOES, PLEASE
*               DESCRIBE IT HERE!
*
****************************************************************************/


#include "plusplus.h"

#include <stddef.h>
#include <limits.h>

#include "ptree.h"
#include "cgfront.h"
#include "fold.h"
#include "codegen.h"
#include "floatsup.h"


static target_long FoldSignedRShiftMax( target_long v )
/*****************************************************/
{
    if( v < 0 ) {
        return( -1 );
    }
    return( 0 );
}


static bool isCondDecor(        // TEST IF CONDITIONALLY DECORATED
    PTREE node )                // - the expression
{
    bool ok;                    // - true ==> conditionally decorated

    ok = false;
    if( NodeIsBinaryOp( node, CO_COMMA ) ) {
        node = node->u.subtree[0];
        if( node->op == PT_IC ) {
            if( node->u.ic.opcode == IC_COND_TRUE
             || node->u.ic.opcode == IC_COND_FALSE ) {
                ok = true;
            }
        }
    }
    return( ok );
}


static PTREE overCondDecor(     // BY-PASS CONDITIONAL DECORATION
    PTREE expr )                // - expression
{
    if( isCondDecor( expr ) ) {
        expr = expr->u.subtree[1];
    }
    return( expr );
}


bool Zero64                     // TEST IF 64-BITTER IS ZERO
    ( signed_64 const *test )   // - value to be tested
{
    return( test->u._32[0] == 0 && test->u._32[1] == 0 );
}

/**
 * Check if node is or evaluates to a zero constant for folding purposes.
 *
 * \param expr The expression to be analyzed.
 */
static bool zeroConstant( PTREE expr )
{
    PTREE orig;

    switch( expr->op ) {
    // For boolean folding purposes, nullptr evaluates to zero (false).
    case PT_PTR_CONSTANT:
        return( true );
    case PT_INT_CONSTANT:
        if( NULL == Integral64Type( expr->type ) ) {
            return( expr->u.int_constant == 0 );
        } else {
            return( Zero64( &expr->u.int64_constant ) );
        }
    case PT_FLOATING_CONSTANT:
      { target_ulong ul_val = CFCnvF32( expr->u.floating_constant );
        return( 0 == ul_val );
      }
    case PT_BINARY:
        orig = expr;
        expr = NodeRemoveCasts( expr );
        if( expr == orig )
            break;
        return( zeroConstant( expr ) );
    }
    return( false );
}

/**
 * Check if expression evaluates to a non-zero value.
 *
 * \param expr The expression to be evaluated.
 */
static bool nonZeroExpr( PTREE expr )
{
    PTREE orig;

    switch( expr->op ) {
    case PT_PTR_CONSTANT:
    case PT_INT_CONSTANT:
    case PT_FLOATING_CONSTANT:
        return( ! zeroConstant( expr ) );
    case PT_SYMBOL:
        /* a symbol r-value has a fetch by now so this PTREE means &name */
        return( true );
    case PT_BINARY:
        orig = expr;
        expr = NodeRemoveCasts( expr );
        if( expr == orig )
            break;
        return( nonZeroExpr( expr ) );
    }
    if( expr->flags & PTF_PTR_NONZERO ) {
        return( true );
    }
    return( false );
}


/**
 * Check that the expression has side effects.
 *
 * \param expr The expression to be tested.
 */
static bool hasSideEffects( PTREE expr )
{
    return( ( expr->flags & PTF_SIDE_EFF ) != 0  );
}

/**
 * Check if the expression is foldable (has a constant value).
 *
 * \param expr The expression to be tested.
 */
static bool foldable( PTREE expr )
{
    // If the expression has side effects, it cannot be folded.
    if( hasSideEffects( expr ) ) {
        return( false );
    }

    // We can fold some of the non-zero pointer occurrences.
    if( expr->flags & PTF_PTR_NONZERO ) {
        return( true );
    }

    switch( expr->op ) {
    case PT_PTR_CONSTANT:
    case PT_INT_CONSTANT:
    case PT_FLOATING_CONSTANT:
        return( true );
    default :
        if( zeroConstant( expr ) ) {
            return( true );
        }
        break;
    }
    return( false );
}

PTREE CastIntConstant( PTREE expr, TYPE type, bool *happened )
{
    PTREE new_expr;
    target_ulong ul_val;
    float_handle dbl_val;
    type_id id;
    bool signed_type;


    signed_type = SignedIntType( expr->type );
    id = TypedefModifierRemove( type )->id;
    ul_val = expr->u.uint_constant;
    if( NULL == Integral64Type( expr->type ) ) {
        switch( id ) {
        case TYP_SCHAR:
            ul_val = (target_schar) ul_val;
            /* fall through */
        case TYP_SSHORT:
            ul_val = (target_short) ul_val;
            /* fall through */
        case TYP_SINT:
            ul_val = (target_int) ul_val;
            /* fall through */
        case TYP_SLONG:
            ul_val = (target_long) ul_val;
            new_expr = PTreeIntConstant( ul_val, id );
            break;
        case TYP_UCHAR:
            ul_val = (target_uchar) ul_val;
            /* fall through */
        case TYP_USHORT:
            ul_val = (target_ushort) ul_val;
            /* fall through */
        case TYP_UINT:
            ul_val = (target_uint) ul_val;
            /* fall through */
        case TYP_ULONG:
            ul_val = (target_ulong) ul_val;
            new_expr = PTreeIntConstant( ul_val, id );
            break;
        case TYP_ULONG64:
        case TYP_SLONG64:
            if( PT_FLOATING_CONSTANT == expr->op ) {
                new_expr = PTreeInt64Constant
                                ( CFCnvF64( expr->u.floating_constant )
                                , id );
            } else {
                new_expr = PTreeInt64Constant( expr->u.int64_constant, id );
            }
            break;
        case TYP_POINTER:
        case TYP_MEMBER_POINTER:
            ul_val = (target_ulong) ul_val;
            new_expr = PTreeIntConstant( ul_val, TYP_ULONG );
            new_expr->type = type;
            break;
        case TYP_FLOAT:
#if 0
// these are now identical, with canonical floating point
            if( signed_type ) {
                flt_val = CFCnvIF( &cxxh, expr->u.int_constant );
            } else {
                flt_val = CFCnvUF( &cxxh, expr->u.uint_constant );
            }
            new_expr = PTreeFloatingConstant( flt_val, id );
            break;
#endif
        case TYP_LONG_DOUBLE:
        case TYP_DOUBLE:
            if( signed_type ) {
                dbl_val = CFCnvIF( &cxxh, expr->u.int_constant );
            } else {
                dbl_val = CFCnvUF( &cxxh, expr->u.uint_constant );
            }
            new_expr = PTreeFloatingConstant( dbl_val, id );
            break;
        case TYP_WCHAR:
            ul_val = (target_wchar) ul_val;
            new_expr = PTreeIntConstant( ul_val, id );
            break;
        default:
            return( expr );
        }
    } else {
        ul_val = expr->u.int_constant;
        switch( id ) {
        case TYP_SCHAR:
            ul_val = (target_schar) ul_val;
            /* fall through */
        case TYP_SSHORT:
            ul_val = (target_short) ul_val;
            /* fall through */
        case TYP_SINT:
            ul_val = (target_int) ul_val;
            /* fall through */
        case TYP_SLONG:
            ul_val = (target_long) ul_val;
            new_expr = PTreeIntConstant( ul_val, id );
            break;
        case TYP_SLONG64:
        case TYP_ULONG64:
            new_expr = PTreeInt64Constant( expr->u.int64_constant, id );
            break;
        case TYP_UCHAR:
            ul_val = (target_uchar) ul_val;
            /* fall through */
        case TYP_USHORT:
            ul_val = (target_ushort) ul_val;
            /* fall through */
        case TYP_UINT:
            ul_val = (target_uint) ul_val;
            /* fall through */
        case TYP_ULONG:
            ul_val = (target_ulong) ul_val;
            new_expr = PTreeIntConstant( ul_val, id );
            break;
        case TYP_POINTER:
        case TYP_MEMBER_POINTER:
            ul_val = (target_ulong) ul_val;
            new_expr = PTreeIntConstant( ul_val, TYP_ULONG );
            new_expr->type = type;
            break;
        case TYP_FLOAT:
#if 0
// these are now identical, with canonical floating point
            if( signed_type ) {
                flt_val = CFCnvI64F( &cxxh, expr->u.int64_constant.u._32[I64LO32], expr->u.int64_constant.u._32[I64HI32] );
            } else {
                flt_val = CFCnvU64F( &cxxh, expr->u.int64_constant.u._32[I64LO32], expr->u.int64_constant.u._32[I64HI32] );
            }
            new_expr = PTreeFloatingConstant( flt_val, id );
            break;
#endif
        case TYP_LONG_DOUBLE:
        case TYP_DOUBLE:
            if( signed_type ) {
                dbl_val = CFCnvI64F( &cxxh, expr->u.int64_constant.u._32[I64LO32], expr->u.int64_constant.u._32[I64HI32] );
            } else {
                dbl_val = CFCnvU64F( &cxxh, expr->u.int64_constant.u._32[I64LO32], expr->u.int64_constant.u._32[I64HI32] );
            }
            new_expr = PTreeFloatingConstant( dbl_val, id );
            break;
        case TYP_WCHAR:
            ul_val = (target_wchar) ul_val;
            new_expr = PTreeIntConstant( ul_val, id );
            break;
        default:
            return( expr );
        }
    }
    *happened = true;
    new_expr->flags = expr->flags;
    new_expr = PTreeCopySrcLocation( new_expr, expr );
    PTreeFree( expr );
    return( new_expr );
}

static PTREE castFloatingConstant( PTREE expr, TYPE type, bool *happened )
{
    target_long value;
    PTREE new_expr;
    type_id id;

    id = TypedefModifierRemove( type )->id;
    switch( id ) {
    case TYP_POINTER:
    case TYP_MEMBER_POINTER:
        id = TYP_ULONG;
        /* fall through */
    case TYP_SCHAR:
    case TYP_SSHORT:
    case TYP_SINT:
    case TYP_SLONG:
    case TYP_UCHAR:
    case TYP_USHORT:
    case TYP_UINT:
    case TYP_ULONG:
    case TYP_WCHAR:
        value = CFFloat2Long( &(expr->u.floating_constant) );
        new_expr = PTreeIntConstant( (target_ulong) value, id );
        new_expr = CastIntConstant( new_expr, type, happened );
        break;
    case TYP_FLOAT:
      { float_handle flt_val;

        flt_val = CFCopy( &cxxh, expr->u.floating_constant );
        new_expr = PTreeFloatingConstant( flt_val, id );
      } break;
    case TYP_LONG_DOUBLE:
    case TYP_DOUBLE:
      { float_handle flt_val;
        flt_val = CFCopy( &cxxh, expr->u.floating_constant );
        new_expr = PTreeFloatingConstant( flt_val, id );
      } break;
    case TYP_SLONG64:
    case TYP_ULONG64:
      { signed_64 val = CFCnvF64( expr->u.floating_constant );
        new_expr = PTreeInt64Constant( val, id );
      } break;
    default:
        return( expr );
    }
    *happened = true;
    new_expr = PTreeCopySrcLocation( new_expr, expr );
    PTreeFree( expr );
    return( new_expr );
}

static PTREE castConstant( PTREE expr, TYPE type, bool *happened )
{
    TYPE type_final;

    *happened = false;
    if( !foldable( expr ) ) {
        return( expr );
    }
    type_final = TypedefModifierRemoveOnly( type );
    type = TypedefModifierRemove( type_final );
    switch( expr->op ) {
    case PT_INT_CONSTANT:
        expr = CastIntConstant( expr, type, happened );
        expr->type = type_final;
        break;
    case PT_FLOATING_CONSTANT:
        expr = castFloatingConstant( expr, type, happened );
        expr->type = type_final;
        break;
    }
    return( expr );
}

static bool soFarSoGood( PTREE expr, ptree_op_t op, CGOP cgop )
{
    if( expr != NULL && expr->op == op && expr->cgop == cgop ) {
        return( true );
    }
    return( false );
}

static bool referenceSymbol( PTREE expr )
{
    SYMBOL ref_sym;

    ref_sym = expr->u.symcg.symbol;
    if( ref_sym != NULL && TypeReference( ref_sym->sym_type ) != NULL ) {
        return( true );
    }
    return( false );
}

static bool thisSymbol( PTREE expr )
{
    SYMBOL this_sym;

    this_sym = expr->u.symcg.symbol;
    if( this_sym == NULL ) {
        return( true );
    }
    return( false );
}


static bool anachronismFound( PTREE expr )
{
    /* two anachronisms supported:

        (1) this != 0, this == 0
        (2) &r != 0, &r == 0    -- r is 'T & r;' (parm or auto)
    */
    if( !CompFlags.extensions_enabled ) {
        /* ANSI C++ code cannot require these constructs */
        return( false );
    }
    if( soFarSoGood( expr, PT_UNARY, CO_ADDR_OF ) ) {
        expr = expr->u.subtree[0];
        if( soFarSoGood( expr, PT_UNARY, CO_RARG_FETCH ) ) {
            /*
                int foo( X &r )
                {
                    return( &r != NULL );
                }
            */
            expr = expr->u.subtree[0];
            if( soFarSoGood( expr, PT_SYMBOL, CO_NAME_PARM_REF ) ) {
                return( referenceSymbol( expr ) );
            }
        } else if( soFarSoGood( expr, PT_UNARY, CO_FETCH ) ) {
            /*
                int foo( X *p )
                {
                    X &r = *p;
                    return( &r != NULL );
                }
            */
            expr = expr->u.subtree[0];
            if( soFarSoGood( expr, PT_SYMBOL, CO_NAME_NORMAL ) ) {
                return( referenceSymbol( expr ) );
            }
        }
    } else if( soFarSoGood( expr, PT_UNARY, CO_RARG_FETCH ) ) {
        /*
            int S::foo()
            {
                return( this ? 0 : field );
            }
        */
        expr = expr->u.subtree[0];
        if( soFarSoGood( expr, PT_SYMBOL, CO_NAME_PARM_REF ) ) {
            return( thisSymbol( expr ) );
        }
    }
    return( false );
}

/**
 * Transform a node into a true/false boolean constant.
 * Unlike makeBooleanConst, this function deletes the current node.
 *
 * \param expr The node to be transformed.
 * \param op The undecorated expr.
 * \param value Either 0 or 1 (false or true).
 */
static PTREE makeTrueFalse( PTREE expr, PTREE op, int value )
{
    PTREE node;
    DbgVerify( ( value & 1 ) == value, "invalid boolean constant fold" );

    if( anachronismFound( op ) ) {
        return( expr );
    }
    node = NodeOffset( value );
    node = NodeSetBooleanType( node );
    node = PTreeCopySrcLocation( node, expr );
    NodeFreeDupedExpr( expr );
    return( node );
}

/**
 * Transform a node into a true/false boolean constant.
 * Unlike makeTrueFalse, this fucntion does not delete the current
 * node, but simply replaces its type. (Its subtrees may never be freed)
 *
 * \param expr The node to be transformed.
 * \param value Either 0 or 1 (false or true).
 */
static PTREE makeBooleanConst( PTREE expr, int value )
{
    DbgVerify( ( value & 1 ) == value, "invalid boolean constant fold" );
    expr->u.int_constant = value;
    expr->op = PT_INT_CONSTANT;
    expr = NodeSetBooleanType( expr );
    return( expr );
}

PTREE FoldUnary( PTREE expr )
/***************************/
{
    PTREE op1;
    TYPE result_type = expr->type;;
    bool dont_care;

    op1 = expr->u.subtree[0];

    if( !foldable( op1 ) ) {
        return( expr );
    }

    if( expr->cgop == CO_EXCLAMATION ) {
        // This is the simplest operation.
        return( makeTrueFalse( expr, op1, !nonZeroExpr( op1 ) ) );
    }

    if( op1->op == PT_FLOATING_CONSTANT ) {
        switch( expr->cgop ) {
        case CO_UMINUS:
            CFNegate( op1->u.floating_constant );
            break;
        case CO_UPLUS:
            break;
        default:
            return( expr );
        }
        op1 = PTreeCopySrcLocation( op1, expr );
        PTreeFree( expr );
        return( castConstant( op1, result_type, &dont_care ) );
    } else if( op1->op == PT_INT_CONSTANT ) {
        switch( expr->cgop ) {
        case CO_TILDE:
            op1->u.int64_constant.u._32[0] = ~ op1->u.int64_constant.u._32[0];
            op1->u.int64_constant.u._32[1] = ~ op1->u.int64_constant.u._32[1];
            break;
        case CO_UMINUS:
            U64Neg( &op1->u.int64_constant, &op1->u.int64_constant );
            break;
        case CO_UPLUS:
            break;
        default:
            return( expr );
        }
    }
    // If we have a non-zero pointer expression, don't try to cast it
    // or alter the unary expression.
    if( expr->flags & PTF_PTR_NONZERO ) {
        return( expr );
    }
    op1 = PTreeCopySrcLocation( op1, expr );
    PTreeFree( expr );
    return( castConstant( op1, result_type, &dont_care ) );
}

static PTREE foldFloating( CGOP op, PTREE left, float_handle v2 )
{
    float_handle v1;
    float_handle tmp;

    v1 = left->u.floating_constant;
    switch( op ) {
    case CO_PLUS:
        tmp = CFAdd( &cxxh, v1, v2 );
        CFFree( &cxxh, v1 );
        v1 = tmp;
        break;
    case CO_MINUS:
        tmp = CFSub( &cxxh, v1, v2 );
        CFFree( &cxxh, v1 );
        v1 = tmp;
        break;
    case CO_TIMES:
        tmp = CFMul( &cxxh, v1, v2 );
        CFFree( &cxxh, v1 );
        v1 = tmp;
        break;
    case CO_DIVIDE:
        if( CFTest( v2 ) == 0 ) {
            CErr1( ERR_DIVISION_BY_ZERO );
        }
        tmp = CFDiv( &cxxh, v1, v2 );
        CFFree( &cxxh, v1 );
        v1 = tmp;
        break;
    case CO_EQ:
        left = makeBooleanConst( left, CFCompare( v1, v2 ) == 0 );
        CFFree( &cxxh, v1 );
        return( left );
    case CO_NE:
        left = makeBooleanConst( left, CFCompare( v1, v2 ) != 0 );
        CFFree( &cxxh, v1 );
        return( left );
    case CO_GT:
        left = makeBooleanConst( left, CFCompare( v1, v2 ) > 0 );
        CFFree( &cxxh, v1 );
        return( left );
    case CO_LE:
        left = makeBooleanConst( left, CFCompare( v1, v2 ) <= 0 );
        CFFree( &cxxh, v1 );
        return( left );
    case CO_LT:
        left = makeBooleanConst( left, CFCompare( v1, v2 ) < 0 );
        CFFree( &cxxh, v1 );
        return( left );
    case CO_GE:
        left = makeBooleanConst( left, CFCompare( v1, v2 ) >= 0 );
        CFFree( &cxxh, v1 );
        return( left );
    case CO_COMMA:
        CFFree( &cxxh, v1 );
        v1 = CFCopy( &cxxh, v2 );
        break;
    default:
        return( NULL );
    }

    left->u.floating_constant = v1;
    left->op = PT_FLOATING_CONSTANT;
    return( left );
}

static PTREE foldUInt( CGOP op, PTREE left, target_ulong v2 )
{
    double test;
    target_ulong v1;

    v1 = left->u.uint_constant;
    switch( op ) {
    case CO_PLUS:
        v1 += v2;
        break;
    case CO_MINUS:
        v1 -= v2;
        break;
    case CO_TIMES:
        test = (double) v1 * (double) v2;
        v1 *= v2;
        if( test != v1 ) {
            CErr1( ANSI_ARITHMETIC_OVERFLOW );
        }
        break;
    case CO_DIVIDE:
        if( v2 == 0 ) {
            CErr1( ERR_DIVISION_BY_ZERO );
            v1 = 1;
        } else {
            v1 /= v2;
        }
        break;
    case CO_PERCENT:
        if( v2 == 0 ) {
            CErr1( ERR_DIVISION_BY_ZERO );
            v1 = 1;
        } else {
            v1 %= v2;
        }
        break;
    case CO_AND:
        v1 &= v2;
        break;
    case CO_OR:
        v1 |= v2;
        break;
    case CO_XOR:
        v1 ^= v2;
        break;
    case CO_RSHIFT:
        if( ((target_ulong) v2) >= TARGET_LONG * CHAR_BIT ) {
            v1 = 0;
        } else {
            v1 >>= v2;
        }
        break;
    case CO_LSHIFT:
        if( ((target_ulong) v2) >= TARGET_LONG * CHAR_BIT ) {
            v1 = 0;
        } else {
            v1 <<= v2;
        }
        break;
    case CO_EQ:
        left = makeBooleanConst( left, v1 == v2 );
        return( left );
    case CO_NE:
        left = makeBooleanConst( left, v1 != v2 );
        return( left );
    case CO_GT:
        left = makeBooleanConst( left, v1 > v2 );
        return( left );
    case CO_LE:
        left = makeBooleanConst( left, v1 <= v2 );
        return( left );
    case CO_LT:
        left = makeBooleanConst( left, v1 < v2 );
        return( left );
    case CO_GE:
        left = makeBooleanConst( left, v1 >= v2 );
        return( left );
    case CO_AND_AND:
        left = makeBooleanConst( left, v1 && v2 );
        return( left );
    case CO_OR_OR:
        left = makeBooleanConst( left, v1 || v2 );
        return( left );
    case CO_COMMA:
        v1 = v2;
        break;
#if _CPU == 8086
    case CO_SEG_OP:
        v1 = (v2 << (TARGET_NEAR_POINTER * 8)) | v1;
        break;
#endif
    default:
        return( NULL );
    }
    left->u.uint_constant = v1;
    left->op = PT_INT_CONSTANT;
    return( left );
}

static PTREE foldInt( CGOP op, PTREE left, target_long v2 )
{
    double test;
    target_long v1;

    v1 = left->u.int_constant;
    switch( op ) {
    case CO_PLUS:
        v1 += v2;
        break;
    case CO_MINUS:
        v1 -= v2;
        break;
    case CO_TIMES:
        test = (double) v1 * (double) v2;
        v1 *= v2;
        if( test != v1 ) {
            CErr1( ANSI_ARITHMETIC_OVERFLOW );
        }
        break;
    case CO_DIVIDE:
        if( v2 == 0 ) {
            CErr1( ERR_DIVISION_BY_ZERO );
            v1 = 1;
        } else {
            v1 /= v2;
        }
        break;
    case CO_PERCENT:
        if( v2 == 0 ) {
            CErr1( ERR_DIVISION_BY_ZERO );
            v1 = 1;
        } else {
            v1 %= v2;
        }
        break;
    case CO_AND:
        v1 &= v2;
        break;
    case CO_OR:
        v1 |= v2;
        break;
    case CO_XOR:
        v1 ^= v2;
        break;
    case CO_RSHIFT:
        if( ((target_ulong) v2) >= TARGET_LONG * CHAR_BIT ) {
            v1 = FoldSignedRShiftMax( v1 );
        } else {
            v1 >>= v2;
        }
        break;
    case CO_LSHIFT:
        if( ((target_ulong) v2) >= TARGET_LONG * CHAR_BIT ) {
            v1 = 0;
        } else {
            v1 <<= v2;
        }
        break;
    case CO_EQ:
        left = makeBooleanConst( left, v1 == v2 );
        return( left );
    case CO_NE:
        left = makeBooleanConst( left, v1 != v2 );
        return( left );
    case CO_GT:
        left = makeBooleanConst( left, v1 > v2 );
        return( left );
    case CO_LE:
        left = makeBooleanConst( left, v1 <= v2 );
        return( left );
    case CO_LT:
        left = makeBooleanConst( left, v1 < v2 );
        return( left );
    case CO_GE:
        left = makeBooleanConst( left, v1 >= v2 );
        return( left );
    case CO_AND_AND:
        left = makeBooleanConst( left, v1 && v2 );
        return( left );
    case CO_OR_OR:
        left = makeBooleanConst( left, v1 || v2 );
        return( left );
    case CO_COMMA:
        v1 = v2;
        break;
    default:
        return( NULL );
    }
    left->u.int_constant = v1;
    left->op = PT_INT_CONSTANT;
    return( left );
}

static void idiv64              // DO 64-BIT SIGNED DIVISION
    ( signed_64 const * v1      // - top
    , signed_64 const * v2      // - divisor
    , signed_64* result         // - result
    , signed_64* rem )          // - remainder
{
    if( v2->u._32[0] == 0
     && v2->u._32[1] == 0 ) {
        CErr1( ERR_DIVISION_BY_ZERO );
        result->u._32[I64HI32] = 0;
        result->u._32[I64LO32] = 1;
        rem->u._32[I64HI32] = 0;
        rem->u._32[I64LO32] = 0;
    } else {
        I64Div( v1, v2, result, rem );
    }
}

static PTREE foldInt64( CGOP op, PTREE left, signed_64 v2 )
{
    signed_64 test;
    signed_64 v1;
    float_handle t0, t1, t2;

    v1 = left->u.int64_constant;
    switch( op ) {
    case CO_PLUS:
        U64Add( &v1, &v2, &left->u.int64_constant );
        break;
    case CO_MINUS:
        U64Sub( &v1, &v2, &left->u.int64_constant );
        break;
    case CO_TIMES:
        t0 = CFCnvI64F( &cxxh, v1.u._32[I64LO32], v1.u._32[I64HI32] );
        t1 = CFCnvI64F( &cxxh, v2.u._32[I64LO32], v2.u._32[I64HI32] );
        t2 = CFMul( &cxxh, t0, t1 );
        test = CFCnvF64( t2 );
        CFFree( &cxxh, t0 );
        CFFree( &cxxh, t1 );
        CFFree( &cxxh, t2 );
        U64Mul( &v1, &v2, &left->u.int64_constant );
        if( 0 != I64Cmp( &test, &left->u.int64_constant ) ) {
            CErr1( ANSI_ARITHMETIC_OVERFLOW );
        }
        break;
    case CO_DIVIDE:
      { signed_64 rem;
        idiv64( &v1, &v2, &left->u.int64_constant, &rem );
      } break;
    case CO_PERCENT:
      { signed_64 div;
        idiv64( &v1, &v2, &div, &left->u.int64_constant );
      } break;
    case CO_AND:
        left->u.int64_constant.u._32[0] = v1.u._32[0] & v2.u._32[0];
        left->u.int64_constant.u._32[1] = v1.u._32[1] & v2.u._32[1];
        break;
    case CO_OR:
        left->u.int64_constant.u._32[0] = v1.u._32[0] | v2.u._32[0];
        left->u.int64_constant.u._32[1] = v1.u._32[1] | v2.u._32[1];
        break;
    case CO_XOR:
        left->u.int64_constant.u._32[0] = v1.u._32[0] ^ v2.u._32[0];
        left->u.int64_constant.u._32[1] = v1.u._32[1] ^ v2.u._32[1];
        break;
    case CO_RSHIFT:
        I64ShiftR( &v1, v2.u._32[I64LO32], &left->u.int64_constant );
        break;
    case CO_LSHIFT:
        U64ShiftL( &v1, v2.u._32[I64LO32], &left->u.int64_constant );
        break;
    case CO_EQ:
        left = makeBooleanConst( left, 0 == I64Cmp( &v1, &v2 ) );
        return( left );
    case CO_NE:
        left = makeBooleanConst( left, 0 != I64Cmp( &v1, &v2 ) );
        return( left );
    case CO_GT:
        left = makeBooleanConst( left, 0 < I64Cmp( &v1, &v2 )) ;
        return( left );
    case CO_LE:
        left = makeBooleanConst( left, 0 >= I64Cmp( &v1, &v2 ) );
        return( left );
    case CO_LT:
        left = makeBooleanConst( left, 0 > I64Cmp( &v1, &v2 )) ;
        return( left );
    case CO_GE:
        left = makeBooleanConst( left, 0 <= I64Cmp( &v1, &v2 ) );
        return( left );
    case CO_AND_AND:
        left = makeBooleanConst( left, !Zero64( &v1 ) && !Zero64( &v2 ) );
        return( left );
    case CO_OR_OR:
        left = makeBooleanConst( left, !Zero64( &v1) || !Zero64( &v2 ) );
        return( left );
    case CO_COMMA:
        left->u.int64_constant = v2;
        break;
    default:
        return( NULL );
    }
    left->op = PT_INT_CONSTANT;
    return( left );
}

static void udiv64              // DO 64-BIT UNSIGNED DIVISION
    ( unsigned_64 const * v1    // - top
    , unsigned_64 const * v2    // - divisor
    , unsigned_64* result       // - result
    , unsigned_64* rem )        // - remainder
{
    if( v2->u._32[0] == 0
     && v2->u._32[1] == 0 ) {
        CErr1( ERR_DIVISION_BY_ZERO );
        result->u._32[I64HI32] = 0;
        result->u._32[I64LO32] = 1;
        rem->u._32[I64HI32] = 0;
        rem->u._32[I64LO32] = 0;
    } else {
        U64Div( v1, v2, result, rem );
    }
}

static PTREE foldUInt64( CGOP op, PTREE left, signed_64 v2 )
{
    signed_64 test;
    signed_64 v1;
    float_handle t0, t1, t2;

    v1 = left->u.int64_constant;
    switch( op ) {
    case CO_PLUS:
        U64Add( &v1, &v2, &left->u.int64_constant );
        break;
    case CO_MINUS:
        U64Sub( &v1, &v2, &left->u.int64_constant );
        break;
    case CO_TIMES:
        t0 = CFCnvU64F( &cxxh, v1.u._32[I64LO32], v1.u._32[I64HI32] );
        t1 = CFCnvU64F( &cxxh, v2.u._32[I64LO32], v2.u._32[I64HI32] );
        t2 = CFMul( &cxxh, t0, t1 );
        test = CFCnvF64( t2 );
        CFFree( &cxxh, t0 );
        CFFree( &cxxh, t1 );
        CFFree( &cxxh, t2 );
        U64Mul( &v1, &v2, &left->u.int64_constant );
        if( 0 != U64Cmp( &test, &left->u.int64_constant ) ) {
            CErr1( ANSI_ARITHMETIC_OVERFLOW );
        }
        break;
    case CO_DIVIDE:
      { signed_64 rem;
        udiv64( &v1, &v2, &left->u.int64_constant, &rem );
      } break;
    case CO_PERCENT:
      { signed_64 div;
        udiv64( &v1, &v2, &div, &left->u.int64_constant );
      } break;
    case CO_AND:
        left->u.int64_constant.u._32[0] = v1.u._32[0] & v2.u._32[0];
        left->u.int64_constant.u._32[1] = v1.u._32[1] & v2.u._32[1];
        break;
    case CO_OR:
        left->u.int64_constant.u._32[0] = v1.u._32[0] | v2.u._32[0];
        left->u.int64_constant.u._32[1] = v1.u._32[1] | v2.u._32[1];
        break;
    case CO_XOR:
        left->u.int64_constant.u._32[0] = v1.u._32[0] ^ v2.u._32[0];
        left->u.int64_constant.u._32[1] = v1.u._32[1] ^ v2.u._32[1];
        break;
    case CO_RSHIFT:
        U64ShiftR( &v1, v2.u._32[I64LO32], &left->u.int64_constant );
        break;
    case CO_LSHIFT:
        U64ShiftL( &v1, v2.u._32[I64LO32], &left->u.int64_constant );
        break;
    case CO_EQ:
        left = makeBooleanConst( left, 0 == U64Cmp( &v1, &v2 ) );
        return( left );
    case CO_NE:
        left = makeBooleanConst( left, 0 != U64Cmp( &v1, &v2 ) );
        return( left );
    case CO_GT:
        left = makeBooleanConst( left, 0 < U64Cmp( &v1, &v2 )) ;
        return( left );
    case CO_LE:
        left = makeBooleanConst( left, 0 >= U64Cmp( &v1, &v2 ) );
        return( left );
    case CO_LT:
        left = makeBooleanConst( left, 0 > U64Cmp( &v1, &v2 )) ;
        return( left );
    case CO_GE:
        left = makeBooleanConst( left, 0 <= U64Cmp( &v1, &v2 ) );
        return( left );
    case CO_AND_AND:
        left = makeBooleanConst( left, !Zero64( &v1 ) && !Zero64( &v2 ) );
        return( left );
    case CO_OR_OR:
        left = makeBooleanConst( left, !Zero64( &v1) || !Zero64( &v2 ) );
        return( left );
    case CO_COMMA:
        left->u.int64_constant = v2;
        break;
    default:
        return( NULL );
    }
    left->op = PT_INT_CONSTANT;
    return( left );
}


static PTREE pruneExpr(         // PRUNE ONE SIDE FROM EXPRESSION
    PTREE orig,                 // - original expression
    PTREE* ref,                 // - subtree[0] or [1] in orig
    PTREE undec )               // - undecorated result
{
    if( *ref == undec ) {
        *ref = NULL;
    } else {
        (*ref)->u.subtree[1] = NULL;
    }
    undec = PTreeCopySrcLocation( undec, orig );
    NodeFreeDupedExpr( orig );
    return( undec );
}

#define isIntFloatOp( op ) \
    (((1L << (op)) & ((1L << PT_INT_CONSTANT) | (1L << PT_FLOATING_CONSTANT))) != 0 )

/**
 * Perform some folding optimizations that can be safely applied
 * towards the left of the expression (the lhs is constant).
 *
 * \param expr The binary expression to be folded.
 * \param op1 The lhs of the expression.
 * \param op2 The rhs of the expression.
 * \param has_decoration Whether either side of the expression
 *                       was conditionally-decorated.
 */
static PTREE FoldBinaryLeft( bool *has_folded,
    PTREE expr, PTREE op1, PTREE op2,
    bool has_decoration )
{
    PTREE op_t;
    PTREE op_f;
    PTREE *pchild1 = &expr->u.subtree[0];
    PTREE *pchild2 = &expr->u.subtree[1];

    DbgVerify( foldable( op1 ), "op1 must be foldable" );

    *has_folded = false;

    switch( expr->cgop ) {
    case CO_EQ:
        DbgVerify( !has_decoration, "FoldBinaryLeft -- bad ==" );
        // NOTE: We must use zeroConstant on op2, as we don't know
        // whether it is a constant expression or not (!nonZeroExpr may not be)
        if( zeroConstant( op2 ) && !hasSideEffects( op2 ) ) {
            *has_folded = true;
            // expr is true only if op1 is also zero.
            expr = makeTrueFalse( expr, op2, !nonZeroExpr( op1 ));
        }
        break;
    case CO_NE:
        DbgVerify( !has_decoration, "FoldBinaryLeft -- bad !=" );

        if( zeroConstant( op2 ) && !hasSideEffects( op2 ) ) {
            *has_folded = true;
            // expr is true only if op1 is not zero.
            expr = makeTrueFalse( expr, op2, nonZeroExpr( op1 ));
        }
        break;
    case CO_AND_AND:
        *has_folded = true;
        // Appy short-circuiting logic for &&.

//          DbgVerify( has_decoration, "FoldBinary -- bad &&" );
        if( nonZeroExpr( op1 ) ) {
            /* 1 && X => X (X is already boolean) */
            expr = pruneExpr( expr, pchild2, op2 );
        } else {
            /* 0 && X => 0 */
            expr = pruneExpr( expr, pchild1, op1 );
        }
        break;
    case CO_OR_OR:
        *has_folded = true;
        //Apply short-circuiting logic for ||.
//          DbgVerify( has_decoration, "FoldBinary -- bad ||" );
        if( !nonZeroExpr( op1 ) ) {
            /* 0 || X => X (X is already boolean) */
            expr = pruneExpr( expr, pchild2, op2 );
        } else {
            /* 1 || X => 1 */
            expr = pruneExpr( expr, pchild1, op1 );
        }
        break;
    case CO_COMMA:
        /* c , X => X */
//              DbgVerify( ! has_decoration, "FoldBinary -- bad comma" );
        expr->u.subtree[1] = NULL;
        op2 = PTreeCopySrcLocation( op2, expr );
        NodeFreeDupedExpr( expr );
        *has_folded = true;
        return( op2 );
    case CO_QUESTION:
        DbgVerify( ! has_decoration, "FoldBinary -- bad ?" );
        op_t = op2->u.subtree[0];
        op_f = op2->u.subtree[1];
        has_decoration = isCondDecor( op_t );
        DbgVerify( has_decoration == isCondDecor( op_f )
                 , "FoldBinary -- bad ?:" );
        if( has_decoration ) {
            op_t = op_t->u.subtree[1];
            op_f = op_f->u.subtree[1];
        }
        if( nonZeroExpr( op1 ) ) {
            /* 1 ? T : F => T */
            if( has_decoration ) {
                op2->u.subtree[0]->u.subtree[1] = NULL;
            } else {
                op2->u.subtree[0] = NULL;
            }
            op2 = op_t;
        } else {
            /* 0 ? T : F => F */
            if( has_decoration ) {
                op2->u.subtree[1]->u.subtree[1] = NULL;
            } else {
                op2->u.subtree[1] = NULL;
            }
            op2 = op_f;
        }
        op2 = PTreeCopySrcLocation( op2, expr );
        NodeFreeDupedExpr( expr );
        *has_folded = true;
        return( op2 );
    }

    return( expr );
}

/**
 * Perform some simple (single pass) folding transformations
 * on the AST (binary expression).
 * Note that other (multipass) transformations are applied
 * by the backend.
 *
 * \param expr a binary expression to be folded.
 */
PTREE FoldBinary( PTREE expr )
{
    PTREE orig1;
    PTREE orig2;
    PTREE op1;
    PTREE op2;
    PTREE op_test;
    TYPE type;
    ptree_op_t typ1;
    ptree_op_t typ2;
    bool cast_happened;
    bool has_decoration_left;
    bool has_decoration_right;
    bool has_decoration;

    type = expr->type;
    orig1 = expr->u.subtree[0];
    orig2 = expr->u.subtree[1];
    op1 = overCondDecor( orig1 );
    has_decoration_left = op1 != orig1;
    op2 = overCondDecor( orig2 );
    has_decoration_right = op2 != orig2;
    has_decoration = has_decoration_left | has_decoration_right;

    if( !foldable( op1 ) && !foldable( op2 ) )
        return( expr );

    // Try performing left-folding on the expression.
    /* Note that right-folding is only safe in certain situations. For example

    ( fn() && 0 )

    cannot be directly folded into ( false ) by eliding the function call.
    */
    if( foldable( op1 ) ) {
        bool has_lfolded = false;
        expr = FoldBinaryLeft( &has_lfolded, expr, op1, op2, has_decoration );
        if( has_lfolded ) {
            return( expr );
        }
    }

    if( !foldable( op2 ) )
        return( expr );

    switch( expr->cgop ) {
    case CO_COMMA:
        // X, c -- can be optimized when X is PT_IC( IC_COND_TRUE )
        // and comma node has PTF_COND_END set
        //
        if( (expr->flags & PTF_COND_END)
         && op1->op == PT_IC
         && op1->u.ic.opcode == IC_COND_TRUE ) {
            expr = pruneExpr( expr, &expr->u.subtree[1], op2 );
        }
        return( expr );
    case CO_PLUS_EQUAL :
    case CO_MINUS_EQUAL :
    case CO_AND_EQUAL :
    case CO_OR_EQUAL :
    case CO_XOR_EQUAL :
    case CO_EQUAL:
        /* have to be careful with pointer scaling of numbers */
        DbgVerify( ! has_decoration, "FoldBinary -- bad equals" );
        if( ArithType( type ) != NULL ) {
            expr->u.subtree[1] = castConstant( op2, type, &cast_happened );
        }
        return( expr );
    case CO_CONVERT:
        DbgVerify( ! has_decoration, "FoldBinary -- bad convert" );
        op_test = castConstant( op2, type, &cast_happened );
        if( cast_happened ) {
            /* op2 was freed */
            op_test = PTreeCopySrcLocation( op_test, expr );
            NodeFreeDupedExpr( op1 );
            PTreeFree( expr );
            return( op_test );
        }
        return( expr );

    }

    typ1 = op1->op;
    typ2 = op2->op;
    if( ! isIntFloatOp( typ1 ) || ! isIntFloatOp( typ2 ) ) {
        // (void)0 can make it here
        return( expr );
    }
    if( typ1 != typ2 ) {
        if( PT_FLOATING_CONSTANT == typ1 ) {
            if( NULL == Integral64Type( op2->type ) ) {
                if( SignedIntType( op2->type ) ) {
                    op2->u.floating_constant
                        = CFCnvIF( &cxxh, op2->u.int_constant );
                } else {
                    op2->u.floating_constant
                        = CFCnvUF( &cxxh, op2->u.uint_constant );
                }
            } else {
                if( SignedIntType( op2->type ) ) {
                    op2->u.floating_constant
                        = CFCnvI64F( &cxxh, op2->u.int64_constant.u._32[I64LO32], op2->u.int64_constant.u._32[I64HI32] );
                } else {
                    op2->u.floating_constant
                        = CFCnvU64F( &cxxh, op2->u.int64_constant.u._32[I64LO32], op2->u.int64_constant.u._32[I64HI32] );
                }
            }
            typ2 = PT_FLOATING_CONSTANT;
            op2->op = typ2;
        } else {
            if( NULL == Integral64Type( op1->type ) ) {
                if( SignedIntType( op1->type ) ) {
                    op1->u.floating_constant
                        = CFCnvIF( &cxxh, op1->u.int_constant );
                } else {
                    op1->u.floating_constant
                        = CFCnvUF( &cxxh, op1->u.uint_constant );
                }
            } else {
                if( SignedIntType( op1->type ) ) {
                    op1->u.floating_constant
                        = CFCnvI64F( &cxxh, op1->u.int64_constant.u._32[I64LO32], op1->u.int64_constant.u._32[I64HI32] );
                } else {
                    op1->u.floating_constant
                        = CFCnvU64F( &cxxh, op1->u.int64_constant.u._32[I64LO32], op1->u.int64_constant.u._32[I64HI32] );
                }
            }
            typ1 = PT_FLOATING_CONSTANT;
            op1->op = typ2;
        }
    }
    if( PT_FLOATING_CONSTANT == typ1 ) {
        op1 = foldFloating( expr->cgop, op1, op2->u.floating_constant );
    } else if( SignedIntType( op1->type ) ) {
        if( NULL == Integral64Type( op1->type )
         && NULL == Integral64Type( op2->type ) ) {
            op1 = foldInt( expr->cgop, op1, op2->u.int_constant );
        } else {
            op1 = foldInt64( expr->cgop, op1, op2->u.int64_constant );
        }
    } else {
        if( NULL == Integral64Type( op1->type )
         && NULL == Integral64Type( op2->type ) ) {
            op1 = foldUInt( expr->cgop, op1, op2->u.uint_constant );
        } else {
            op1 = foldUInt64( expr->cgop, op1, op2->u.int64_constant );
        }
    }
    if( op1 != NULL ) {
        /* binary op was folded! */
        if( has_decoration ) {
            orig1->u.subtree[1] = NULL;
            orig2->u.subtree[1] = NULL;
        } else {
            expr->u.subtree[0] = NULL;
        }
        op1 = castConstant( op1, type, &cast_happened );
        op1 = PTreeCopySrcLocation( op1, expr );
        NodeFreeDupedExpr( expr );
        return( op1 );
    }

    return( expr );
}

PTREE Fold( PTREE expr )        // Fold expression
/**********************/
{
    switch( expr->op ) {
    case PT_UNARY :
        if( expr->u.subtree[0] != NULL ) {
            expr = FoldUnary( expr );
        }
        break;
    case PT_BINARY :
        if( expr->u.subtree[0] != NULL
         && expr->u.subtree[1] != NULL ) {
            expr = FoldBinary( expr );
        }
        break;
    }
    return( expr );
}
