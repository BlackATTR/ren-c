//
//  File: %mod-lodepng.c
//  Summary: "Native Functions for cryptography"
//  Section: Extension
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
// R3-Alpha had some PNG decoding in a file called %u-png.c.  That decoder
// appeared to be original code from Rebol Technologies, as there are no
// comments saying otherwise.  Saphirion apparently hit bugs in the encoding
// that file implemented, but rather than try and figure out how to fix it
// they just included LodePNG--and adapted it for use in encoding only:
//
// http://lodev.org/lodepng/
//
// LodePNG is an encoder/decoder that is also a single source file and header
// file...but has some community of users and receives bugfixes.  So for
// simplicity, Ren-C went ahead and removed %u-png.c to use LodePNG for
// decoding and PNG file identification as well.
//
// Note: LodePNG is known to be slower than the more heavyweight "libpng"
// library, and does not support the progressive/streaming decoding used by
// web browsers.  For this reason, the extension is called "lodepng", to make
// room for more sophisticated PNG decoders in the future.
//

#include "lodepng.h"

#include "sys-core.h"
#include "sys-ext.h"

#include "tmp-mod-lodepng-first.h"


//=//// CUSTOM SERIES-BACKED MEMORY ALLOCATOR /////////////////////////////=//
//
// LodePNG allows for a custom allocator, but it assumes the pointer it is
// given is where it will write data...so it can't be given something like
// a REBSER* which needs BIN_HEAD() or BIN_AT() to dereference it.  But we
// would like to avoid the busywork of copying data between malloc()'d buffers
// and REBSER data which can be given into userspace.
//
// A trick is used here where a series is allocated that is slightly larger
// than the requested data size...just large enough to put a pointer to the
// series itself at the head of the memory.  Then, the memory right after
// that pointer is given back to LodePNG.  The series pointer can then be
// found again by subtracting sizeof(REBSER*) from the client pointer.
//
// lodepng contains prototypes for these functions, and expects them to
// be defined somewhere if you #define LODEPNG_NO_COMPILE_ALLOCATORS
// (this is specified in the extension compiler flag settings)
//
//=////////////////////////////////////////////////////////////////////////=//

void* lodepng_malloc(size_t size)
{
    return rebMalloc(size);
}

void* lodepng_realloc(void* ptr, size_t new_size)
{
    return rebRealloc(ptr, new_size);
}

void lodepng_free(void* ptr)
{
    rebFree(ptr);
}


//=//// HOOKS TO REUSE REBOL'S ZLIB ///////////////////////////////////////=//
//
// By default, LodePNG will build its own copy of zlib functions for compress
// and decompress.  However, Rebol already has zlib built in.  So we ask
// LodePNG not to compile its own copy, and pass function pointers to do
// the compression and decompression in via the LodePNGState.
//
// Hence when lodepng.c is compiled, we must #define LODEPNG_NO_COMPILE_ZLIB
//
//=////////////////////////////////////////////////////////////////////////=//

static unsigned rebol_zlib_decompress(
    unsigned char** out,
    size_t* outsize,
    const unsigned char* in,
    size_t insize,
    const LodePNGDecompressSettings* settings
){
    // as far as I can tell, the logic of LodePNG is to preallocate a buffer
    // and so out and outsize are already set up.  This is due to some
    // knowledge it has about the scanlines.  But it's passed in as "out"
    // pointer parameters in case you update it (?)
    //
    // Rebol's decompression was not written for the caller to provide
    // a buffer, though COMPRESS/INTO or DECOMPRESS/INTO would be useful.
    // So consider it.  But for now, free the buffer and let the logic of
    // zlib always make its own.
    //
    rebFree(*out);

    assert(5 == *cast(const int*, settings->custom_context)); // just testing
    UNUSED(settings->custom_context);

    // PNG uses "zlib envelope" w/ADLER32 checksum, hence "Zinflate"
    //
    const REBINT max = -1; // size unknown, inflation will need to guess
    REBCNT out_len;
    *out = rebZinflateAlloc(&out_len, in, insize, max);
    *outsize = out_len;

    return 0;
}

