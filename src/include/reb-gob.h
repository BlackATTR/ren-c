//
//  File: %reb-gob.h
//  Summary: "Graphical compositing objects"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
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
// GOBs are lower-level graphics object used by the compositing and rendering
// system of the /View system of R3-Alpha.  They represented either individual
// pieces of a GUI control (panes and their children) or top-level windows
// themselves.
//
// Because a GUI could contain thousands of GOBs, it was believed that they
// could not be implemented as ordinary OBJECT!s.  Instead they were made as
// small fixed-size structs (somewhat parallel to REBSER) which held pointers
// to dynamic series data, like pane lists or associated user data.  Because
// they held pointers to Rebol nodes, they had to have custom behavior in
// the garbage collector--meaning they shipped as part of the core, despite
// that there was no GUI in R3-Alpha's core open-source release.
//
// Ren-C has transitioned this so that GOBs work within the user-defined type
// system, where no custom GC behavior is needed.  e.g. a REBGOB is actually
// just a REBARR, and marked using the array marking mechanics.
//
// To keep memory usage in the same order of magnitude as R3-Alpha, the GOB!'s
// array is only 7 cells in length.  This allows it to fit into the 8 cell
// memory pool, when the END marker is taken into account.  To achieve this
// goal, creative use is made of "pseudotype" REB_G_XYF cells--to allow the
// packing of floats and flags into cells that don't participate in GC.  This
// gives an approximation of "struct-like" compactness for that inert data.
//
////=// NOTES ////////////////////////////////////////////////////////////=//
//
// !!! Note: Currently there is some marking behavior that has to be managed
// specially to recognize the REB_GOB type, and to mark the LINK() and MISC()
// fields of the array.  The plan is to continue generalizing this (e.g. with
// SERIES_INFO_MARK_LINK and SERIES_INFO_MARK_MISC that mark a generic
// REBNOD* that extensions use).  But the main point has been achieved...that
// there is no "gob memory pool" or special marking procedure in %m-gc.c
//
// GOB PAYLOAD:
//
//     REBGOB *gob;
//     REBCNT index;

// On the GOB array's REBSER node itself:
//
//     LINK.parent is the "parent GOB or window ptr"
//     MISC.owner is the "owner" (seemingly unused?)
//
// The offset, size, old_offset and old_size cells are REB_G_XYF cells that
// are GC-inert.  They use their payloads for x and y coordinates, but the
// extra slot is used for other things.
//
// (Note that only one byte of the extra on `size` and `old_size` are used at
// the moment, and `old_offset` still has all 32-bits of extra space.  So
// there are more bits to squeeze out if the complexity warranted it.)
//
enum {
    IDX_GOB_PANE,  // List of child GOBs, was REBSER, now REBARR for marking
    IDX_GOB_CONTENT,
    IDX_GOB_DATA,
    IDX_GOB_OFFSET_AND_FLAGS,  // location (x, y) in payload, flags in extra
    IDX_GOB_SIZE_AND_ALPHA,  // size (w, h) in payload, transparency in extra
    IDX_GOB_OLD_OFFSET,  // prior location in payload [extra is available]
    IDX_GOB_TYPE_AND_OLD_SIZE,  // prior size in payload, type in extra
    IDX_GOB_MAX
};

STATIC_ASSERT(IDX_GOB_MAX <= 7); // ideally true--see notes at top of file


enum Reb_Gob_Flags {
    //
    // !!! These were "GOB state flags".  Despite there being only 3 of them,
    // they were previously in a different place than the "GOB flags".
    //
    GOBS_OPEN = 1 << 0, // Window is open
    GOBS_ACTIVE = 1 << 1, // Window is active
    GOBS_NEW = 1 << 2, // Gob is new to pane (old-offset, old-size wrong)

    // These were just generically "GOB flags"
    //
    GOBF_TOP = 1 << 3, // Top level (window or output image)
    GOBF_WINDOW = 1 << 4, // Window (parent is OS window reference)
    GOBF_OPAQUE = 1 << 5, // Has no alpha
    GOBF_STATIC = 1 << 6, // Does not change
    GOBF_HIDDEN = 1 << 7, // Is hidden (e.g. hidden window)
    GOBF_RESIZE = 1 << 8, // Can be resized
    GOBF_NO_TITLE = 1 << 9, // Has window title
    GOBF_NO_BORDER = 1 << 10, // Has no window border
    GOBF_DROPABLE = 1 << 11, // [sic] Let window receive drag and drop
    GOBF_TRANSPARENT = 1 << 12, // Window is in transparent mode
    GOBF_POPUP = 1 << 13, // Window is a popup (with owner window)
    GOBF_MODAL = 1 << 14, // Modal event filtering
    GOBF_ON_TOP = 1 << 15, // The window is always on top
    GOBF_ACTIVE = 1 << 16, // Window is active
    GOBF_MINIMIZE = 1 << 17, // Window is minimized
    GOBF_MAXIMIZE = 1 << 18, // Window is maximized
    GOBF_RESTORE = 1 << 19, // Window is restored
    GOBF_FULLSCREEN = 1 << 20 // Window is fullscreen
};


