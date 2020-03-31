//
//  File: %mod-crypt.c
//  Summary: "Native Functions for Cryptography"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2020 Rebol Open Source Contributors
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
//=////////////////////////////////////////////////////////////////////////=//
//
// R3-Alpha originally had a few hand-picked routines for hashing picked from
// OpenSSL.  Saphirion added support for the AES streaming cipher and Diffie
// Hellman keys in order to do Transport Layer Security (TLS, e.g. the "S" in
// for "Secure" in HTTPS).  But cryptography represents something of a moving
// target; and in the interest of being relatively lightweight a pragmatic
// set of "current" crypto is included by default.
//

#include "rsa/rsa.h" // defines gCryptProv and rng_fd (used in Init/Shutdown)
#include "dh/dh.h"
#include "aes/aes.h"

// The "Easy ECC" supports four elliptic curves, but is only set up to do one
// of them at a time you pick with a #define.  We pick secp256r1, in part
// because Discourse supports it on the Rebol forum.
//
#define ECC_CURVE secp256r1
#include "easy-ecc/ecc.h"

// %bigint_impl.h defines min and max, which triggers warnings in clang about
// C++ compatibility even if building as C...due to some header file that
// sys-core.h includes.
//
#undef min
#undef max

#ifdef IS_ERROR
    #undef IS_ERROR  // winerror.h defines, so undef it to avoid the warning
#endif
#include "sys-core.h"

#include "mbedtls/sha256.h"
#include "mbedtls/arc4.h"  // RC4 is technically trademarked, so it's "ARC4"

#include "sys-zlib.h"  // needed for the ADLER32 hash

#include "tmp-mod-crypt.h"


#include "md5/u-md5.h"  // exposed via `CHECKSUM 'MD5` (see notes)
#include "sha1/u-sha1.h"  // exposed via `CHECKSUM 'SHA1` (see notes)


// Most routines in mbedTLS return either `void` or an `int` code which is
// 0 on success and negative numbers on error.  This macro helps generalize
// the pattern of trying to build a result and having a cleanup (similar
// ones exist inside mbedTLS itself, e.g. MBEDTLS_MPI_CHK() in %bignum.h)
//
// !!! We probably do not need to have non-debug builds use up memory by
// integrating the string table translating all those negative numbers into
// specific errors.  But a debug build might want to.  For now, one error.
//
#define IF_NOT_0(label,error,call) \
    do { \
        assert(error == nullptr); \
        int mbedtls_ret = (call);  /* don't use (call) more than once! */ \
        if (mbedtls_ret != 0) { \
            error = rebValue("make error! {mbedTLS error}", rebEND); \
            goto label; \
        } \
    } while (0)


//=//// RANDOM NUMBER GENERATION //////////////////////////////////////////=//
//
// The generation of "random enough numbers" is a deep topic in cryptography.
// mbedTLS doesn't build in a random generator and allows you to pick one that
// is "as random as you feel you need" and can take advantage of any special
// "entropy sources" you have access to (e.g. the user waving a mouse around
// while the numbers are generated).  The prototype of the generator is:
//
//     int (*f_rng)(void *p_rng, unsigned char *output, size_t len);
//
// Each function that takes a random number generator also takes a pointer
// you can tunnel through (the first parameter), if it has some non-global
// state it needs to use.
//
// mbedTLS offers %ctr_drbg.h and %ctr_drbg.c for standardized functions which
// implement a "Counter mode Deterministic Random Byte Generator":
//
// https://tls.mbed.org/kb/how-to/add-a-random-generator
//
// !!! Currently we just use the code from Saphirion, given that TLS is not
// even checking the certificates it gets.
//

// Initialized by the CRYPT extension entry point, shut down by the exit code
//
#ifdef TO_WINDOWS
    HCRYPTPROV gCryptProv = 0;
#else
    int rng_fd = -1;
#endif

