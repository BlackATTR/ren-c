//
//  File: %sys-action.h
//  Summary: {action! defs AFTER %tmp-internals.h (see: %sys-rebact.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Rebol Open Source Contributors
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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Using a technique strongly parallel to contexts, an action is identified
// by an array which acts as its "paramlist".  The 0th element of that array
// is an archetypal value of the ACTION!.  That is followed by 1..NUM_PARAMS
// cells that have REB_XXX types higher than REB_MAX (e.g. "pseudotypes").
// These PARAM cells are not intended to be leaked to the user...they
// indicate the parameter type (normal, quoted, local).  The parameter cell's
// payload holds a typeset, and the extra holds the symbol.
//
// Each ACTION! instance cell (including the one that can be found in the [0]
// slot of the parameter list) has also a "details" field.  This is another
// array that holds the instance data used by the C native "dispatcher"
// function, which lives in MISC(details).dispatcher).  The details are how
// the same dispatcher can have different effects:
//
// What the details array holds varies by dispatcher:
//
//     USER FUNCTIONS: 1-element array w/a BLOCK!, the body of the function
//     GENERICS: 1-element array w/WORD! "verb" (OPEN, APPEND, etc)
//     SPECIALIZATIONS: 1-element array containing an exemplar FRAME! value
//     ROUTINES/CALLBACKS: stylized array (REBRIN*)
//     TYPECHECKERS: the TYPESET! to check against
//
// Since plain natives only need the C function, the body is optionally used
// to store a block of Rebol code that is equivalent to the native, for
// illustrative purposes.  (a "fake" answer for SOURCE)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// NOTES:
//
// * Unlike contexts, an ACTION! does not have values of its own, only
//   parameter definitions (or "params").  The arguments ("args") come from an
//   action's instantiation on the stack, viewed as a context using a FRAME!.
//
// * Paramlists may contain hidden fields, if they are specializations...
//   because they have to have the right number of slots to line up with the
//   frame of the underlying function.
//
// * The `misc.meta` field of the paramlist holds a meta object (if any) that
//   describes the function.  This is read by help.
//
// * By storing the C function dispatcher pointer in the `details` array node
//   instead of in the value cell itself, it also means the dispatcher can be
//   HIJACKed--or otherwise hooked to affect all instances of a function.
//


//=//// PARAMLIST_FLAG_HAS_RETURN /////////////////////////////////////////=//
//
// Has a definitional RETURN in the last paramlist slot.
//
#define PARAMLIST_FLAG_HAS_RETURN \
    ARRAY_FLAG_23


//=//// PARAMLIST_FLAG_POSTPONES_ENTIRELY /////////////////////////////////=//
//
// A postponing operator causes everything on its left to run before it will.
// Like a deferring operator, it is only allowed to appear after the last
// parameter of an expression except it closes out *all* the parameters on
// the stack vs. just one.
//
#define PARAMLIST_FLAG_POSTPONES_ENTIRELY \
    ARRAY_FLAG_24


//=//// PARAMLIST_FLAG_IS_INVISIBLE ///////////////////////////////////////=//
//
// This is a calculated property, which is cached by Make_Action().
//
// An "invisible" function is one that does not touch its frame output cell,
// leaving it completely alone.  This is how `10 comment ["hi"] + 20` can
// work...if COMMENT destroyed the 10 in the output cell it would be lost and
// the addition could no longer work.
//
#define PARAMLIST_FLAG_IS_INVISIBLE \
    ARRAY_FLAG_25


//=//// PARAMLIST_FLAG_DEFERS_LOOKBACK ////////////////////////////////////=//
//
// Special action property set with TWEAK.  Used by THEN, ELSE, and ALSO.
//
// Tells you whether a function defers its first real argument when used as a
// lookback.  Because lookback dispatches cannot use refinements, the answer
// is always the same for invocation via a plain word.
//
#define PARAMLIST_FLAG_DEFERS_LOOKBACK \
    ARRAY_FLAG_26