static unsigned rebol_zlib_compress(
    unsigned char** out,
    size_t* outsize,
    const unsigned char* in,
    size_t insize,
    const LodePNGCompressSettings* settings
){
    lodepng_free(*out); // see remarks in decompress, and about COMPRESS/INTO

    assert(5 == *cast(const int*, settings->custom_context)); // just testing
    UNUSED(settings->custom_context);

    // PNG uses "zlib envelope" w/ADLER32 checksum, hence "Zdeflate"
    //
    REBCNT out_len;
    *out = rebZdeflateAlloc(&out_len, in, insize);
    *outsize = out_len;

    return 0;
}


//
//  identify-png?: native [
//
//  {Codec for identifying BINARY! data for a PNG}
//
//      return: [logic!]
//      data [binary!]
//  ]
//
REBNATIVE(identify_png_q)
{
    LODEPNG_INCLUDE_PARAMS_OF_IDENTIFY_PNG_Q;

    LodePNGState state;
    lodepng_state_init(&state);

    // use the zlib already built into Rebol for DECOMPRESS, inflate()
    //
    state.decoder.zlibsettings.custom_zlib = rebol_zlib_decompress;

    // this is how to pass an arbitrary void* that custom zlib can access
    // (so one could put decompression settings or state in there)
    //
    int arg = 5;
    state.decoder.zlibsettings.custom_context = &arg;

    unsigned width;
    unsigned height;
    unsigned error = lodepng_inspect(
        &width,
        &height,
        &state,
        VAL_BIN_AT(ARG(data)), // PNG data
        VAL_LEN_AT(ARG(data)) // PNG data length
    );

    // state contains extra information about the PNG such as text chunks
    //
    lodepng_state_cleanup(&state);

    if (error != 0)
        return R_FALSE;

    // !!! Should codec identifiers return any optional information they just
    // happen to get?  Instead of passing NULL for the addresses of the width
    // and the height, this could incidentally get that information back
    // to return it.  Then any non-FALSE result could be "identified" while
    // still being potentially more informative about what was found out.
    //
    return R_TRUE;
}


//
//  decode-png: native [
//
//  {Codec for decoding BINARY! data for a PNG}
//
//      return: [image!]
//      data [binary!]
//  ]
//
REBNATIVE(decode_png)
{
    LODEPNG_INCLUDE_PARAMS_OF_DECODE_PNG;

    LodePNGState state;
    lodepng_state_init(&state);

    // use the zlib already built into Rebol for DECOMPRESS, inflate()
    //
    state.decoder.zlibsettings.custom_zlib = rebol_zlib_decompress;

    // this is how to pass an arbitrary void* that custom zlib can access
    // (so one could put decompression settings or state in there)
    //
    int arg = 5;
    state.decoder.zlibsettings.custom_context = &arg;

    // Even if the input PNG doesn't have alpha or color, ask for conversion
    // to RGBA.
    //
    state.decoder.color_convert = 1;
    state.info_png.color.colortype = LCT_RGBA;
    state.info_png.color.bitdepth = 8;

    unsigned char* image_bytes;
    unsigned width;
    unsigned height;
    unsigned error = lodepng_decode(
        &image_bytes,
        &width,
        &height,
        &state,
        VAL_BIN_AT(ARG(data)), // PNG data
        VAL_LEN_AT(ARG(data)) // PNG data length
    );

    // `state` can contain potentially interesting information, such as
    // metadata (key="Software" value="REBOL", for instance).  Currently this
    // is just thrown away, but it might be interesting to have access to.
    // Because Rebol_Malloc() was used to make the strings, they could easily
    // be Rebserize()'d and put in an object.
    //
    lodepng_state_cleanup(&state);

    if (error != 0)
        fail (lodepng_error_text(error));

    // Note LodePNG cannot decode into an existing buffer, though it has been
    // requested:
    //
    // https://github.com/lvandeve/lodepng/issues/17
    //
    // !!! We don't currently rebRepossess() the data as a BINARY! to use
    // directly for the image data for two reasons.  The series backing
    // Make_Image() needs to be wide=sizeof(u32), and fiddling it would take
    // to get that to work is not clearly better than having IMAGE! use a
    // byte-sized series.
    //
    // The other reason is that is seems the pixel format used by LodePNG may
    // not be the same as Rebol's format, so the data has to be rewritten.
    // Review both points, as for large images you don't want to make a copy.
    // If IMAGE! were a user-defined / extension type, it would make sense
    // for it to be built on top of BINARY! (and a PAIR! for size...)
    //
    // !!! It may be worth it to tweak LodePNG with new output formats
    // besides LCT_RGBA, if it's truly necessary to use a different internal
    // format than what it gives back.
    //
    REBSER *image = Make_Image(width, height, TRUE);
    unsigned char *src = image_bytes;
    REBYTE *dest = SER_DATA_RAW(image);
    REBCNT index;
    for (index = 0; index < width * height; ++index) {
        dest[C_R] = src[0];
        dest[C_G] = src[1];
        dest[C_B] = src[2];
        dest[C_A] = src[3];
        dest += 4;
        src += 4;
    }
    rebFree(image_bytes); // !!! would have been nicer to rebRepossess()

    Init_Image(D_OUT, image);

    return R_OUT;
}


