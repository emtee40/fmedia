/** M3U, PLS input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
#include <util/url.h>

#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)

const fmed_core *core;
const fmed_queue *qu;

#include <plist/entry.h>

//FMEDIA MODULE
static const void* plist_iface(const char *name);
static int plist_sig(uint signo);
static void plist_destroy(void);
static int plist_conf(const char *name, fmed_conf_ctx *ctx);
static const fmed_mod fmed_plist_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&plist_iface, &plist_sig, &plist_destroy, &plist_conf
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_plist_mod;
}

extern const fmed_filter fmed_cue_input;
extern const fmed_filter cuehook_iface;
extern const fmed_filter fmed_dir_input;
extern int dir_conf(fmed_conf_ctx *ctx);

#include <plist/m3u-read.h>
#include <plist/pls-read.h>
#include <plist/m3u-write.h>

static const void* plist_iface(const char *name)
{
	if (!ffsz_cmp(name, "m3u"))
		return &fmed_m3u_input;
	else if (ffsz_eq(name, "m3u-out"))
		return &m3u_output;
	else if (!ffsz_cmp(name, "pls"))
		return &fmed_pls_input;
	else if (!ffsz_cmp(name, "cue"))
		return &fmed_cue_input;
	else if (ffsz_eq(name, "cuehook"))
		return &cuehook_iface;
	else if (!ffsz_cmp(name, "dir"))
		return &fmed_dir_input;
	return NULL;
}

static int plist_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "dir"))
		return dir_conf(ctx);
	return -1;
}

static int plist_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		if (NULL == (qu = core->getmod("#queue.queue")))
			return 1;
		break;
	}
	return 0;
}

static void plist_destroy(void)
{
}