int get_random(void *p_rng, unsigned char *output, size_t output_len)
{
    assert(p_rng == nullptr);  // parameter currently not used
    UNUSED(p_rng);

  #ifdef TO_WINDOWS
    if (CryptGenRandom(gCryptProv, output_len, output) != 0)
        return 0;  // success
  #else
    if (rng_fd != -1 && read(rng_fd, output, output_len) != -1)
        return 0;  // success
  #endif

  rebJumps ("fail {Random number generation did not succeed}");
}



//=//// CHECKSUM "EXTENSIBLE WITH PLUG-INS" NATIVE ////////////////////////=//
//
// Rather than pollute the namespace with functions that had every name of
// every algorithm (`sha256 my-data`, `md5 my-data`) Rebol had a CHECKSUM
// that effectively namespaced it (e.g. `checksum/method my-data 'sha256`).
// This suffered from somewhat the same problem as ENCODE and DECODE in that
// parameterization was not sorted out; instead leading to a hodgepodge of
// refinements that may or may not apply to each algorithm.
//
// Additionally: the idea that there is some default CHECKSUM the language
// would endorse for all time when no /METHOD is given is suspect.  It may
// be that a transient "only good for this run" sum (which wouldn't serialize)
// could be repurposed for this use.
//
// !!! For now, the CHECKSUM function is left as-is for MD5 and SHA1, but
// the idea should be reviewed for its merits.
//

// Table of has functions and parameters:
static struct {
    REBYTE *(*digest)(const REBYTE *, REBLEN, REBYTE *);
    void (*init)(void *);
    void (*update)(void *, const REBYTE *, REBLEN);
    void (*final)(REBYTE *, void *);
    int (*ctxsize)(void);
    REBSYM sym;
    REBLEN len;
    REBLEN hmacblock;
} digests[] = {

    {SHA1, SHA1_Init, SHA1_Update, SHA1_Final, SHA1_CtxSize, SYM_SHA1, 20, 64},
    {MD5, MD5_Init, MD5_Update, MD5_Final, MD5_CtxSize, SYM_MD5, 16, 64},

    {nullptr, nullptr, nullptr, nullptr, nullptr, SYM_0, 0, 0}
};

