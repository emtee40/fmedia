/** Run fmedia with GUI.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
#include <util/path.h>
#include <util/conf2-writer.h>
#include <util/cmdarg-scheme.h>
#include <util/gui-winapi/dialog.h>
#include <FFOS/process.h>
#include <FFOS/signal.h>


struct gctx {
	ffdl core_dl;
	fmed_core* (*core_init)(char **argv, char **env);
	void (*core_free)(void);
};
static struct gctx *g;
static fmed_core *core;


//LOG
static void fgui_log(uint flags, fmed_logdata *ld);
static const fmed_log fgui_logger = {
	&fgui_log
};

static void fgui_log(uint flags, fmed_logdata *ld)
{
	char buf[4096];
	ffuint cap = FFCNT(buf) - FFSLEN("\n");
	ffstr s = FFSTR_INITN(buf, 0);

	ffstr_addfmt(&s, cap, "%s %s %s: ", ld->stime, ld->level, ld->module);
	if (ld->ctx != NULL)
		ffstr_addfmt(&s, cap, "%S:\t", ld->ctx);
	ffstr_addfmtv(&s, cap, ld->fmt, ld->va);
	if (flags & FMED_LOG_SYS)
		ffstr_addfmt(&s, cap, ": %E", fferr_last());
	s.ptr[s.len++] = '\r';
	s.ptr[s.len++] = '\n';

	ffui_msgdlg_show("fmedia", s.ptr, s.len, FFUI_MSGDLG_ERR);
}


static void open_input(int argc, char **argv)
{
	const fmed_queue *qu;
	fmed_que_entry e;
	void *qe, *first = NULL;

	if (NULL == (qu = core->getmod("#queue.queue")))
		return;

	ffcmdarg a;
	ffcmdarg_init(&a, (const char**)argv, argc);

	for (;;) {
		ffstr val;
		int r = ffcmdarg_parse(&a, &val);
		if (r == FFCMDARG_DONE)
			break;
		else if (r != FFCMDARG_RVAL)
			continue;

		ffmem_zero_obj(&e);
		ffstr_setstr(&e.url, &val);
		qe = qu->add(&e);
		if (first == NULL)
			first = qe;
	}

	ffcmdarg_fin(&a);

	if (first != NULL)
		qu->cmd(FMED_QUE_PLAY, first);
}


/**
@mode: enum FMED_INSTANCE_MODE */
static int gcmd_send(const fmed_globcmd_iface *globcmd, uint mode, const char **argv, int argc)
{
	int r = -1;
	ffconfw confw = {};
	ffconfw_init(&confw, 0);

	if (mode == FMED_IM_CLEARPLAY)
		ffconfw_addkeyz(&confw, "clear");

	char *cmd = "play";
	if (mode == FMED_IM_ADD)
		cmd = "add";
	ffconfw_addkeyz(&confw, cmd);

	ffcmdarg a;
	ffcmdarg_init(&a, argv, argc);
	for (;;) {
		ffstr val;
		int r = ffcmdarg_parse(&a, &val);
		if (r == FFCMDARG_DONE)
			break;
		else if (r == FFCMDARG_RVAL)
			if (ffconfw_addstr(&confw, &val) < 0)
				goto end;
	}

	ffconfw_fin(&confw);
	ffstr out;
	ffconfw_output(&confw, &out);

	if (0 != globcmd->write(out.ptr, out.len)) {
		goto end;
	}

	r = 0;
end:
	ffconfw_close(&confw);
	ffcmdarg_fin(&a);
	return r;
}


static int loadcore(char *argv0)
{
	int rc = -1;
	char buf[FF_MAXPATH];
	const char *path;
	ffdl dl = NULL;
	ffarr a = {0};

	if (NULL == (path = ffps_filename(buf, sizeof(buf), argv0)))
		goto end;
	if (0 == ffstr_catfmt(&a, "%s/../core.%s%Z", path, FFDL_EXT))
		goto end;
	a.len = ffpath_normalize(a.ptr, a.cap, a.ptr, a.len - 1, 0);
	((char*)a.ptr)[a.len] = '\0';

	if (NULL == (dl = ffdl_open(a.ptr, 0))) {
		ffstderr_fmt("can't load %s: %s\n", a.ptr, ffdl_errstr());
		goto end;
	}

	g->core_init = (void*)ffdl_addr(dl, "core_init");
	g->core_free = (void*)ffdl_addr(dl, "core_free");
	if (g->core_init == NULL || g->core_free == NULL)
		goto end;

	g->core_dl = dl;
	dl = NULL;
	rc = 0;

end:
	FF_SAFECLOSE(dl, NULL, ffdl_close);
	ffarr_free(&a);
	return rc;
}

#ifndef _DEBUG
extern void _crash_handler(const char *fullname, const char *version, struct ffsig_info *inf);

/** Called by FFOS on program crash. */
static void crash_handler(struct ffsig_info *inf)
{
	const char *ver = (core != NULL) ? core->props->version_str : "";
	_crash_handler("fmedia", ver, inf);
}
#endif

int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	if (NULL == (g = ffmem_new(struct gctx)))
		return 1;

#ifndef _DEBUG
	static const uint sigs_fault[] = { FFSIG_SEGV, FFSIG_ILL, FFSIG_FPE, FFSIG_ABORT };
	ffsig_subscribe(&crash_handler, sigs_fault, FFCNT(sigs_fault));
	// ffsig_raise(FFSIG_SEGV);
#endif

	int argc;
	char **argv = ffcmdarg_from_linew(GetCommandLine(), &argc);

	if (0 != loadcore(NULL))
		goto end;

	if (NULL == (core = g->core_init(argv, NULL)))
		goto end;

	core->cmd(FMED_SETLOG, &fgui_logger);
	core->props->gui = 1;
	if (0 != core->cmd(FMED_CONF, NULL))
		goto end;

	const fmed_log *glog;
	core->insmod("gui.log", NULL);
	if (NULL == (glog = core->getmod("gui.log")))
		goto end;
	core->cmd(FMED_SETLOG, glog);

	int im = core->getval("instance_mode");
	const fmed_globcmd_iface *globcmd = NULL;
	if (im != FMED_IM_OFF
		&& NULL != (globcmd = core->getmod("#globcmd.globcmd"))
		&& 0 == globcmd->ctl(FMED_GLOBCMD_OPEN, NULL)) {

		gcmd_send(globcmd, im, (const char**)argv, argc);
		goto end;
	}

	if (0 != core->sig(FMED_OPEN))
		goto end;

	if (globcmd != NULL) {
		globcmd->ctl(FMED_GLOBCMD_START, NULL);
	}

	open_input(argc, argv);

	core->sig(FMED_START);

end:
	if (core != NULL) {
		g->core_free();
	}
	FF_SAFECLOSE(g->core_dl, NULL, ffdl_close);
	ffmem_free(argv);
	ffmem_free(g);
	return 0;
}
