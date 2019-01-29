//
//  File: %sys-rebval.h
//  Summary: {any-value! defs BEFORE %tmp-internals.h (see: %sys-value.h)}
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
// REBVAL is the structure/union for all Rebol values. It's designed to be
// four C pointers in size (so 16 bytes on 32-bit platforms and 32 bytes
// on 64-bit platforms).  Operation will be most efficient with those sizes,
// and there are checks on boot to ensure that `sizeof(REBVAL)` is the
// correct value for the platform.  But from a mechanical standpoint, the
// system should be *able* to work even if the size is different.
//
// Of the four 32-or-64-bit slots that each value has, the first is used for
// the value's "Header".  This includes the data type, such as REB_INTEGER,
// REB_BLOCK, REB_TEXT, etc.  Then there are flags which are for general
// purposes that could apply equally well to any type of value (including
// whether the value should have a new-line after it when molded out inside
// of a block).
//
// Obviously, an arbitrary long string won't fit into the remaining 3*32 bits,
// or even 3*64 bits!  You can fit the data for an INTEGER or DECIMAL in that
// (at least until they become arbitrary precision) but it's not enough for
// a generic BLOCK! or an ACTION! (for instance).  So the remaining bits
// often will point to one or more Rebol "nodes" (see %sys-series.h for an
// explanation of REBSER, REBARR, REBCTX, and REBMAP.)
//
// So the next part of the structure is the "Extra".  This is the size of one
// pointer, which sits immediately after the header (that's also the size of
// one pointer).
//
// This sets things up for the "Payload"--which is the size of two pointers.
// It is broken into a separate structure at this position so that on 32-bit
// platforms, it can be aligned on a 64-bit boundary (assuming the REBVAL's
// starting pointer was aligned on a 64-bit boundary to start with).  This is
// important for 64-bit value processing on 32-bit platforms, which will
// either be slow or crash if reads of 64-bit floating points/etc. are done
// on unaligned locations.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Forward declarations are in %reb-defs.h
//
// * See %sys-rebnod.h for an explanation of FLAG_LEFT_BIT.  This file defines
//   those flags which are common to every value of every type.  Due to their
//   scarcity, they are chosen carefully.
//


// The GET_CELL_FLAG()/etc. macros splice together CELL_FLAG_ with the text
// you pass in (token pasting).  Since it does this, alias NODE_FLAG_XXX to
// CELL_FLAG_XXX so they can be used with those macros.
//
// * ARG_MARKED_CHECKED -- This uses the NODE_FLAG_MARKED bit on args in
//   action frames, and in particular specialization uses it to denote which
//   arguments in a frame are actually specialized.  This helps notice the
//   difference during an APPLY of encoded partial refinement specialization
//   encoding from just a user putting random values in a refinement slot.
//
// * OUT_MARKED_STALE -- This application of NODE_FLAG_MARKED helps show
//   when an evaluation step didn't add any new output, but it does not
//   overwrite the contents of the out cell.  This allows the evaluator to
//   leave a value in the output slot even if there is trailing invisible
//   evaluation to be done, such as in `[1 + 2 elide (print "Hi")]`, where
//   something like ALL would want to hold onto the 3 without needing to
//   cache it in some other location.  Stale out cells cannot be used as
//   left side input for enfix.
//
// **IMPORTANT**: This means that a routine being passed an arbitrary value
//   should not make assumptions about the marked bit.  It should only be
//   used in circumstances where some understanding of being "in control"
//   of the bit are in place--like processing an array a routine itself made.
//

#define CELL_FLAG_MANAGED NODE_FLAG_MANAGED
#define CELL_FLAG_ROOT NODE_FLAG_ROOT
#define CELL_FLAG_TRANSIENT NODE_FLAG_TRANSIENT
#define CELL_FLAG_STACK_LIFETIME NODE_FLAG_STACK