// The GOB's "content" is a cell and may imply what kind of GOB it is (e.g
// an IMAGE! means GOBT_IMAGE).  But if the content is a BLOCK! it could mean
// other things.  So there's a separate type field.

enum Reb_Gob_Type {
    GOBT_NONE,  // BLANK!
    GOBT_COLOR,  // TUPLE!
    GOBT_IMAGE,  // IMAGE!
    GOBT_STRING,  // TEXT!
    GOBT_DRAW,  // BLOCK!
    GOBT_TEXT,  // BLOCK!
    GOBT_EFFECT  // BLOCK!
};


// Ren-C's PAIR! data type uses full precision values, thus supporting any
// INTEGER!, any DECIMAL!, or more generally any two values.  But that needs
// an extra allocation (albeit an efficient one, a single REBSER node, where
// the two values are packed into it with no allocation beyond the node).
//
// Whether it be important or not, GOB!s were conceived to pack their data
// more efficiently than that.  So the custom strategy for PAYLOAD() and
// EXTRA() allows compact possibilites using cells, so that it can use a
// float resolution and fit two floats in the payload, with the extra field
// left over for additional data.  This lets GOB!s use a "somewhat ordinary"
// array (though these XYF types are internal).

#define VAL_XYF_X(v)    PAYLOAD(Custom, (v)).first.f
#define VAL_XYF_Y(v)    PAYLOAD(Custom, (v)).second.f

inline static REBVAL *Init_XYF(
    RELVAL *out,
    REBD32 x,  // 32-bit floating point type, typically just `float`...
    REBD32 y   // there's no standard: https://stackoverflow.com/a/18705626/
){
    RESET_CELL(out, REB_G_XYF);
    VAL_XYF_X(out) = x;
    VAL_XYF_Y(out) = y;
    return cast(REBVAL*, out);
}

typedef struct gob_window {             // Maps gob to window
    REBGOB *gob;
    void* win;
    void* compositor;
} REBGOBWINDOWS;

#define GOB_X(g)        VAL_XYF_X(ARR_AT((g), IDX_GOB_OFFSET_AND_FLAGS))
#define GOB_Y(g)        VAL_XYF_Y(ARR_AT((g), IDX_GOB_OFFSET_AND_FLAGS))
#define GOB_W(g)        VAL_XYF_X(ARR_AT((g), IDX_GOB_SIZE_AND_ALPHA))
#define GOB_H(g)        VAL_XYF_Y(ARR_AT((g), IDX_GOB_SIZE_AND_ALPHA))

#define GOB_LOG_X(g)        LOG_COORD_X(GOB_X(g))
#define GOB_LOG_Y(g)        LOG_COORD_Y(GOB_Y(g))
#define GOB_LOG_W(g)        LOG_COORD_W(GOB_X(g))
#define GOB_LOG_H(g)        LOG_COORD_H(GOB_Y(g))

#define GOB_X_INT(g)    ROUND_TO_INT(GOB_X(g))
#define GOB_Y_INT(g)    ROUND_TO_INT(GOB_Y(g))
#define GOB_W_INT(g)    ROUND_TO_INT(GOB_W(g))
#define GOB_H_INT(g)    ROUND_TO_INT(GOB_H(g))

#define GOB_LOG_X_INT(g)    ROUND_TO_INT(GOB_LOG_X(g))
#define GOB_LOG_Y_INT(g)    ROUND_TO_INT(GOB_LOG_Y(g))
#define GOB_LOG_W_INT(g)    ROUND_TO_INT(GOB_LOG_W(g))
#define GOB_LOG_H_INT(g)    ROUND_TO_INT(GOB_LOG_H(g))

#define GOB_XO(g)       VAL_XYF_X(ARR_AT((g), IDX_OLD_OFFSET))
#define GOB_YO(g)       VAL_XYF_Y(ARR_AT((g), IDX_OLD_OFFSET))
#define GOB_WO(g)       VAL_XYF_X(ARR_AT((g), IDX_OLD_SIZE))
#define GOB_HO(g)       VAL_XYF_Y(ARR_AT((g), IDX_OLD_SIZE))

#define GOB_XO_INT(g)   ROUND_TO_INT(GOB_XO(g))
#define GOB_YO_INT(g)   ROUND_TO_INT(GOB_YO(g))
#define GOB_WO_INT(g)   ROUND_TO_INT(GOB_WO(g))
#define GOB_HO_INT(g)   ROUND_TO_INT(GOB_HO(g))

#define GOB_FLAGS(g) \
    EXTRA(Custom, ARR_AT((g), IDX_GOB_OFFSET_AND_FLAGS)).u

#define SET_GOB_FLAG(g,f)       cast(void, GOB_FLAGS(g) |= (f))
#define GET_GOB_FLAG(g,f)       (did (GOB_FLAGS(g) & (f)))
#define CLR_GOB_FLAG(g,f)       cast(void, GOB_FLAGS(g) &= ~(f))

