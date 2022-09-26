/** Split one track into many.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>
#include <util/path.h>


extern const fmed_core *core;

#undef errlog
#undef dbglog
#define errlog(...)  fmed_errlog(core, NULL, "split", __VA_ARGS__)
#define dbglog(...)  fmed_dbglog(core, NULL, "split", __VA_ARGS__)

struct split {
	uint state;
	uint64 until;
	uint64 splitby;
	uint sampsize;
	const fmed_modinfo *mi;
	const char *datatype;
};

static void split_close(void *ctx)
{
	struct split *s = ctx;
	ffmem_free(s);
}

static void* split_open(fmed_filt *d)
{
	if (d->audio.split == (uint64)FMED_NULL)
		return FMED_FILT_SKIP;

	const char *ofn = d->out_filename;
	if (ofn == NULL) {
		errlog("output file isn't specified");
		return NULL;
	}
	ffstr ext;
	ffpath_splitname(ofn, ffsz_len(ofn), NULL, &ext);
	const fmed_modinfo *mi = core->getmod2(FMED_MOD_OUTEXT, ext.ptr, ext.len);
	if (mi == NULL) {
		errlog("no module can write to this file format: %S", &ext);
		return NULL;
	}

	struct split *s;
	if (NULL == (s = ffmem_new(struct split)))
		return NULL;

	s->mi = mi;
	s->splitby = ffpcm_samples(d->audio.split, d->audio.fmt.sample_rate);
	s->until = s->splitby;
	if (s->splitby == 0) {
		errlog("split value is 0", 0);
		split_close(s);
		return NULL;
	}
	s->sampsize = ffpcm_size(d->audio.fmt.format, d->audio.fmt.channels);
	s->datatype = d->datatype;
	return s;
}

static int split_process(void *ctx, fmed_filt *d)
{
	struct split *s = ctx;
	uint64 pos;

	switch (s->state) {
	case 0:
		d->datatype = s->datatype; // the audio output filter needs input data type, but overwrites this value afterwards
		if (0 == d->track->cmd(d->trk, FMED_TRACK_FILT_ADDLAST, "afilter.autoconv")
			|| 0 == d->track->cmd(d->trk, FMED_TRACK_FILT_ADDLAST, s->mi->name)
			|| 0 == d->track->cmd(d->trk, FMED_TRACK_FILT_ADDLAST, "#file.out"))
			return FMED_RERR;
		d->out_seekable = 1;
		s->state = 1;
		break;

	case 1:
		if (!(d->flags & FMED_FFWD) && d->datalen == 0)
			return FMED_RMORE;
		break;
	}

	d->out = d->data;
	d->outlen = d->datalen;

	if (FMED_NULL == (int64)(pos = d->audio.pos))
		return FMED_RDONE;

	if (d->stream_copy) {
		dbglog("at %U", pos);
		if (d->audio.pos >= s->until) {
			dbglog("reached sample #%U", s->until);
			s->until += s->splitby;
			d->outlen = 0;
			s->state = 0;
			return FMED_RNEXTDONE;
		}

	} else {
		if (!d->audio.fmt.ileaved) {
			errlog("non-interleaved input data isn't supported");
			return FMED_RERR;
		}

		uint samps = d->datalen / s->sampsize;
		dbglog("at %U..%U", pos, pos + samps);
		if (pos + samps >= s->until) {
			dbglog("reached sample #%U", s->until);
			d->outlen = (s->until - pos) * s->sampsize;
			if (pos > s->until)
				d->outlen = 0;
			s->until += s->splitby;
			d->datalen -= d->outlen;
			d->audio.pos += d->outlen / s->sampsize;
			s->state = 0;
			return FMED_RNEXTDONE;
		}
	}

	if (d->flags & FMED_FLAST)
		return FMED_RDONE;
	d->datalen = 0;
	return FMED_RDATA;
}

const fmed_filter fmed_sndmod_split = { split_open, split_process, split_close };