#define CELL_FLAG_ARG_MARKED_CHECKED NODE_FLAG_MARKED
#define CELL_FLAG_OUT_MARKED_STALE NODE_FLAG_MARKED
#define CELL_FLAG_VAR_MARKED_REUSE NODE_FLAG_MARKED
#define CELL_FLAG_MARKED_REMOVE NODE_FLAG_MARKED
#define CELL_FLAG_BIND_MARKED_REUSE NODE_FLAG_MARKED


// v-- BEGIN GENERAL CELL BITS HERE, third byte in the header


//=//// CELL_FLAG_PROTECTED ///////////////////////////////////////////////=//
//
// Values can carry a user-level protection bit.  The bit is not copied by
// Move_Value(), and hence reading a protected value and writing it to
// another location will not propagate the protectedness from the original
// value to the copy.
//
// Note: Any formatted cell can be tested for this, even if it is "trash".
// This means writing routines that are putting data into a cell for the first
// time can check the bit.  (Series, having more than one kind of protection,
// put those bits in the "info" so they can all be checked at once...otherwise
// there might be a shared NODE_FLAG_PROTECTED in common.)
//
#define CELL_FLAG_PROTECTED \
    FLAG_LEFT_BIT(16)


//=//// CELL_FLAG_FALSEY //////////////////////////////////////////////////=//
//
// This flag is used as a quick cache on NULL, BLANK! or LOGIC! false values.
// These are the only three values that return true from the NOT native
// (a.k.a. "conditionally false").  All other types return true from TO-LOGIC
// or its synonym, "DID".
//
// (It is also placed on END cells and TRASH cells, to speed up the VAL_TYPE()
// check for finding illegal types...by only checking falsey types.)
//
// Because of this cached bit, LOGIC! does not need to store any data in its
// payload... its data of being true or false is already covered by this
// header bit.
//
#define CELL_FLAG_FALSEY \
    FLAG_LEFT_BIT(17)


//=//// CELL_FLAG_NEWLINE_BEFORE //////////////////////////////////////////=//
//
// When the array containing a value with this flag set is molding, that will
// output a new line *before* molding the value.  This flag works in tandem
// with a flag on the array itself which manages whether there should be a
// newline output before the closing array delimiter: ARRAY_FLAG_NEWLINE_AT_TAIL.
//
// The bit is set initially by what the scanner detects, and then left to the
// user's control after that.
//
// !!! The native `new-line` is used set this, which has a somewhat poor
// name considering its similarity to `newline` the line feed char.
//
// !!! Currently, ANY-PATH! rendering just ignores this bit.  Some way of
// representing paths with newlines in them may be needed.
//
#define CELL_FLAG_NEWLINE_BEFORE \
    FLAG_LEFT_BIT(18)


//=//// CELL_FLAG_UNEVALUATED /////////////////////////////////////////////=//
//
// Some functions wish to be sensitive to whether or not their argument came
// as a literal in source or as a product of an evaluation.  While all values
// carry the bit, it is only guaranteed to be meaningful on arguments in
// function frames...though it is valid on any result at the moment of taking
// it from Eval_Core_Throws().
//
// It is in the negative sense because the act of requesting it is uncommon,
// e.g. from the QUOTE operator.  So most Init_Blank() or other assignment
// should default to being "evaluative".
//
// !!! This concept is somewhat dodgy and experimental, but it shows promise
// in addressing problems like being able to give errors if a user writes
// something like `if [x > 2] [print "true"]` vs. `if x > 2 [print "true"]`,
// while still tolerating `item: [a b c] | if item [print "it's an item"]`. 
// That has a lot of impact for the new user experience.
//
#define CELL_FLAG_UNEVALUATED \
    FLAG_LEFT_BIT(19)


