/** fmedia/Android: track manager
2022, Simon Zolin */

struct filter {
	const char *name;
	const fmed_filter *iface;
	void *obj;
	fftime busytime;
	uint backward_skip :1;
};

/* Max filter chain:
ifile->detector->ifmt->meta->ctl
ifile->detector->ifmt->copy->ctl ->ofile
ifile->detector->ifmt->ctl ->ofmt->ofile */
#define MAX_FILTERS 6

struct track_ctx {
	fmed_track_info ti;

	struct filter filters_pool[MAX_FILTERS];
	uint i_fpool;
	struct filter *filters_active[MAX_FILTERS];
	ffslice filters;
	uint cur;
	fftime tstart;
};

const fmed_track track_iface;

static char* trk_chain_print(struct track_ctx *t, const struct filter *mark, char *buf, ffsize cap);

static fmed_track_obj* trk_create(uint cmd, const char *url)
{
	struct track_ctx *t = ffmem_new(struct track_ctx);
	t->cur = -1;
	t->tstart = fftime_monotonic();

	fmed_track_info *ti = &t->ti;
	ti->trk = t;
	ti->track = &track_iface;
	ti->input.size = FMED_NULL;
	ti->input.seek = FMED_NULL;
	ti->output.seek = FMED_NULL;
	ti->audio.seek = FMED_NULL;
	ti->audio.until = FMED_NULL;
	ti->audio.decoder = "";
	t->filters.ptr = t->filters_active;
	return t;
}

/** Print the time we spent inside each filter */
static void trk_busytime_print(struct track_ctx *t)
{
	fftime total = fftime_monotonic();
	fftime_sub(&total, &t->tstart);

	ffvec buf = {};
	ffvec_addfmt(&buf, "busy time: %u.%06u.  "
		, (int)fftime_sec(&total), (int)fftime_usec(&total));

	struct filter *f;
	FF_FOREACH(t->filters_pool, f) {
		if (f->obj == NULL)
			continue;
		uint percent = fftime_to_usec(&f->busytime) * 100 / fftime_to_usec(&total);
		ffvec_addfmt(&buf, "%s: %u.%06u (%u%%), "
			, f->name, (int)fftime_sec(&f->busytime), (int)fftime_usec(&f->busytime)
			, percent);
	}
	buf.len -= FFS_LEN(", ");

	infolog1(t, "%S", &buf);
	ffvec_free(&buf);
}

static void trk_close(struct track_ctx *t)
{
	if (t == NULL) return;

	for (uint i = 0;  i != MAX_FILTERS;  i++) {
		const struct filter *f = &t->filters_pool[i];
		if (f->obj != NULL) {
			dbglog1(t, "%s: closing filter", f->name);
			f->iface->close(f->obj);
		}
	}

	fmed_track_info *ti = &t->ti;

	if (ti->print_time)
		trk_busytime_print(t);

	char **ps;
	FFSLICE_WALK(&ti->meta, ps) {
		ffmem_free(*ps);
	}
	ffvec_free(&ti->meta);

	ffmem_free(ti->out_filename);

	dbglog1(t, "track closed");
	ffmem_free(t);
}

static fmed_track_info* trk_conf(fmed_track_obj *trk)
{
	struct track_ctx *t = trk;
	return &t->ti;
}