//
//  export checksum: native [
//
//  "Computes a checksum, CRC, or hash."
//
//      data [binary!]
//      /part "Length of data"
//          [any-value!]
//      /tcp "Returns an Internet TCP 16-bit checksum"
//      /secure "Returns a cryptographically secure checksum"
//      /hash "Returns a hash value with given size"
//          [integer!]
//      /method "Method to use [SHA1 MD5] (see also CRC32 native)"
//          [word!]
//      /key "Returns keyed HMAC value"
//          [binary! text!]
//  ]
//
REBNATIVE(checksum)
{
    CRYPT_INCLUDE_PARAMS_OF_CHECKSUM;

    REBLEN len = Part_Len_May_Modify_Index(ARG(data), ARG(part));
    REBYTE *data = VAL_RAW_DATA_AT(ARG(data));  // after Part_Len, may change

    REBSYM sym;
    if (REF(method)) {
        sym = VAL_WORD_SYM(ARG(method));
        if (sym == SYM_0)  // not in %words.r, no SYM_XXX constant
            fail (PAR(method));
    }
    else
        sym = SYM_SHA1;

    // If method, secure, or key... find matching digest:
    if (REF(method) || REF(secure) || REF(key)) {
        if (sym == SYM_CRC32) {
            if (REF(secure) || REF(key))
                fail (Error_Bad_Refines_Raw());

            // CRC32 is typically an unsigned 32-bit number and uses the full
            // range of values.  Yet Rebol chose to export this as a signed
            // integer via CHECKSUM.  Perhaps (?) to generate a value that
            // could be used by Rebol2, as it only had 32-bit signed INTEGER!.
            //
            REBINT crc32 = cast(int32_t, crc32_z(0L, data, len));
            return Init_Integer(D_OUT, crc32);
        }

        if (sym == SYM_ADLER32) {
            if (REF(secure) || REF(key))
                fail (Error_Bad_Refines_Raw());

            // adler32() is a Saphirion addition since 64-bit INTEGER! was
            // available in Rebol3, and did not convert the unsigned result
            // of the adler calculation to a signed integer.
            //
            uLong adler = z_adler32(0L, data, len);
            return Init_Integer(D_OUT, adler);
        }

        REBLEN i;
        for (i = 0; i != sizeof(digests) / sizeof(digests[0]); i++) {
            if (!SAME_SYM_NONZERO(digests[i].sym, sym))
                continue;

            REBSER *digest = Make_Series(digests[i].len + 1, sizeof(char));

            if (not REF(key))
                digests[i].digest(data, len, BIN_HEAD(digest));
            else {
                REBLEN blocklen = digests[i].hmacblock;

                REBYTE tmpdigest[20]; // size must be max of all digest[].len

                REBSIZ key_size;
                const REBYTE *key_bytes = VAL_BYTES_AT(&key_size, ARG(key));

                if (key_size > blocklen) {
                    digests[i].digest(key_bytes, key_size, tmpdigest);
                    key_bytes = tmpdigest;
                    key_size = digests[i].len;
                }

                REBYTE ipad[64]; // size must be max of all digest[].hmacblock
                memset(ipad, 0, blocklen);
                memcpy(ipad, key_bytes, key_size);

                REBYTE opad[64]; // size must be max of all digest[].hmacblock
                memset(opad, 0, blocklen);
                memcpy(opad, key_bytes, key_size);

                REBLEN j;
                for (j = 0; j < blocklen; j++) {
                    ipad[j] ^= 0x36; // !!! why do people write this kind of
                    opad[j] ^= 0x5c; // thing without a comment? !!! :-(
                }

                char *ctx = ALLOC_N(char, digests[i].ctxsize());
                digests[i].init(ctx);
                digests[i].update(ctx,ipad,blocklen);
                digests[i].update(ctx, data, len);
                digests[i].final(tmpdigest,ctx);
                digests[i].init(ctx);
                digests[i].update(ctx,opad,blocklen);
                digests[i].update(ctx,tmpdigest,digests[i].len);
                digests[i].final(BIN_HEAD(digest),ctx);

                FREE_N(char, digests[i].ctxsize(), ctx);
            }

            TERM_BIN_LEN(digest, digests[i].len);
            return Init_Binary(D_OUT, digest);
        }

        fail (PAR(method));
    }
    else if (REF(tcp)) {
        REBINT ipc = Compute_IPC(data, len);
        Init_Integer(D_OUT, ipc);
    }
    else if (REF(hash)) {
        REBINT sum = VAL_INT32(ARG(hash));
        if (sum <= 1)
            sum = 1;

        REBINT hash = Hash_Bytes(data, len) % sum;
        Init_Integer(D_OUT, hash);
    }
    else
        Init_Integer(D_OUT, Compute_CRC24(data, len));

    return D_OUT;
}


//=//// INDIVIDUAL CRYPTO NATIVES /////////////////////////////////////////=//
//
// These natives are the hodgepodge of choices that implemented "enough TLS"
// to let Rebol communicate with HTTPS sites.  The first ones originated
// from Saphirion's %host-core.c:
//
// https://github.com/zsx/r3/blob/atronix/src/os/host-core.c
//
// !!! The effort to improve these has been ongoing and gradual.  Current
// focus is on building on the shared/vetted/maintained architecture of
// mbedTLS, instead of the mix of standalone clips from the Internet and some
// custom code from Saphirion.  But eventually this should aim to make
// inclusion of each crypto a separate extension for more modularity.
//


static void cleanup_rc4_ctx(const REBVAL *v)
{
    struct mbedtls_arc4_context *ctx
        = VAL_HANDLE_POINTER(struct mbedtls_arc4_context, v);
    mbedtls_arc4_free(ctx);
    FREE(struct mbedtls_arc4_context, ctx);
}