//=//// CELL_FLAG_ENFIXED /////////////////////////////////////////////////=//
//
// In Ren-C, there is only one kind of function (ACTION!).  But it's possible
// to tag a function value cell in a context as being "enfixed", hence it
// will acquire its first argument from the left.  See SET/ENFIX and ENFIX.
//
// The reasion it is a generic CELL_FLAG_XXX and not an PARAMLIST_FLAG_XXX is
// so that it can be dealt with without specifically knowing that the cell
// involved is an action.  One benefit is that testing for an enfix action
// can be done just by looking at this bit--since only actions have it set.
//
// But also, this bit is not copied by Move_Value.  As a result, if you say
// something like `foo: :+`, foo will contain the non-enfixed form of the
// function.  To do that would require more nuance in Move_Value if it were
// an PARAMLIST_FLAG_XXX, testing for action-ness vs. just masking it out.
//
#define CELL_FLAG_ENFIXED \
    FLAG_LEFT_BIT(20)


//=//// CELL_FLAG_EVAL_FLIP ///////////////////////////////////////////////=//
//
// This is a bit which should not be present on cells in user-exposed arrays.
//
// If a DO is happening with EVAL_FLAG_EXPLICIT_EVALUATE, only values which
// carry this bit will override it.  It may be the case that the flag on a
// value would signal a kind of quoting to suppress evaluation in ordinary
// evaluation (without EVAL_FLAG_EXPLICIT_EVALUATE), hence it is a "flip" bit.
//
#define CELL_FLAG_EVAL_FLIP \
    FLAG_LEFT_BIT(21) // IMPORTANT: Same bit as EVAL_FLAG_EXPLICIT_EVALUATE


//=//// CELL_FLAG_CONST ///////////////////////////////////////////////////=//
//
// A value that is CONST has read-only access to any series or data it points
// to, regardless of whether that data is in a locked series or not.  It is
// possible to get a mutable view on a const value by using MUTABLE, and a
// const view on a mutable value with CONST.
//
#define CELL_FLAG_CONST \
    FLAG_LEFT_BIT(22) // NOTE: Must be SAME BIT as EVAL_FLAG_CONST


//=//// CELL_FLAG_EXPLICITLY_MUTABLE //////////////////////////////////////=//
//
// While it may seem that a mutable value would be merely one that did not
// carry CELL_FLAG_CONST, there's a need for a separate bit to indicate when
// MUTABLE has been specified explicitly.  That way, evaluative situations
// like `do mutable compose [...]` or `make object! mutable load ...` can
// realize that they should switch into a mode which doesn't enforce const
// by default--which it would ordinarily do.
//
// If this flag did not exist, then to get the feature of disabled mutability
// would require every such operation taking something like a /MUTABLE
// refinement.  This moves the flexibility onto the values themselves.
//
// While CONST can be added by the system implicitly during an evaluation,
// the MUTABLE flag should only be added by running MUTABLE.
//
#define CELL_FLAG_EXPLICITLY_MUTABLE \
    FLAG_LEFT_BIT(23)


// After 8 bits for node flags, 8 bits for the datatype, and 8 generic value
// bits...there's only 8 more bits left on 32-bit platforms in the header.
//
// !!! This is slated for an interesting feature of fitting an immutable
// single element array into a cell.  The proposal is called "mirror bytes".

#define MIRROR_BYTE(v) \
    FOURTH_BYTE((v)->header)

#define mutable_MIRROR_BYTE(v) \
    mutable_FOURTH_BYTE((v)->header)


// Endlike headers have the second byte clear (to pass the IS_END() test).
// But they also have leading bits `10` so they don't look like a UTF-8
// string, and don't have NODE_FLAG_CELL set to prevents writing to them.
//
// !!! One must be careful in reading and writing bits initialized via
// different structure types.  As it is, setting and testing for ends is done
// with `unsigned char*` access of a whole byte, so it is safe...but there
// are nuances to be aware of:
//
// https://stackoverflow.com/q/51846048
//
inline static union Reb_Header Endlike_Header(uintptr_t bits) {
    assert(
        0 == (bits & (
            NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
            | FLAG_SECOND_BYTE(255)
        ))
    );
    union Reb_Header h;
    h.bits = bits | NODE_FLAG_NODE;
    return h;
}