//=//// PARAMLIST_FLAG_QUOTES_FIRST ///////////////////////////////////////=//
//
// This is a calculated property, which is cached by Make_Action().
//
// This is another cached property, needed because lookahead/lookback is done
// so frequently, and it's quicker to check a bit on the function than to
// walk the parameter list every time that function is called.
//
#define PARAMLIST_FLAG_QUOTES_FIRST \
    ARRAY_FLAG_27


//=//// PARAMLIST_FLAG_SKIPPABLE_FIRST ////////////////////////////////////=//
//
// This is a calculated property, which is cached by Make_Action().
//
// It is good for the evaluator to have a fast test for knowing if the first
// argument to a function is willing to be skipped, as this comes into play
// in quote resolution.  (It's why `x: default [10]` can have default looking
// for SET-WORD! and SET-PATH! to its left, but `case [... default [x]]` can
// work too when it doesn't see a SET-WORD! or SET-PATH! to the left.)
//
#define PARAMLIST_FLAG_SKIPPABLE_FIRST \
    ARRAY_FLAG_28


//=//// PARAMLIST_FLAG_IS_NATIVE //////////////////////////////////////////=//
//
// Native functions are flagged that their dispatcher represents a native in
// order to say that their ACT_DETAILS() follow the protocol that the [0]
// slot is "equivalent source" (may be a TEXT!, as in user natives, or a
// BLOCK!).  The [1] slot is a module or other context into which APIs like
// rebRun() etc. should consider for binding, in addition to lib.  A BLANK!
// in the 1 slot means no additional consideration...bind to lib only.
//
#define PARAMLIST_FLAG_IS_NATIVE \
    ARRAY_FLAG_29


//=//// PARAMLIST_FLAG_UNLOADABLE_NATIVE //////////////////////////////////=//
//
// !!! Currently there isn't support for unloading extensions once they have
// been loaded.  Previously, this flag was necessary to indicate a native was
// in a DLL, and something like it may become necessary again.
//
#define PARAMLIST_FLAG_UNLOADABLE_NATIVE \
    ARRAY_FLAG_30


//=//// PARAMLIST_FLAG_RETURN_REQUOTES ////////////////////////////////////=//
//
// This is a cached property with a slight performance advantage for the
// evaluator, as it doesn't have to go find the RETURN parameter to know if
// it should apply the requote.  It is a minor optimization, and could be
// sacrificed if this bit were needed for something else.
//
#define PARAMLIST_FLAG_RETURN_REQUOTES \
    ARRAY_FLAG_31


// These are the flags which are scanned for and set during Make_Action
//
#define PARAMLIST_MASK_CACHED \
    (PARAMLIST_FLAG_IS_INVISIBLE | PARAMLIST_FLAG_RETURN_REQUOTES \
        | PARAMLIST_FLAG_QUOTES_FIRST | PARAMLIST_FLAG_SKIPPABLE_FIRST)

// These flags should be copied when specializing or adapting.  They may not
// be derivable from the paramlist (e.g. a native with no RETURN does not
// track if it requotes beyond the paramlist).
//
#define PARAMLIST_MASK_INHERIT \
    (PARAMLIST_FLAG_DEFERS_LOOKBACK | PARAMLIST_FLAG_POSTPONES_ENTIRELY)