//
//  export rc4-key: native [
//
//  "Encrypt/decrypt data (modifies) using RC4 algorithm."
//
//      return: [handle!]
//      key [binary!]
//  ]
//
REBNATIVE(rc4_key)
//
// !!! RC4 was originally included for use with TLS.  However, the insecurity
// of RC4 led the IETF to prohibit RC4 for TLS use in 2015:
//
// https://tools.ietf.org/html/rfc7465
//
// So it is not in use at the moment.  It isn't much code, but could probably
// be moved to its own extension so it could be selected to build in or not,
// which is how cryptography methods should probably be done.
{
    CRYPT_INCLUDE_PARAMS_OF_RC4_KEY;

    struct mbedtls_arc4_context *ctx = ALLOC(struct mbedtls_arc4_context);
    mbedtls_arc4_init(ctx);
    mbedtls_arc4_setup(ctx, VAL_BIN_AT(ARG(key)), VAL_LEN_AT(ARG(key)));

    return Init_Handle_Cdata_Managed(
        D_OUT,
        ctx,
        sizeof(struct mbedtls_arc4_context),
        &cleanup_rc4_ctx
    );
}


//
//  export rc4-stream: native [
//
//  "Encrypt/decrypt data (modifies) using RC4 algorithm."
//
//      return: <void>
//      ctx "Stream cipher context"
//          [handle!]
//      data "Data to encrypt/decrypt (modified)"
//          [binary!]
//  ]
//
REBNATIVE(rc4_stream)
{
    CRYPT_INCLUDE_PARAMS_OF_RC4_STREAM;

    REBVAL *data = ARG(data);

    if (VAL_HANDLE_CLEANER(ARG(ctx)) != cleanup_rc4_ctx)
        rebJumps ("fail [{Not a RC4 Context:}", ARG(ctx), "]", rebEND);

    struct mbedtls_arc4_context *ctx
        = VAL_HANDLE_POINTER(struct mbedtls_arc4_context, ARG(ctx));

    REBVAL *error = nullptr;

    IF_NOT_0(cleanup, error, mbedtls_arc4_crypt(
        ctx,
        VAL_LEN_AT(data),
        VAL_BIN_AT(data),  // input "message"
        VAL_BIN_AT(data)  // output (same, since it modifies)
    ));

  cleanup:
     if (error)
        rebJumps ("fail", error, rebEND);

    return rebVoid();
}