//=//// CELL RESET AND COPY MASKS /////////////////////////////////////////=//
//
// It's important for operations that write to cells not to overwrite *all*
// the bits in the header, because some of those bits give information about
// the nature of the cell's storage and lifetime.  Similarly, if bits are
// being copied from one cell to another, those header bits must be masked
// out to avoid corrupting the information in the target cell.
//
// !!! In the future, the 64-bit build may put the integer stack level of a
// cell in the header--which would be part of the cell's masked out format.
//
// Additionally, operations that copy need to not copy any of those bits that
// are owned by the cell, plus additional bits that would be reset in the
// cell if overwritten but not copied.  For now, this is why `foo: :+` does
// not make foo an enfixed operation.
//
// Note that this will clear NODE_FLAG_FREE, so it should be checked by the
// debug build before resetting.
//
// Note also that NODE_FLAG_MARKED usage is a relatively new concept, e.g.
// to allow REMOVE-EACH to mark values in a locked series as to which should
// be removed when the enumeration is finished.  This *should* not be able
// to interfere with the GC, since userspace arrays don't use that flag with
// that meaning, but time will tell if it's a good idea to reuse the bit.
//

#define CELL_MASK_PERSIST \
    (NODE_FLAG_NODE | NODE_FLAG_CELL | NODE_FLAG_MANAGED | NODE_FLAG_ROOT \
        | CELL_FLAG_TRANSIENT | CELL_FLAG_STACK_LIFETIME)

#define CELL_MASK_COPY \
    ~(CELL_MASK_PERSIST | NODE_FLAG_MARKED | CELL_FLAG_PROTECTED \
        | CELL_FLAG_ENFIXED | CELL_FLAG_UNEVALUATED | CELL_FLAG_EVAL_FLIP)


//=//// CELL's `EXTRA` FIELD DEFINITION ///////////////////////////////////=//
//
// Each value cell has a header, "extra", and payload.  Having the header come
// first is taken advantage of by the byte-order-sensitive macros to be
// differentiated from UTF-8 strings, etc. (See: Detect_Rebol_Pointer())
//
// Conceptually speaking, one might think of the "extra" as being part of
// the payload.  But it is broken out into a separate field.  This is because
// the `binding` property is written using common routines for several
// different types.  If the common routine picked just one of the payload
// forms initialize, it would "disengage" the other forms.
//
// (C permits *reading* of common leading elements from another union member,
// even if that wasn't the last union used to write it.  But all bets are off
// for other unions if you *write* a leading member through another one.
// For longwinded details: http://stackoverflow.com/a/11996970/211160 )
//
// Another aspect of breaking out the "extra" is so that on 32-bit platforms,
// the starting address of the payload is on a 64-bit alignment boundary.
// See Reb_Integer, Reb_Decimal, and Reb_Typeset for examples where the 64-bit
// quantity requires things like REBDEC to have 64-bit alignment.  At time of
// writing, this is necessary for the "C-to-Javascript" emscripten build to
// work.  It's also likely preferred by x86.
//

struct Reb_Binding_Extra // see %sys-bind.h
{
    REBNOD* node;
};

struct Reb_Key_Extra // see %sys-action.h, %sys-context.h
{
    REBSTR *spelling; // UTF-8 byte series, name of parameter / context key
};

struct Reb_Handle_Extra // see %sys-handle.h
{
    REBARR *singular;
};

struct Reb_Date_Extra // see %sys-time.h
{
    REBYMD ymdz; // month/day/year/zone (time payload *may* hold nanoseconds) 
};

struct Reb_Partial_Extra // see %c-specialize.c (used with REB_X_PARTIAL)
{
    REBVAL *next; // links to next potential partial refinement arg
};

union Reb_Custom_Extra { // needed to beat strict aliasing, used in payload
    void *p;
    uintptr_t u;
    intptr_t i;
};

union Reb_Bytes_Extra {
    REBYTE common[sizeof(uint32_t) * 1];
    REBYTE varies[sizeof(void*) * 1];
};

