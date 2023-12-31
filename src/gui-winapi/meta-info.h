/** fmedia: gui-winapi: display file meta info
2021, Simon Zolin */

struct gui_winfo {
	ffui_wnd winfo;
	ffui_view vinfo;
};

const ffui_ldr_ctl winfo_ctls[] = {
	FFUI_LDR_CTL(struct gui_winfo, winfo),
	FFUI_LDR_CTL(struct gui_winfo, vinfo),
	FFUI_LDR_CTL_END
};

void info_click()
{
	struct gui_winfo *w = gg->winfo;
	int i, isub;
	ffui_point pt;
	ffui_cur_pos(&pt);
	if (-1 == (i = ffui_view_hittest(&w->vinfo, &pt, &isub))
		|| isub != 1)
		return;
	ffui_view_edit(&w->vinfo, i, 1);
}

void winfo_action(ffui_wnd *wnd, int id)
{
	switch (id) {
	case INFOEDIT:
		info_click();
		break;
	}
}

void winfo_addpair(const ffstr *name, const ffstr *val)
{
	struct gui_winfo *w = gg->winfo;
	ffui_viewitem it;
	ffui_view_iteminit(&it);
	ffui_view_settextstr(&it, name);
	ffui_view_setgroupid(&it, 0);
	ffui_view_append(&w->vinfo, &it);

	ffui_view_settextstr(&it, val);
	ffui_view_set(&w->vinfo, 1, &it);
}

void winfo_show_core(void *param)
{
	struct gui_winfo *w = gg->winfo;
	fmed_que_entry *e;
	ffui_viewgrp vg;
	int i, grp = 0;
	ffstr name, empty = {}, *val;
	ffarr data = {};

	ffui_view_showgroups(&w->vinfo, 1);
	ffui_show(&w->winfo, 1);

	if (-1 == (i = wmain_list_next_selected(-1))) {
		ffui_view_clear(&w->vinfo);
		ffui_view_cleargroups(&w->vinfo);
		return;
	}

	e = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i);

	ffui_settextstr(&w->winfo, &e->url);

	ffui_redraw(&w->vinfo, 0);
	ffui_view_clear(&w->vinfo);
	ffui_view_cleargroups(&w->vinfo);

	ffui_viewgrp_reset(&vg);
	ffui_viewgrp_settextz(&vg, "Metadata");
	ffui_view_insgrp(&w->vinfo, -1, grp, &vg);

	ffstr_setz(&name, "File path");
	winfo_addpair(&name, &e->url);

	fffileinfo fi = {};
	ffbool have_fi = (0 == fffile_infofn(e->url.ptr, &fi));
	ffarr_alloc(&data, 255);

	ffstr_setz(&name, "File size");
	data.len = 0;
	if (have_fi)
		ffstr_catfmt(&data, "%U KB", fffile_infosize(&fi) / 1024);
	winfo_addpair(&name, (ffstr*)&data);

	ffstr_setz(&name, "File date");
	data.len = 0;
	if (have_fi) {
		ffdatetime dt;
		fftime t = fffile_infomtime(&fi);
		uint tzoff = core->cmd(FMED_TZOFFSET);
		t.sec += FFTIME_1970_SECONDS + tzoff;
		fftime_split1(&dt, &t);
		data.len = fftime_tostr1(&dt, data.ptr, data.cap, FFTIME_DATE_WDMY | FFTIME_HMS);
	}
	winfo_addpair(&name, (ffstr*)&data);

	ffstr_setz(&name, "Info");
	if (NULL == (val = gg->qu->meta_find(e, FFSTR("__info"))))
		val = &empty;
	winfo_addpair(&name, val);

	for (i = 0;  NULL != (val = gg->qu->meta(e, i, &name, 0));  i++) {
		if (val == FMED_QUE_SKIP)
			continue;
		winfo_addpair(&name, val);
	}
	ffui_redraw(&w->vinfo, 1);
	ffarr_free(&data);
}

void winfo_show(uint show)
{
	struct gui_winfo *w = gg->winfo;

	if (!show) {
		ffui_show(&w->winfo, 0);
		return;
	}

	corecmd_addfunc(winfo_show_core, NULL);
}

void winfo_init()
{
	struct gui_winfo *w = ffmem_new(struct gui_winfo);
	gg->winfo = w;
	w->winfo.hide_on_close = 1;
	w->winfo.on_action = winfo_action;
}
