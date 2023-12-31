/** fmedia/Android: data filters
2023, Simon Zolin */

#include <fmedia.h>
extern const fmed_core *core;

#define syserrlog1(trk, ...)  fmed_syserrlog(core, trk, NULL, __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define infolog1(trk, ...)  fmed_infolog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

#include "file-read.h"
#include "file-write.h"
#include <dfilter/zstd-comp.h>
#include <dfilter/zstd-decomp.h>
