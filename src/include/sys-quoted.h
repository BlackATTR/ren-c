//
//  File: %sys-quoted.h
//  Summary: {Definitions for QUOTED! Datatype}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
//=////////////////////////////////////////////////////////////////////////=//
//
// In Ren-C, any value can be "quote" escaped, any number of times.  As there
// is no limit to how many levels of escaping there can be, the general case
// of the escaping cannot fit in a value cell, so a "singular" array is used
// (a compact form with only a series tracking node, sizeof(REBVAL)*2)
//
// The depth is the number of apostrophes, e.g. ''''X is a depth of 4.  It is
// stored in the cell payload and not the MISC() or LINK() of the singular,
// so that when you add or remove quote levels to the same value a new series
// isn't required...the cell just has a different count.
//
// HOWEVER... there is an efficiency trick, which uses the KIND_BYTE() div 4
// as the "lit level" of a value.  Then the byte mod 4 becomes the actual
// type.  So only an actual REB_QUOTED at "apparent lit-level 0" has its own
// payload...as a last resort if the level exceeded what the type byte can
// encode.
//
// This saves on storage and GC load for small levels of quotedness, at the
// cost of making VAL_TYPE() do an extra comparison to clip all values above
// 64 to act as REB_QUOTED.  Operations like IS_WORD() are not speed affected,
// as they do not need to worry about the aliasing and can just test the byte
// against the unquoted REB_WORD value they are interested in.
//

inline static REBCNT VAL_QUOTED_DEPTH(const RELVAL *v) {
    if (KIND_BYTE(v) >= REB_64) // shallow enough to use type byte trick...
        return KIND_BYTE(v) / REB_64; // ...see explanation above
    assert(KIND_BYTE(v) == REB_QUOTED);
    return PAYLOAD(Quoted, v).depth;
}

inline static REBCNT VAL_NUM_QUOTES(const RELVAL *v) {
    if (not IS_QUOTED(v))
        return 0;
    return VAL_QUOTED_DEPTH(v);
}


// It is necessary to be able to store relative values in escaped cells.
//
inline static RELVAL *Quotify_Core(
    RELVAL *v,
    REBCNT depth
){
    if (KIND_BYTE(v) == REB_QUOTED) { // reuse payload, bump count
        assert(PAYLOAD(Quoted, v).depth > 3); // or should've used kind byte
        PAYLOAD(Quoted, v).depth += depth;
        return v;
    }

    enum Reb_Kind kind = cast(enum Reb_Kind, KIND_BYTE(v) % REB_64);
    depth += KIND_BYTE(v) / REB_64;

    if (depth <= 3) { // can encode in a cell with no REB_QUOTED payload
        mutable_KIND_BYTE(v) = kind + (REB_64 * depth);
    }
    else {
        // No point having ARRAY_HAS_FILE_LINE when only deep levels of a
        // literal would have it--wastes time/storage to save it.
        //
        // An efficiency trick here could point to VOID_VALUE, BLANK_VALUE,
        // NULLED_CELL, etc. in those cases, so long as GC knew.  (But how
        // efficient do 4-level-deep-quoted nulls need to be, really?)
        //
        REBARR *a = Alloc_Singular(
            NODE_FLAG_MANAGED | ARRAY_FLAG_NULLEDS_LEGAL
        );

        // This is an uncomfortable situation of moving values without a
        // specifier; but it needs to be done otherwise you could not have
        // literals in function bodies.  What it means is that you should
        // not be paying attention to the cell bits for making decisions
        // about specifiers and such.  The format bits of this cell are
        // essentially noise, and only the literal's specifier should be used.

        RELVAL *cell = ARR_SINGLE(a);
        Move_Value_Header(cell, v);
        mutable_KIND_BYTE(cell) = kind; // escaping only in literal
        cell->extra = v->extra;
        cell->payload = v->payload;
      #if !defined(NDEBUG)
        SET_CELL_FLAG(cell, PROTECTED); // maybe shared; can't change
      #endif
 
        RESET_VAL_HEADER(v, REB_QUOTED, CELL_MASK_NONE);
        if (Is_Bindable(cell))
            v->extra = cell->extra; // must be in sync with cell (if binding)
        else {
            // We say all REB_QUOTED cells are bindable, so their binding gets
            // checked even if the contained cell isn't bindable.  By setting
            // the binding to null if the contained cell isn't bindable, that
            // prevents needing to make Is_Bindable() a more complex check,
            // we can just say yes always but have the binding null if not.
            //
            EXTRA(Binding, v).node = nullptr;
        }
        PAYLOAD(Quoted, v).cell = cell;
        PAYLOAD(Quoted, v).depth = depth;
    }

    return v;
}

#if !defined(CPLUSPLUS_11)
    #define Quotify Quotify_Core
