REBOL []

name: 'Debugger
source: %debugger/mod-debugger.c
includes: copy [
    %prep/extensions/debugger ;for %tmp-extensions-debugger-init.inc
]
depends: [
]