//
//  export rsa: native [
//
//  "Encrypt/decrypt data using the RSA algorithm."
//
//      data [binary!]
//      key-object [object!]
//      /decrypt "Decrypts the data (default is to encrypt)"
//      /private "Uses an RSA private key (default is a public key)"
//  ]
//
REBNATIVE(rsa)
{
    CRYPT_INCLUDE_PARAMS_OF_RSA;

    REBVAL *obj = ARG(key_object);

    // N and E are required
    //
    REBVAL *n = rebValue("ensure binary! pick", obj, "'n", rebEND);
    REBVAL *e = rebValue("ensure binary! pick", obj, "'e", rebEND);

    RSA_CTX *rsa_ctx = NULL;

    REBINT binary_len;
    if (REF(private)) {
        REBVAL *d = rebValue("ensure binary! pick", obj, "'d", rebEND);

        if (not d)
            fail ("No d returned BLANK, can we assume error for cleanup?");

        REBVAL *p = rebValue("ensure binary! pick", obj, "'p", rebEND);
        REBVAL *q = rebValue("ensure binary! pick", obj, "'q", rebEND);
        REBVAL *dp = rebValue("ensure binary! pick", obj, "'dp", rebEND);
        REBVAL *dq = rebValue("ensure binary! pick", obj, "'dq", rebEND);
        REBVAL *qinv = rebValue("ensure binary! pick", obj, "'qinv", rebEND);

        // !!! Because BINARY! is not locked in memory or safe from GC, the
        // libRebol API doesn't allow direct pointer access.  Use the
        // internal VAL_BIN_AT for now, but consider if a temporary locking
        // should be possible...locked until released.
        //
        binary_len = rebUnbox("length of", d, rebEND);
        RSA_priv_key_new(
            &rsa_ctx
            ,
            VAL_BIN_AT(n)
            , rebUnbox("length of", n, rebEND)
            ,
            VAL_BIN_AT(e)
            , rebUnbox("length of", e, rebEND)
            ,
            VAL_BIN_AT(d)
            , binary_len // taken as `length of d` above
            ,
            p ? VAL_BIN_AT(p) : NULL
            , p ? rebUnbox("length of", p, rebEND) : 0
            ,
            q ? VAL_BIN_AT(q) : NULL
            , q ? rebUnbox("length of", q, rebEND) : 0
            ,
            dp ? VAL_BIN_AT(dp) : NULL
            , dp ? rebUnbox("length of", dp, rebEND) : 0
            ,
            dq ? VAL_BIN_AT(dq) : NULL
            , dp ? rebUnbox("length of", dq, rebEND) : 0
            ,
            qinv ? VAL_BIN_AT(qinv) : NULL
            , qinv ? rebUnbox("length of", qinv, rebEND) : 0
        );

        rebRelease(d);
        rebRelease(p);
        rebRelease(q);
        rebRelease(dp);
        rebRelease(dq);
        rebRelease(qinv);
    }
    else {
        binary_len = rebUnbox("length of", n, rebEND);
        RSA_pub_key_new(
            &rsa_ctx
            ,
            VAL_BIN_AT(n)
            , binary_len // taken as `length of n` above
            ,
            VAL_BIN_AT(e)
            , rebUnbox("length of", e, rebEND)
        );
    }

    rebRelease(n);
    rebRelease(e);

    // !!! See notes above about direct binary access via libRebol
    //
    REBYTE *dataBuffer = VAL_BIN_AT(ARG(data));
    REBINT data_len = rebUnbox("length of", ARG(data), rebEND);

    BI_CTX *bi_ctx = rsa_ctx->bi_ctx;
    bigint *data_bi = bi_import(bi_ctx, dataBuffer, data_len);

    // Buffer suitable for recapturing as a BINARY! for either the encrypted
    // or decrypted data
    //
    REBYTE *crypted = rebAllocN(REBYTE, binary_len);

    if (REF(decrypt)) {
        int result = RSA_decrypt(
            rsa_ctx,
            dataBuffer,
            crypted,
            binary_len,
            REF(private) ? 1 : 0
        );

        if (result == -1) {
            bi_free(rsa_ctx->bi_ctx, data_bi);
            RSA_free(rsa_ctx);

            rebFree(crypted); // would free automatically due to failure...
            rebJumps(
                "fail [{Failed to decrypt:}", ARG(data), "]", rebEND
            );
        }

        assert(result == binary_len); // was this true?
    }
    else {
        int result = RSA_encrypt(
            rsa_ctx,
            dataBuffer,
            data_len,
            crypted,
            REF(private) ? 1 : 0
        );

        if (result == -1) {
            bi_free(rsa_ctx->bi_ctx, data_bi);
            RSA_free(rsa_ctx);

            rebFree(crypted); // would free automatically due to failure...
            rebJumps(
                "fail [{Failed to encrypt:}", ARG(data), "]", rebEND
            );
        }

        // !!! any invariant here?
    }

    bi_free(rsa_ctx->bi_ctx, data_bi);
    RSA_free(rsa_ctx);

    return rebRepossess(crypted, binary_len);
}


//
//  export dh-generate-keypair: native [
//
//  "Generate a new Diffie-Hellman private/public key pair"
//
//      return: "Diffie-Hellman object, with P, PRIVATE, and PUBLIC members"
//          [object!]
//      g "generator"
//          [binary!]
//      p "modulus (saved in the object)"
//          [binary!]
//  ]
//
REBNATIVE(dh_generate_keypair)
//
// !!! A comment in the original Saphirion code said "NOT YET IMPLEMENTED" on:
//
//     /generate
//         size [integer!] "Key length"
//         generator [integer!] "Generator number"
{
    CRYPT_INCLUDE_PARAMS_OF_DH_GENERATE_KEYPAIR;

    DH_CTX dh_ctx;
    memset(&dh_ctx, 0, sizeof(dh_ctx));

    dh_ctx.g = VAL_BIN_AT(ARG(g));
    dh_ctx.glen = rebUnbox("length of", ARG(g), rebEND);

    dh_ctx.p = VAL_BIN_AT(ARG(p));
    dh_ctx.len = rebUnbox("length of", ARG(p), rebEND);

    // Generate the private and public keys into memory that can be
    // rebRepossess()'d as the memory backing a BINARY! series
    //
    dh_ctx.x = rebAllocN(REBYTE, dh_ctx.len);  // x => private key
    memset(dh_ctx.x, 0, dh_ctx.len);
    dh_ctx.gx = rebAllocN(REBYTE, dh_ctx.len);  // gx => public key
    memset(dh_ctx.gx, 0, dh_ctx.len);

    DH_generate_key(&dh_ctx);

    return rebValue(
        "make object! [",
            "p:", ARG(p),
            "private:", rebR(rebRepossess(dh_ctx.x, dh_ctx.len)),
            "public:", rebR(rebRepossess(dh_ctx.gx, dh_ctx.len)),
        "]",
    rebEND);
}