#else
    inline static REBVAL *Quotify(REBVAL *v, REBCNT depth)
        { return KNOWN(Quotify_Core(v, depth)); }

    inline static RELVAL *Quotify(RELVAL *v, REBCNT depth)
        { return Quotify_Core(v, depth); }
#endif


// Only works on small escape levels that fit in a cell (<=3).  So it can
// do '''X -> ''X, ''X -> 'X or 'X -> X.  Use Unquotify() for the more
// generic routine, but this is needed by the evaluator most commonly.
//
inline static RELVAL *Unquotify_In_Situ(RELVAL *v, REBCNT unquotes)
{
    assert(KIND_BYTE(v) >= REB_64); // not an in-situ quoted value otherwise
    assert(cast(REBCNT, KIND_BYTE(v) / REB_64) >= unquotes);
    mutable_KIND_BYTE(v) -= REB_64 * unquotes;
    assert(
        KIND_BYTE(v) % 64 != REB_0
        and KIND_BYTE(v) % 64 != REB_QUOTED
        and KIND_BYTE(v) % 64 < REB_MAX
    );
    return v;
}


// Turns 'X into X, or '''''[1 + 2] into '''(1 + 2), etc.
//
// Works on escape levels that fit in the cell (<= 3) as well as those that
// require a second cell to point at in a REB_QUOTED payload.
//
inline static RELVAL *Unquotify_Core(RELVAL *v, REBCNT unquotes) {
    if (unquotes == 0)
        return v;

    if (KIND_BYTE(v) != REB_QUOTED)
        return Unquotify_In_Situ(v, unquotes);

    REBCNT depth = PAYLOAD(Quoted, v).depth;
    assert(depth > 3 and depth >= unquotes);
    depth -= unquotes;

    RELVAL *cell = PAYLOAD(Quoted, v).cell;
    assert(
        KIND_BYTE(cell) != REB_0
        and KIND_BYTE(cell) != REB_QUOTED
        and KIND_BYTE(cell) < REB_MAX
    );

    if (depth > 3) // still can't do in-situ escaping within a single cell
        PAYLOAD(Quoted, v).depth = depth;
    else {
        Move_Value_Header(v, cell);
        mutable_KIND_BYTE(v) += (REB_64 * depth);
        assert(
            not Is_Bindable(cell) or
            EXTRA(Binding, v).node == EXTRA(Binding, cell).node // must sync
        );
        v->payload = cell->payload;
    }
    return v;
}

#if !defined(CPLUSPLUS_11)
    #define Unquotify Unquotify_Core
#else
    inline static REBVAL *Unquotify(REBVAL *v, REBCNT depth)
        { return KNOWN(Unquotify_Core(v, depth)); }

    inline static RELVAL *Unquotify(RELVAL *v, REBCNT depth)
        { return Unquotify_Core(v, depth); }
#endif


inline static const REBCEL *VAL_UNESCAPED(const RELVAL *v) {
    if (KIND_BYTE(v) != REB_QUOTED)
        return v; // kind byte may be > 64

    // The reason this routine returns `const` is because you can't modify
    // the contained value without affecting other views of it, if it is
    // shared in an escaping.  Modifications must be done with awareness of
    // the original RELVAL, and that it might be a QUOTED!.
    //
    return PAYLOAD(Quoted, v).cell;
}


inline static REBCNT Dequotify(RELVAL *v) {
    if (KIND_BYTE(v) != REB_QUOTED) {
        REBCNT depth = KIND_BYTE(v) / REB_64;
        mutable_KIND_BYTE(v) %= REB_64;
        return depth;
    }

    REBCNT depth = PAYLOAD(Quoted, v).depth;
    RELVAL *cell = PAYLOAD(Quoted, v).cell;
    assert(KIND_BYTE(cell) != REB_QUOTED and KIND_BYTE(cell) < REB_64);

    Move_Value_Header(v, cell);
  #if !defined(NDEBUG)
    if (Is_Bindable(cell))
        assert(EXTRA(Binding, v).node == EXTRA(Binding, cell).node);
    else
        assert(not EXTRA(Binding, v).node);
  #endif
    v->extra = cell->extra;
    v->payload = cell->payload;
    return depth;
}


// !!! Temporary workaround for what was IS_LIT_WORD() (now not its own type)
//
inline static bool IS_QUOTED_WORD(const RELVAL *v) {
    return IS_QUOTED(v)
        and VAL_QUOTED_DEPTH(v) == 1
        and CELL_KIND(VAL_UNESCAPED(v)) == REB_WORD;
}

// !!! Temporary workaround for what was IS_LIT_PATH() (now not its own type)
//
inline static bool IS_QUOTED_PATH(const RELVAL *v) {
    return IS_QUOTED(v)
        and VAL_QUOTED_DEPTH(v) == 1
        and CELL_KIND(VAL_UNESCAPED(v)) == REB_PATH;
}
