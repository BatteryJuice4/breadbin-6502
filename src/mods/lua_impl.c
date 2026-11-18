// Lua 5.4 (external/lua, unmodified) built as one unit, so its configuration can
// be set here without editing the vendored files (the same idea as Lua's own
// onelua.c). Only the core, the auxiliary library and the libraries script mods
// may use are compiled: no io, os, package (loadlib) or debug.
//
// Determinism: Lua normally seeds its string hash from the clock and an address,
// which makes pairs() order differ between runs and would break replays. A fixed
// seed keeps it the same everywhere. (math.random is reseeded in script.c.)
#define luai_makeseed(L) (0u)

#include "lprefix.h"

// core, in onelua.c's order
#include "lzio.c"
#include "lctype.c"
#include "lopcodes.c"
#include "lmem.c"
#include "lundump.c"
#include "ldump.c"
#include "lstate.c"
#include "lgc.c"
#include "llex.c"
#include "lcode.c"
#include "lparser.c"
#include "ldebug.c"
#include "lfunc.c"
#include "lobject.c"
#include "ltm.c"
#include "lstring.c"
#include "ltable.c"
#include "ldo.c"
#include "lvm.c"
#include "lapi.c"

// auxiliary library
#include "lauxlib.c"

// the standard libraries script mods get
#include "lbaselib.c"
#include "lcorolib.c"
#include "lmathlib.c"
#include "lstrlib.c"
#include "ltablib.c"
#include "lutf8lib.c"