union Reb_Value_Extra { //=/////////////////// ACTUAL EXTRA DEFINITION ////=//

    struct Reb_Binding_Extra Binding;
    struct Reb_Key_Extra Key;
    struct Reb_Handle_Extra Handle;
    struct Reb_Date_Extra Date;
    struct Reb_Partial_Extra Partial;

    union Reb_Custom_Extra Custom;
    union Reb_Bytes_Extra Bytes;

  #if !defined(NDEBUG)
    //
    // A tick field is included in all debug builds, not just those which
    // DEBUG_TRACK_CELLS...because negative signs are used to give a distinct
    // state to unreadable blanks.  See %sys-track.h and %sys-blank.h
    //
    intptr_t tick; // Note: will be negative for unreadable blanks
  #endif

    // The release build doesn't put anything in the ->extra field by default,
    // so sensitive compilers notice when cells are moved without that
    // initialization.  Rather than disable the warning, this can be used to
    // put some junk into it, but TRASH_POINTER_IF_DEBUG() won't subvert the
    // warning.  So just poke whatever pointer is at hand that is likely to
    // already be in a register and not meaningful (e.g. nullptr is a bad
    // value, because that could look like a valid non-binding)
    //
    void *trash;
};


//=//// CELL's `PAYLOAD` FIELD DEFINITION /////////////////////////////////=//
//
// The payload is located in the second half of the cell.  Since it consists
// of four platform pointers, the payload should be aligned on a 64-bit
// boundary even on 32-bit platorms.
//
// `Pointers` and `Bytes` provide a generic strategy for adding payloads
// after-the-fact.  This means clients (like extensions) don't have to have
// their payload declarations cluttering this file.
//
// IMPORTANT: `Bytes` should *not* be cast to an arbitrary pointer!!!  That
// would violate strict aliasing.  Only direct payload types should be used:
//
//     https://stackoverflow.com/q/41298619/
//

struct Reb_Quoted_Payload // see %sys-quoted.h
{
    RELVAL *cell; // lives in singular array, find with Singular_From_Cell()
    REBCNT depth; // kept in payload so allocation shares across quote levels
};

struct Reb_Character_Payload { REBUNI codepoint; }; // see %sys-char.h

struct Reb_Integer_Payload { REBI64 i64; }; // see %sys-integer.h

struct Reb_Decimal_Payload { REBDEC dec; }; // see %sys-decimal.h

struct Reb_Datatype_Payload // see %sys-datatype.h
{
    enum Reb_Kind kind;
    REBARR *spec;
};

struct Reb_Typeset_Payload // see %sys-typeset.h
{
    REBU64 bits; // One bit for each DATATYPE! (use with FLAGIT_KIND)
};

struct Reb_Series_Payload // see %sys-series.h
{
    REBSER *rebser; // vector-like-double-ended-queue of equal-sized items
    REBCNT index; // 0-based position (if it is 0, that means Rebol index 1)
};

struct Reb_Action_Payload // see %sys-action.h
{
    REBARR *paramlist; // see MISC.meta, LINK.underlying in %sys-rebser.h
    REBARR *details; // see MISC.dispatcher, LINK.specialty in %sys-rebser.h
};

struct Reb_Context_Payload // see %sys-context.h
{
    REBARR *varlist; // see MISC.meta, LINK.keysource in %sys-rebser.h
    REBACT *phase; // only used by FRAME! contexts, see %sys-frame.h
};

struct Reb_Word_Payload // see %sys-word.h
{
    REBSTR *spelling; // word's non-canonized spelling, UTF-8 string series
    REBINT index; // index of word in context (if binding is not null)
};

struct Reb_Varargs_Payload // see %sys-varargs.h
{
    REBINT signed_param_index; // if negative, consider the arg enfixed
    REBACT *phase; // where to look up parameter by its offset
};

struct Reb_Time_Payload { // see %sys-time.h
    REBI64 nanoseconds;
};