static void trk_process(struct track_ctx *t)
{
	uint i = 0;
	int r;
	for (;;) {
		struct filter *f = *ffslice_itemT(&t->filters, i, struct filter*);

		if (f->backward_skip) {
			// last time the filter returned FMED_ROK
			f->backward_skip = 0;
			if (i != 0) {
				// go to previous filter
				r = FMED_RMORE;
				goto result;
			}
			// calling first-in-chain filter
		}

		t->ti.flags &= ~FMED_FFIRST;
		if (i == 0)
			t->ti.flags |= FMED_FFIRST;

		fftime t1, t2;
		if (t->ti.print_time)
			t1 = fftime_monotonic();

		t->cur = i;
		if (f->obj == NULL) {
			dbglog1(t, "%s: opening filter", f->name);
			void *obj;
			if (NULL == (obj = f->iface->open(&t->ti))) {
				dbglog1(t, "%s: filter open failed", f->name);
				goto err;
			}
			if (obj == FMED_FILT_SKIP) {
				dbglog1(t, "%s: skipping", f->name);
				t->ti.data_out = t->ti.data_in;
				r = FMED_RDONE;
				goto result;
			}
			f->obj = obj;
		}

		dbglog1(t, "%s: calling filter, input:%L  flags:%xu"
			, f->name, t->ti.data_in.len, t->ti.flags);

		r = f->iface->process(f->obj, &t->ti);
		i = t->cur; // t->cur may be modified when adding filters

		if (t->ti.print_time) {
			t2 = fftime_monotonic();
			fftime_sub(&t2, &t1);
			fftime_add(&f->busytime, &t2);
		}

		static const char rc_str[][16] = {
			"FMED_RERR",
			"FMED_ROK",
			"FMED_RDATA",
			"FMED_RDONE",
			"FMED_RLASTOUT",
			"FMED_RNEXTDONE",
			"FMED_RMORE",
			"FMED_RBACK",
			"FMED_RASYNC",
			"FMED_RFIN",
			"FMED_RSYSERR",
			"FMED_RDONE_ERR",
		};
		dbglog1(t, "%s: filter returned %s, output:%L"
			, f->name, rc_str[r+1], t->ti.data_out.len);

result:
		switch (r) {
		case FMED_RDONE:
		case FMED_RLASTOUT:
			if (r == FMED_RDONE) {
				// deactivate filter
				ffslice_rmT(&t->filters, i, 1, void*);
				i--;
			} else {
				// deactivate this and all previous filters
				ffslice_rmT(&t->filters, 0, i + 1, void*);
				i = -1;
			}
			if (t->filters.len == 0) {
				// all filters have finished
				goto fin;
			}

			{
			char buf[200];
			dbglog1(t, "chain [%s]"
				, trk_chain_print(t, NULL, buf, sizeof(buf)));
			}

			goto go_fwd;

		case FMED_ROK:
			f->backward_skip = 1;
			// fallthrough
		case FMED_RDATA:
go_fwd:
			if (i + 1 == t->filters.len) {
				// last-in-chain filter returned data
				goto go_back;
			}
			i++;

			t->ti.flags |= FMED_FFWD;
			t->ti.data_in = t->ti.data_out;
			ffstr_null(&t->ti.data_out);
			break;

		case FMED_RMORE:
go_back:
			if (i == 0) {
				errlog1(t, "%s: first-in-chain filter wants more data", f->name);
				goto err;
			}
			i--;
			t->ti.flags &= ~FMED_FFWD;
			ffstr_null(&t->ti.data_in);
			ffstr_null(&t->ti.data_out);
			break;

		case FMED_RFIN:
			goto fin;

		case FMED_RERR:
			goto err;

		default:
			errlog1(t, "%s: bad return code %u", f->name, r);
			goto err;
		}
	}

err:
	if (t->ti.error == 0)
		t->ti.error = FMED_E_OTHER;

fin:
	dbglog1(t, "finished processing: %u", t->ti.error);
}

static char* trk_chain_print(struct track_ctx *t, const struct filter *mark, char *buf, ffsize cap)
{
	cap--;
	ffstr s = FFSTR_INITN(buf, 0);
	const struct filter **pf, *f;
	FFSLICE_WALK(&t->filters, pf) {
		f =  *pf;
		if (f == mark)
			ffstr_addchar(&s, cap, '*');
		ffstr_addfmt(&s, cap, "%s -> ", f->name);
	}
	s.ptr[s.len] = '\0';
	return buf;
}

