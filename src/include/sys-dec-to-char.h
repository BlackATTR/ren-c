//
//  File: %sys-dec-to-char.h
//  Summary: "Decimal conversion wrapper"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 Saphirion AG
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
//=////////////////////////////////////////////////////////////////////////=//
//

EXTERN_C char *dtoa(
    double dd,
    int mode,
    int ndigits,
    int *decpt,
    int *sign,
    char **rve
);

EXTERN_C double STRTOD(const char *s00, const char **se);