//
//  export dh-compute-key: native [
//
//  "Computes key from a private/public key pair and the peer's public key."
//
//      return: "Negotiated key"
//          [binary!]
//      obj "The Diffie-Hellman key object"
//          [object!]
//      public-key "Peer's public key"
//          [binary!]
//  ]
//
REBNATIVE(dh_compute_key)
{
    CRYPT_INCLUDE_PARAMS_OF_DH_COMPUTE_KEY;

    DH_CTX dh_ctx;
    memset(&dh_ctx, 0, sizeof(dh_ctx));

    REBVAL *obj = ARG(obj);

    // !!! used to ensure object only had other fields SELF, PUB-KEY, G
    // otherwise gave Error(RE_EXT_CRYPT_INVALID_KEY_FIELD)

    REBVAL *p = rebValue("ensure binary! pick", obj, "'p", rebEND);
    REBVAL *priv_key = rebValue("ensure binary! pick", obj, "'private", rebEND);

    dh_ctx.p = VAL_BIN_AT(p);
    dh_ctx.len = rebUnbox("length of", p, rebEND);

    dh_ctx.x = VAL_BIN_AT(priv_key);
    // !!! No length check here, should there be?

    dh_ctx.gy = VAL_BIN_AT(ARG(public_key));
    // !!! No length check here, should there be?

    dh_ctx.k = rebAllocN(REBYTE, dh_ctx.len);
    memset(dh_ctx.k, 0, dh_ctx.len);

    DH_compute_key(&dh_ctx);

    rebRelease(p);
    rebRelease(priv_key);

    return rebRepossess(dh_ctx.k, dh_ctx.len);
}


static void cleanup_aes_ctx(const REBVAL *v)
{
    AES_CTX *aes_ctx = VAL_HANDLE_POINTER(AES_CTX, v);
    FREE(AES_CTX, aes_ctx);
}


//
//  export aes-key: native [
//
//  "Encrypt/decrypt data using AES algorithm."
//
//      return: "Stream cipher context handle"
//          [handle!]
//      key [binary!]
//      iv "Optional initialization vector"
//          [binary! blank!]
//      /decrypt "Make cipher context for decryption (default is to encrypt)"
//  ]
//
REBNATIVE(aes_key)
{
    CRYPT_INCLUDE_PARAMS_OF_AES_KEY;

    uint8_t iv[AES_IV_SIZE];

    if (IS_BINARY(ARG(iv))) {
        if (VAL_LEN_AT(ARG(iv)) < AES_IV_SIZE)
            fail ("Length of initialization vector less than AES size");

        memcpy(iv, VAL_BIN_AT(ARG(iv)), AES_IV_SIZE);
    }
    else {
        assert(IS_BLANK(ARG(iv)));
        memset(iv, 0, AES_IV_SIZE);
    }

    REBINT len = VAL_LEN_AT(ARG(key)) << 3;
    if (len != 128 and len != 256) {
        DECLARE_LOCAL (i);
        Init_Integer(i, len);
        rebJumps(
            "fail [{AES key length has to be 16 or 32, not:}", rebI(len), "]",
        rebEND);
    }

    AES_CTX *aes_ctx = ALLOC_ZEROFILL(AES_CTX);

    AES_set_key(
        aes_ctx,
        cast(const uint8_t*, VAL_BIN_AT(ARG(key))),
        cast(const uint8_t*, iv),
        (len == 128) ? AES_MODE_128 : AES_MODE_256
    );

    if (REF(decrypt))
        AES_convert_key(aes_ctx);

    return Init_Handle_Cdata_Managed(
        D_OUT,
        aes_ctx,
        sizeof(AES_CTX),
        &cleanup_aes_ctx
    );
}