#define GOB_ALPHA(g) \
    EXTRA(Bytes, ARR_AT((g), IDX_GOB_SIZE_AND_ALPHA)).common[0]

#define GOB_CONTENT(g)              KNOWN(ARR_AT((g), IDX_GOB_CONTENT))
#define mutable_GOB_CONTENT(g)      ARR_AT((g), IDX_GOB_CONTENT)

#define GOB_TYPE(g) \
    EXTRA(Bytes, ARR_AT(g, IDX_GOB_TYPE_AND_OLD_SIZE)).common[0]

#define SET_GOB_TYPE(g,t)       (GOB_TYPE(g) = (t))

#define GOB_DATA(g)             KNOWN(ARR_AT((g), IDX_GOB_DATA))
#define mutable_GOB_DATA(g)     ARR_AT((g), IDX_GOB_DATA)
#define GOB_DTYPE(g)            VAL_TYPE(GOB_DATA(g))

#define IS_GOB_OPAQUE(g)        GET_GOB_FLAG((g), GOBF_OPAQUE)
#define SET_GOB_OPAQUE(g)       SET_GOB_FLAG((g), GOBF_OPAQUE)
#define CLR_GOB_OPAQUE(g)       CLR_GOB_FLAG((g), GOBF_OPAQUE)

#define GOB_PANE_VALUE(g)       ARR_AT((g), IDX_GOB_PANE)

inline static REBARR *GOB_PANE(REBGOB *g) {
    RELVAL *v = GOB_PANE_VALUE(g);
    if (IS_BLANK(v))
        return nullptr;

    assert(IS_BLOCK(v)); // only other legal thing that can be in pane cell
    assert(VAL_INDEX(v) == 0); // pane array shouldn't have an index
    return VAL_ARRAY(v);
}

#define GOB_PARENT(g)       LINK(g).parent
#define GOB_TMP_OWNER(g)    MISC(g).owner  // !!! What's this TMP_XXX thing?

#define GOB_STRING(g)       SER_HEAD(GOB_CONTENT(g))
#define GOB_LEN(g)          ARR_LEN(GOB_PANE(g))
#define SET_GOB_LEN(g,l)    TERM_ARRAY_LEN(GOB_PANE(g), (l))
#define GOB_HEAD(g)         KNOWN(ARR_HEAD(GOB_PANE(g)))

#define GOB_BITMAP(g)   GOB_STRING(g)
#define GOB_AT(g,n)   (GOB_HEAD(g)+n)

#define IS_WINDOW(g) \
    (GOB_PARENT(g) == Gob_Root && GET_GOB_FLAG(g, GOBF_WINDOW))

#define IS_GOB_COLOR(g)  (GOB_TYPE(g) == GOBT_COLOR)
#define IS_GOB_DRAW(g)   (GOB_TYPE(g) == GOBT_DRAW)
#define IS_GOB_IMAGE(g)  (GOB_TYPE(g) == GOBT_IMAGE)
#define IS_GOB_EFFECT(g) (GOB_TYPE(g) == GOBT_EFFECT)
#define IS_GOB_STRING(g) (GOB_TYPE(g) == GOBT_STRING)
#define IS_GOB_TEXT(g)   (GOB_TYPE(g) == GOBT_TEXT)

extern REBGOB *Gob_Root; // Top level GOB (the screen)


#if defined(NDEBUG) || !defined(CPLUSPLUS_11)
    #define VAL_GOB(v) \
        cast(REBGOB*, PAYLOAD(Custom, (v)).first.p) // use w/a const REBVAL*

    #define mutable_VAL_GOB(v) \
        (*cast(REBGOB**, &PAYLOAD(Custom, (v)).first.p)) // non-const REBVAL*

    #define VAL_GOB_INDEX(v) \
        PAYLOAD(Custom, v).second.u
#else
    inline static REBGOB* VAL_GOB(const REBCEL *v) {
        assert(CELL_KIND(v) == REB_GOB);
        return cast(REBGOB*, PAYLOAD(Custom, v).first.p);
    }

    inline static REBGOB* &mutable_VAL_GOB(REBCEL *v) {
        assert(CELL_KIND(v) == REB_GOB);
        return *cast(REBGOB**, &PAYLOAD(Custom, v).first.p);
    }

    inline static uintptr_t const &VAL_GOB_INDEX(const REBCEL *v) {
        assert(CELL_KIND(v) == REB_GOB);
        return PAYLOAD(Custom, v).second.u;
    }

    inline static uintptr_t &VAL_GOB_INDEX(REBCEL *v) {
        assert(CELL_KIND(v) == REB_GOB);
        return PAYLOAD(Custom, v).second.u;
    }
#endif

inline static REBVAL *Init_Gob(RELVAL *out, REBGOB *g) {
    assert(GET_SERIES_FLAG(g, MANAGED));

    RESET_CELL(out, REB_GOB);
    mutable_VAL_GOB(out) = g;
    VAL_GOB_INDEX(out) = 0;
    return KNOWN(out);
}