struct Reb_Pair_Payload // see %sys-pair.h
{
    REBVAL *pairing; // 2 values packed in a series node - see Alloc_Pairing()
};

struct Reb_Handle_Payload { // see %sys-handle.h
    union {
        void *pointer;
        CFUNC *cfunc; // C function/data pointers pointers may differ in size
    } data;
    uintptr_t length; // will be 0 if a cfunc, and nonzero otherwise
};

struct Reb_Library_Payload // see %sys-library.h
{
    REBARR *singular; // File discriptor in LINK.fd, meta in MISC.meta 
};

struct Reb_Custom_Payload // generic, for adding payloads after-the-fact
{
    union Reb_Custom_Extra first;
    union Reb_Custom_Extra second;
};

struct Reb_Partial_Payload // see %c-specialize.c (used with REB_X_PARTIAL)
{
    REBDSP dsp; // the DSP of this partial slot (if ordered on the stack)
    REBINT signed_index; // index in the paramlist, negative if not "in use"
};

union Reb_Bytes_Payload // IMPORTANT: Do not cast, use `Pointers` instead
{
    REBYTE common[sizeof(uint32_t) * 2]; // same on 32-bit/64-bit platforms
    REBYTE varies[sizeof(void*) * 2]; // size depends on platform
};

#if defined(DEBUG_TRACK_CELLS)
    struct Reb_Track_Payload // see %sys-track.h
    {
        const char *file; // is REBYTE (UTF-8), but char* for debug watch
        int line;
    };
#endif

union Reb_Value_Payload { //=/////////////// ACTUAL PAYLOAD DEFINITION ////=//

    struct Reb_Quoted_Payload Quoted;
    struct Reb_Character_Payload Character;
    struct Reb_Integer_Payload Integer;
    struct Reb_Decimal_Payload Decimal;
    struct Reb_Datatype_Payload Datatype;
    struct Reb_Typeset_Payload Typeset;
    struct Reb_Series_Payload Series;
    struct Reb_Action_Payload Action;
    struct Reb_Context_Payload Context;
    struct Reb_Word_Payload Word;
    struct Reb_Varargs_Payload Varargs;
    struct Reb_Time_Payload Time;
    struct Reb_Pair_Payload Pair;
    struct Reb_Handle_Payload Handle;
    struct Reb_Library_Payload Library;

    struct Reb_Partial_Payload Partial; // internal (see REB_X_PARTIAL)

    struct Reb_Custom_Payload Custom;
    union Reb_Bytes_Payload Bytes;

  #if defined(DEBUG_TRACK_CELLS) && !defined(DEBUG_TRACK_EXTEND_CELLS)
    //
    // Debug builds put the file and line number of initialization for a cell
    // into the payload.  It will remain there after initialization for types
    // that do not need a payload (NULL, VOID!, BLANK!, LOGIC!).  See the
    // DEBUG_TRACK_EXTEND_CELLS option for tracking even types with payloads,
    // and also see TOUCH_CELL() for how to update tracking at runtime.
    //
    struct Reb_Track_Payload Track;
  #endif

  #if !defined(NDEBUG) // unsafe "puns" for easy debug viewing in C watchlist
    int64_t i;
    void *p;
  #endif
};


//=//// COMPLETED 4-PLATFORM POINTER CELL DEFINITION //////////////////////=//
//
// This bundles up the cell into a structure.  The C++ build includes some
// special checks to make sure that overwriting one cell with another can't
// be done with direct assignment, such as `*dest = *src;`  Cells contain
// formatting bits that must be preserved, and some flag bits shouldn't be
// copied. (See: CELL_MASK_PRESERVE)
//
// Also, copying needs to be sensitive to the target slot.  If that slot is
// at a higher stack level than the source (or persistent in an array) then
// special handling is necessary to make sure any stack constrained pointers
// are "reified" and visible to the GC.
//
// Goal is that the mechanics are managed with low-level C, so the C++ build
// is just there to notice when you try to use a raw byte copy.  Use functions
// instead.  (See: Move_Value(), Blit_Cell(), Derelativize())
//
// Note: It is annoying that this means any structure that embeds a value cell
// cannot be assigned.  However, `struct Reb_Value` must be the type exported
// in both C and C++ under the same name and bit patterns.  Pretty much any
// attempt to work around this and create a base class that works in C too
// (e.g. Reb_Cell) would wind up violating strict aliasing.  Think *very hard*
// before changing!
//