//
//  export aes-stream: native [
//
//  "Encrypt/decrypt data using AES algorithm."
//
//      return: "Encrypted/decrypted data (null if zero length)"
//          [<opt> binary!]
//      ctx "Stream cipher context"
//          [handle!]
//      data [binary!]
//  ]
//
REBNATIVE(aes_stream)
{
    CRYPT_INCLUDE_PARAMS_OF_AES_STREAM;

    if (VAL_HANDLE_CLEANER(ARG(ctx)) != cleanup_aes_ctx)
        rebJumps(
            "fail [{Not a AES context:}", ARG(ctx), "]", rebEND
        );

    AES_CTX *aes_ctx = VAL_HANDLE_POINTER(AES_CTX, ARG(ctx));

    REBYTE *dataBuffer = VAL_BIN_AT(ARG(data));
    REBINT len = VAL_LEN_AT(ARG(data));

    if (len == 0)
        return nullptr; // !!! Is NULL a good result for 0 data?

    REBINT pad_len = (((len - 1) >> 4) << 4) + AES_BLOCKSIZE;

    REBYTE *pad_data;
    if (len < pad_len) {
        //
        //  make new data input with zero-padding
        //
        pad_data = rebAllocN(REBYTE, pad_len);
        memset(pad_data, 0, pad_len);
        memcpy(pad_data, dataBuffer, len);
        dataBuffer = pad_data;
    }
    else
        pad_data = nullptr;

    REBYTE *data_out = rebAllocN(REBYTE, pad_len);
    memset(data_out, 0, pad_len);

    if (aes_ctx->key_mode == AES_MODE_DECRYPT)
        AES_cbc_decrypt(
            aes_ctx,
            cast(const uint8_t*, dataBuffer),
            data_out,
            pad_len
        );
    else
        AES_cbc_encrypt(
            aes_ctx,
            cast(const uint8_t*, dataBuffer),
            data_out,
            pad_len
        );

    if (pad_data)
        rebFree(pad_data);

    return rebRepossess(data_out, pad_len);
}


//
//  export sha256: native [
//
//  {Calculate a SHA256 hash value from binary data.}
//
//      return: "32-byte binary hash"
//          [binary!]
//      data "Data to hash, TEXT! will be converted to UTF-8"
//          [binary! text!]
//  ]
//
REBNATIVE(sha256)
{
    CRYPT_INCLUDE_PARAMS_OF_SHA256;

    REBSIZ size;
    const REBYTE *bp = VAL_BYTES_AT(&size, ARG(data));

    struct mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);

    REBVAL *result = nullptr;
    REBVAL *error = nullptr;

    const int is224 = 0;  // could do sha224 if needed...
    IF_NOT_0(cleanup, error, mbedtls_sha256_starts_ret(&ctx, is224));

    IF_NOT_0(cleanup, error, mbedtls_sha256_update_ret(&ctx, bp, size));

  blockscope {
    const size_t sha256_digest_size = 32;

    REBYTE *buf = rebAllocN(REBYTE, sha256_digest_size);  // freed if FAIL
    IF_NOT_0(cleanup, error, mbedtls_sha256_finish_ret(&ctx, buf));

    result = rebRepossess(buf, sha256_digest_size);
  }

  cleanup:
    mbedtls_sha256_free(&ctx);

    if (error)
        rebJumps ("fail", error, rebEND);

    return result;
}