/**
Return filter index within chain */
static int trk_filter_add(struct track_ctx *t, const char *name, const fmed_filter *iface, uint pos)
{
	if (t->i_fpool == MAX_FILTERS) {
		errlog1(t, "max filters limit reached");
		return -1;
	}

	struct filter *f = t->filters_pool + t->i_fpool;
	t->i_fpool++;
	f->iface = iface;
	f->name = name;

	t->filters.len++;
	struct filter **pf;
	if ((int)pos < 0 || pos+1 == t->filters.len)
		pf = ffslice_lastT(&t->filters, struct filter*);
	else
		pf = ffslice_moveT(&t->filters, pos, pos+1, t->filters.len - 1 - pos, struct filter*);
	*pf = f;

	char buf[200];
	dbglog1(t, "%s: added to chain [%s]"
		, f->name, trk_chain_print(t, f, buf, sizeof(buf)));
	return pf - (struct filter**)t->filters.ptr;
}

static int trk_meta_enum(struct track_ctx *t, fmed_trk_meta *meta);

static ffssize trk_cmd(void *trk, uint cmd, ...)
{
	struct track_ctx *t = trk;
	dbglog1(t, "%s: %u", __func__, cmd);

	ffssize r = -1;
	va_list va;
	va_start(va, cmd);

	switch (cmd) {

	case FMED_TRACK_START:
		trk_process(t);
		r = 0;
		break;

	case FMED_TRACK_STOP:
		trk_close(t);
		r = 0;
		break;

	case FMED_TRACK_FILT_ADD:
	case FMED_TRACK_FILT_ADDPREV: {
		const char *name = va_arg(va, void*);
		const fmed_filter *fi = (fmed_filter*)core->cmd(FMED_FILTER_BYNAME, name);
		if (fi == NULL) {
			r = 0;
			break;
		}
		uint pos = t->cur + 1;
		if (cmd == FMED_TRACK_FILT_ADDPREV)
			pos = t->cur;
		if ((int)t->cur < 0)
			pos = -1;
		r = trk_filter_add(t, name, fi, pos);
		if (cmd == FMED_TRACK_FILT_ADDPREV && (uint)r == t->cur && r >= 0)
			t->cur++; // the current filter added a new filter before it
		r++;
		break;
	}

	case FMED_TRACK_META_ENUM:
		r = trk_meta_enum(trk, va_arg(va, fmed_trk_meta*));
		break;

	default:
		errlog1(t, "%s: bad command %u", __func__, cmd);
	}

	va_end(va);
	return r;
}

static void trk_meta_set(void *trk, const ffstr *name, const ffstr *val, uint flags)
{
	struct track_ctx *t = trk;
	*ffvec_pushT(&t->ti.meta, char*) = ffsz_dupstr(name);
	*ffvec_pushT(&t->ti.meta, char*) = ffsz_dupstr(val);
}

static int trk_meta_enum(struct track_ctx *t, fmed_trk_meta *meta)
{
	for (;;) {
		if (meta->idx * 2 == t->ti.meta.len)
			return 1;

		const char *k = *ffslice_itemT(&t->ti.meta, meta->idx * 2, char*);
		const char *v = *ffslice_itemT(&t->ti.meta, meta->idx * 2 + 1, char*);
		ffstr_setz(&meta->name, k);
		ffstr_setz(&meta->val, v);
		meta->idx++;

		int skip = 0;
		if (meta->flags & FMED_QUE_UNIQ) {
			char **it;
			FFSLICE_WALK(&t->ti.meta, it) {
				if (k == *it)
					break;
				if (ffstr_ieqz(&meta->name, *it)) {
					skip = 1; // skip current k-v pair because same key is found before
					break;
				}
				it++; // skip value
			}
		}

		if (!skip)
			return 0;
	}
}

const fmed_track track_iface = {
	.create = trk_create,
	.conf = trk_conf,
	.cmd = trk_cmd,
	.meta_set = trk_meta_set,
};

#undef MAX_FILTERS