//
//  encode-png: native [
//
//  {Codec for encoding a PNG image}
//
//      return: [binary!]
//      image [image!]
// ]
//
REBNATIVE(encode_png)
{
    LODEPNG_INCLUDE_PARAMS_OF_ENCODE_PNG;

    REBVAL *image = ARG(image);

    // Historically, Rebol would write (key="Software" value="REBOL") into
    // image metadata.  Is that interesting?  If so, the state has fields for
    // this...assuming the encoder pays attention to them (the decoder does).
    //
    LodePNGState state;
    lodepng_state_init(&state);

    // use the zlib already built into Rebol for DECOMPRESS, deflate()
    //
    state.encoder.zlibsettings.custom_zlib = rebol_zlib_compress;

    // this is how to pass an arbitrary void* that custom zlib can access
    // (so one could put dompression settings or state in there)
    //
    int arg = 5;
    state.encoder.zlibsettings.custom_context = &arg;

    // input format
    //
    state.info_raw.colortype = LCT_RGBA;
    state.info_raw.bitdepth = 8;

    // output format - could support more options, like LCT_RGB to avoid
    // writing transparency, or grayscale, etc.
    //
    state.info_png.color.colortype = LCT_RGBA;
    state.info_png.color.bitdepth = 8;

    // !!! "disable autopilot" (what is the significance of this?  it might
    // have to be 1 if using an output format different from the input...)
    //
    state.encoder.auto_convert = 0;

    REBCNT width = VAL_IMAGE_WIDE(image);
    REBCNT height = VAL_IMAGE_HIGH(image);

    // !!! Rebol's internal byte ordering for images seems to vary according
    // to platform.  This seems like a pretty bad idea vs. using a standard
    // byte ordering (byte-based access need not worry about endianness).
    // Ideally Rebol would use a format compatible with LodePNG's RGBA order,
    // but if not then it LodePNG should be patched to write the alternate
    // format as an alternative to LCT_RGBA, to avoid this copy.
    //
    REBYTE *image_bytes = SER_DATA_RAW(VAL_SERIES(image));

    REBOOL check = TRUE; // avoid "conditional expression is constant" warning
    REBYTE *reordered;
    if (check && C_R == 0 && C_G == 1 && C_B == 2 && C_A == 3)
        reordered = NULL;
    else {
        reordered = cast(REBYTE*, rebMalloc(width * height * 4));

        REBYTE *src = image_bytes;
        REBYTE *dest = reordered;
        REBCNT index;
        for (index = 0; index < width * height; ++index) {
            dest[0] = src[C_R];
            dest[1] = src[C_G];
            dest[2] = src[C_B];
            dest[3] = src[C_A];
            src += 4;
            dest += 4;
        }
    }

    size_t encoded_size;
    REBYTE *encoded_bytes = NULL;
    unsigned error = lodepng_encode(
        &encoded_bytes,
        &encoded_size,
        reordered != NULL ? reordered : image_bytes,
        width,
        height,
        &state
    );

    lodepng_state_cleanup(&state);

    if (reordered != NULL)
        rebFree(reordered); // !!! Wasteful if we had to make this...

    if (error != 0)
        fail (lodepng_error_text(error));

    // Because LodePNG was hooked with a custom zlib_malloc, it built upon
    // rebMalloc()...which backs its allocations with a series.  This means
    // the encoded buffer can be taken back as a BINARY! without making a
    // new series, see rebMalloc()/rebRepossess() for details.
    //
    REBVAL *binary = rebRepossess(encoded_bytes, encoded_size);
    Move_Value(D_OUT, binary);
    rebRelease(binary);

    return R_OUT;
}


#include "tmp-mod-lodepng-last.h"