//
//  export ecc-generate-keypair: native [
//      {Generates an uncompressed secp256r1 key}
//
//      return: "object with PUBLIC/X, PUBLIC/Y, and PRIVATE key members"
//          [object!]
//  ]
//
REBNATIVE(ecc_generate_keypair)
{
    CRYPT_INCLUDE_PARAMS_OF_ECC_GENERATE_KEYPAIR;

    // Allocate into memory that can be retaken directly as BINARY! in Rebol
    //
    uint8_t *p_publicX = rebAllocN(uint8_t, ECC_BYTES);
    uint8_t *p_publicY = rebAllocN(uint8_t, ECC_BYTES);
    uint8_t *p_privateKey = rebAllocN(uint8_t, ECC_BYTES);
    if (1 != ecc_make_key_xy(p_publicX, p_publicY, p_privateKey))
        fail ("ecc_make_key_xy() did not return 1");

    return rebValue(
        "make object! [",
            "public: make object! [",
                "x:", rebR(rebRepossess(p_publicX, ECC_BYTES)),
                "y:", rebR(rebRepossess(p_publicY, ECC_BYTES)),
            "]",
            "private:", rebR(rebRepossess(p_privateKey, ECC_BYTES)),
        "]",
    rebEND);
}


//
//  export ecdh-shared-secret: native [
//      return: "secret"
//          [binary!]
//      private "32-byte private key"
//          [binary!]
//      public "64-byte public key of peer (or OBJECT! with 32-byte X and Y)"
//          [binary! object!]
//  ]
//
REBNATIVE(ecdh_shared_secret)
{
    CRYPT_INCLUDE_PARAMS_OF_ECDH_SHARED_SECRET;

    assert(ECC_BYTES == 32);

    uint8_t public_key[ECC_BYTES * 2];
    rebBytesInto(public_key, ECC_BYTES * 2, "use [bin] [",
        "bin: either binary?", ARG(public), "[", ARG(public), "] [",
            "append copy pick", ARG(public), "'x", "pick", ARG(public), "'y"
        "]",
        "if 64 != length of bin [",
            "fail {Public BINARY! must be 64 bytes total for secp256r1}"
        "]",
        "bin",
    "]", rebEND);

    uint8_t private_key[ECC_BYTES];
    rebBytesInto(private_key, ECC_BYTES,
        "if 32 != length of", ARG(private), "[",
            "fail {Size of PRIVATE key must be 32 bytes for secp256r1}"
        "]",
        ARG(private),
    rebEND);

    uint8_t *secret = rebAllocN(uint8_t, ECC_BYTES);
    if (1 != ecdh_shared_secret_xy(
        public_key,  // x component
        public_key + ECC_BYTES,  // y component
        private_key,
        secret
    )){
        fail ("ecdh_shared_secret_xy() did not return 1");
    }

    return rebRepossess(secret, ECC_BYTES);
}


//
//  init-crypto: native [
//
//  {Initialize random number generators and OS-provided crypto services}
//
//      return: [void!]
//  ]
//
REBNATIVE(init_crypto)
{
    CRYPT_INCLUDE_PARAMS_OF_INIT_CRYPTO;

  #ifdef TO_WINDOWS
    if (CryptAcquireContextW(
        &gCryptProv,
        0,
        0,
        PROV_RSA_FULL,
        CRYPT_VERIFYCONTEXT | CRYPT_SILENT
    )){
        return rebVoid();
    }
    gCryptProv = 0;
  #else
    rng_fd = open("/dev/urandom", O_RDONLY);
    if (rng_fd != -1)
        return rebVoid();
  #endif

    // !!! Should we fail here, or wait to fail until the system tries to
    // generate random data and cannot?
    //
    fail ("INIT-CRYPTO couldn't initialize random number generation");
}


//
//  shutdown-crypto: native [
//
//  {Shut down random number generators and OS-provided crypto services}
//
//      return: [void!]
//  ]
//
REBNATIVE(shutdown_crypto)
{
    CRYPT_INCLUDE_PARAMS_OF_SHUTDOWN_CRYPTO;

  #ifdef TO_WINDOWS
    if (gCryptProv != 0) {
        CryptReleaseContext(gCryptProv, 0);
        gCryptProv = 0;
    }
  #else
    if (rng_fd != -1) {
        close(rng_fd);
        rng_fd = -1;
    }
  #endif

    return Init_Void(D_OUT);
}