#ifdef CPLUSPLUS_11
    struct Reb_Cell
#else
    struct Reb_Value
#endif
    {
        union Reb_Header header;
        union Reb_Value_Extra extra;
        union Reb_Value_Payload payload;

      #if defined(DEBUG_TRACK_EXTEND_CELLS)
        //
        // Lets you preserve the tracking info even if the cell has a payload.
        // This doubles the cell size, but can be a very helpful debug option.
        //
        struct Reb_Track_Payload track;
        uintptr_t tick; // stored in the Reb_Value_Extra for basic tracking
        uintptr_t touch; // see TOUCH_CELL(), pads out to 4 * sizeof(void*)
      #endif

      #ifdef CPLUSPLUS_11
      public:
        Reb_Cell () = default;
      private:
        Reb_Cell (Reb_Cell const & other) = delete;
        void operator= (Reb_Cell const &rhs) = delete;
      #endif
    };

#ifdef CPLUSPLUS_11
    //
    // A Reb_Relative_Value is a point of view on a cell where VAL_TYPE() can
    // be called and will always give back a value in range < REB_MAX.  All
    // KIND_BYTE() > REB_64 are considered to be REB_QUOTED variants of the
    // byte modulo 64.
    //
    struct Reb_Relative_Value : public Reb_Cell {};
#endif


#define PAYLOAD(Type, v) \
    (v)->payload.Type

#define EXTRA(Type, v) \
    (v)->extra.Type


//=////////////////////////////////////////////////////////////////////////=//
//
//  RELATIVE AND SPECIFIC VALUES (difference enforced in C++ build only)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A RELVAL is an equivalent struct layout to to REBVAL, but is allowed to
// have a REBACT* as its binding.  A relative value pointer can point to a
// specific value, but a relative word or array cannot be pointed to by a
// plain REBVAL*.  The RELVAL-vs-REBVAL distinction is purely commentary
// in the C build, but the C++ build makes REBVAL a type derived from RELVAL.
//
// RELVAL exists to help quarantine the bit patterns for relative words into
// the deep-copied-body of the function they are for.  To actually look them
// up, they must be paired with a FRAME! matching the actual instance of the
// running function on the stack they correspond to.  Once made specific,
// a word may then be freely copied into any REBVAL slot.
//
// In addition to ANY-WORD!, an ANY-ARRAY! can also be relative, if it is
// part of the deep-copied function body.  The reason that arrays must be
// relative too is in case they contain relative words.  If they do, then
// recursion into them must carry forward the resolving "specifier" pointer
// to be combined with any relative words that are seen later.
//

#ifdef CPLUSPLUS_11
    static_assert(
        std::is_standard_layout<struct Reb_Relative_Value>::value,
        "C++ RELVAL must match C layout: http://stackoverflow.com/a/7189821/"
    );

    struct Reb_Value : public Reb_Relative_Value
    {
      #if !defined(NDEBUG)
        Reb_Value () = default;
        ~Reb_Value () {
            assert(this->header.bits & (NODE_FLAG_NODE | NODE_FLAG_CELL));
        }
      #endif
    };

    static_assert(
        std::is_standard_layout<struct Reb_Value>::value,
        "C++ REBVAL must match C layout: http://stackoverflow.com/a/7189821/"
    );
#endif


// !!! Consider a more sophisticated macro/template, like in DEBUG_CHECK_CASTS
// though this is good enough for many usages for now.

#define VAL(p) \
    cast(const RELVAL*, (p))