#define SET_ACTION_FLAG(s,name) \
    (cast(REBSER*, ACT(s))->header.bits |= PARAMLIST_FLAG_##name)

#define GET_ACTION_FLAG(s,name) \
    ((cast(REBSER*, ACT(s))->header.bits & PARAMLIST_FLAG_##name) != 0)

#define CLEAR_ACTION_FLAG(s,name) \
    (cast(REBSER*, ACT(s))->header.bits &= ~PARAMLIST_FLAG_##name)

#define NOT_ACTION_FLAG(s,name) \
    ((cast(REBSER*, ACT(s))->header.bits & PARAMLIST_FLAG_##name) == 0)



//=//// PSEUDOTYPES FOR RETURN VALUES /////////////////////////////////////=//
//
// An arbitrary cell pointer may be returned from a native--in which case it
// will be checked to see if it is thrown and processed if it is, or checked
// to see if it's an unmanaged API handle and released if it is...ultimately
// putting the cell into f->out.
//
// However, pseudotypes can be used to indicate special instructions to the
// evaluator.
//

// This signals that the evaluator is in a "thrown state".
//
#define R_THROWN \
    cast(REBVAL*, &PG_R_Thrown)

// See PARAMLIST_FLAG_INVISIBLE...this is what any function with that flag
// needs to return.
//
// It is also used by path dispatch when it has taken performing a SET-PATH!
// into its own hands, but doesn't want to bother saying to move the value
// into the output slot...instead leaving that to the evaluator (as a
// SET-PATH! should always evaluate to what was just set)
//
#define R_INVISIBLE \
    cast(REBVAL*, &PG_R_Invisible)

// If Eval_Core gets back an REB_R_REDO from a dispatcher, it will re-execute
// the f->phase in the frame.  This function may be changed by the dispatcher
// from what was originally called.
//
// If EXTRA(Any).flag is not set on the cell, then the types will be checked
// again.  Note it is not safe to let arbitrary user code change values in a
// frame from expected types, and then let those reach an underlying native
// who thought the types had been checked.
//
#define R_REDO_UNCHECKED \
    cast(REBVAL*, &PG_R_Redo_Unchecked)

#define R_REDO_CHECKED \
    cast(REBVAL*, &PG_R_Redo_Checked)


// Path dispatch used to have a return value PE_SET_IF_END which meant that
// the dispatcher itself should realize whether it was doing a path get or
// set, and if it were doing a set then to write the value to set into the
// target cell.  That means it had to keep track of a pointer to a cell vs.
// putting the bits of the cell into the output.  This is now done with a
// special REB_R_REFERENCE type which holds in its payload a RELVAL and a
// specifier, which is enough to be able to do either a read or a write,
// depending on the need.
//
// !!! See notes in %c-path.c of why the R3-Alpha path dispatch is hairier
// than that.  It hasn't been addressed much in Ren-C yet, but needs a more
// generalized design.
//
#define R_REFERENCE \
    cast(REBVAL*, &PG_R_Reference)

// This is used in path dispatch, signifying that a SET-PATH! assignment
// resulted in the updating of an immediate expression in pvs->out, meaning
// it will have to be copied back into whatever reference cell it had been in.
//
#define R_IMMEDIATE \
    cast(REBVAL*, &PG_R_Immediate)

#define R_UNHANDLED \
    cast(REBVAL*, &PG_End_Node)


inline static REBARR *ACT_PARAMLIST(REBACT *a) {
    assert(GET_ARRAY_FLAG(&a->paramlist, IS_PARAMLIST));
    return &a->paramlist;
}

#define ACT_ARCHETYPE(a) \
    cast(REBVAL*, cast(REBSER*, ACT_PARAMLIST(a))->content.dynamic.data)

#define ACT_DISPATCHER(a) \
    (MISC(PAYLOAD(Action, ACT_ARCHETYPE(a)).details).dispatcher)

#define ACT_DETAILS(a) \
    PAYLOAD(Action, ACT_ARCHETYPE(a)).details

// These are indices into the details array agreed upon by actions which have
// the PARAMLIST_FLAG_IS_NATIVE set.
//
#define IDX_NATIVE_BODY 0 // text string source code of native (for SOURCE)
#define IDX_NATIVE_CONTEXT 1 // libRebol binds strings here (and lib)
#define IDX_NATIVE_MAX (IDX_NATIVE_CONTEXT + 1)

inline static REBVAL *ACT_PARAM(REBACT *a, REBCNT n) {
    assert(n != 0 and n < ARR_LEN(ACT_PARAMLIST(a)));
    return SER_AT(REBVAL, SER(ACT_PARAMLIST(a)), n);
}

#define ACT_NUM_PARAMS(a) \
    (cast(REBSER*, ACT_PARAMLIST(a))->content.dynamic.len - 1)

#define ACT_META(a) \
    MISC(a).meta



// The concept of the "underlying" function is the one which has the actual
// correct paramlist identity to use for binding in adaptations.
//
// e.g. if you adapt an adaptation of a function, the keylist referred to in
// the frame has to be the one for the inner function.  Using the adaptation's
// parameter list would write variables the adapted code wouldn't read.
//
#define ACT_UNDERLYING(a) \
    (LINK(a).underlying)


// An efficiency trick makes functions that do not have exemplars NOT store
// nullptr in the LINK(info).specialty node in that case--instead the params.
// This makes Push_Action() slightly faster in assigning f->special.
//
inline static REBCTX *ACT_EXEMPLAR(REBACT *a) {
    REBARR *details = PAYLOAD(Action, ACT_ARCHETYPE(a)).details;
    REBARR *specialty = LINK(details).specialty;
    if (GET_ARRAY_FLAG(specialty, IS_VARLIST))
        return CTX(specialty);

    return nullptr;
}

inline static REBVAL *ACT_SPECIALTY_HEAD(REBACT *a) {
    REBARR *details = PAYLOAD(Action, ACT_ARCHETYPE(a)).details;
    REBSER *s = SER(LINK(details).specialty);
    return cast(REBVAL*, s->content.dynamic.data) + 1; // skip archetype/root
}


// There is no binding information in a function parameter (typeset) so a
// REBVAL should be okay.
//
#define ACT_PARAMS_HEAD(a) \
    (cast(REBVAL*, SER(ACT_PARAMLIST(a))->content.dynamic.data) + 1)


inline static REBACT *VAL_ACTION(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_ACTION); // so it works on literals
    REBSER *s = SER(PAYLOAD(Action, v).paramlist);
    if (GET_SERIES_INFO(s, INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return ACT(s);
}

#define VAL_ACT_PARAMLIST(v) \
    ACT_PARAMLIST(VAL_ACTION(v))

#define VAL_ACT_NUM_PARAMS(v) \
    ACT_NUM_PARAMS(VAL_ACTION(v))

#define VAL_ACT_PARAMS_HEAD(v) \
    ACT_PARAMS_HEAD(VAL_ACTION(v))

#define VAL_ACT_PARAM(v,n) \
    ACT_PARAM(VAL_ACTION(v), n)

inline static REBARR *VAL_ACT_DETAILS(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_ACTION);
    return PAYLOAD(Action, v).details;
}

inline static REBNAT VAL_ACT_DISPATCHER(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_ACTION);
    return MISC(PAYLOAD(Action, v).details).dispatcher;
}

inline static REBCTX *VAL_ACT_META(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_ACTION);
    return MISC(PAYLOAD(Action, v).paramlist).meta;
}


// Native values are stored in an array at boot time.  These are convenience
// routines for accessing them, which should compile to be as efficient as
// fetching any global pointer.

#define NAT_VALUE(name) \
    (&Natives[N_##name##_ID])

#define NAT_ACTION(name) \
    VAL_ACTION(NAT_VALUE(name))


// A fully constructed action can reconstitute the ACTION! REBVAL
// that is its canon form from a single pointer...the REBVAL sitting in
// the 0 slot of the action's paramlist.
//
static inline REBVAL *Init_Action_Unbound(
    RELVAL *out,
    REBACT *a
){
  #if !defined(NDEBUG)
    Extra_Init_Action_Checks_Debug(a);
  #endif
    ENSURE_ARRAY_MANAGED(ACT_PARAMLIST(a));
    Move_Value(out, ACT_ARCHETYPE(a));
    assert(VAL_BINDING(out) == UNBOUND);
    return KNOWN(out);
}

static inline REBVAL *Init_Action_Maybe_Bound(
    RELVAL *out,
    REBACT *a,
    REBNOD *binding // allowed to be UNBOUND
){
  #if !defined(NDEBUG)
    Extra_Init_Action_Checks_Debug(a);
  #endif
    ENSURE_ARRAY_MANAGED(ACT_PARAMLIST(a));
    Move_Value(out, ACT_ARCHETYPE(a));
    assert(VAL_BINDING(out) == UNBOUND);
    INIT_BINDING(out, binding);
    return KNOWN(out);
}
