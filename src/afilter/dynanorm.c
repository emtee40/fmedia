/** Dynamic Audio Normalizer filter.
Copyright (c) 2018 Simon Zolin */

#include <fmedia.h>
#include <DynamicAudioNormalizer/DynamicAudioNormalizer-ff.h>

#undef dbglog
#undef errlog
#define dbglog(trk, ...)  fmed_dbglog(core, trk, "dynanorm", __VA_ARGS__)
#define errlog(trk, ...)  fmed_errlog(core, trk, "dynanorm", __VA_ARGS__)

static const fmed_core *core;

struct danconf {
	struct dynanorm_conf conf;
	byte channels_coupled;
	byte enable_dc_correction;
	byte alt_boundary_mode;
};
static struct danconf *sconf;

#define OFF(m)  FMC_O(struct dynanorm_conf, m)
static const fmed_conf_arg danorm_conf_args[] = {
	{ "frame_len_msec",	FMC_INT32, OFF(frameLenMsec) },
	{ "filter_size",	FMC_INT32, OFF(filterSize) },
	{ "peak_value",	FMC_FLOAT64S, OFF(peakValue) },
	{ "max_amplification",	FMC_FLOAT64S, OFF(maxAmplification) },
	{ "target_rms",	FMC_FLOAT64S, OFF(targetRms) },
	{ "compress_factor",	FMC_FLOAT64S, OFF(compressFactor) },
	{ "channels_coupled",	FMC_BOOL8, FMC_O(struct danconf, channels_coupled) },
	{ "enable_dc_correction",	FMC_BOOL8, FMC_O(struct danconf, enable_dc_correction) },
	{ "alt_boundary_mode",	FMC_BOOL8, FMC_O(struct danconf, alt_boundary_mode) },
	{}
};
#undef OFF

static int danorm_f_conf(fmed_conf_ctx *ctx)
{
	sconf = ffmem_new(struct danconf);
	sconf->channels_coupled = 255;
	sconf->enable_dc_correction = 255;
	sconf->alt_boundary_mode = 255;
	dynanorm_init(&sconf->conf);
	fmed_conf_addctx(ctx, sconf, danorm_conf_args);
	return 0;
}

static const fmed_filter fmed_danorm_f;

static const void* danorm_iface(const char *name)
{
	if (ffsz_eq(name, "filter"))
		return &fmed_danorm_f;
	return NULL;
}

static int danorm_sig(uint signo)
{
	switch (signo) {
	case FMED_STOP:
		ffmem_free(sconf);
	}
	return 0;
}

static void danorm_destroy(void)
{
}

static int danorm_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (ffsz_eq(name, "filter"))
		return danorm_f_conf(ctx);
	return -1;
}

static const fmed_mod fmed_danorm_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	danorm_iface, danorm_sig, danorm_destroy, danorm_conf
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_danorm_mod;
}


struct danorm {
	uint state;
	void *ctx;
	ffarr buf;
	uint off;
	ffpcm fmt;
};

static void* danorm_f_open(fmed_filt *d)
{
	struct danorm *c = ffmem_new(struct danorm);
	if (c == NULL)
		return NULL;
	return c;
}

static void danorm_f_close(void *ctx)
{
	struct danorm *c = ctx;
	dynanorm_close(c->ctx);
	ffarr_free(&c->buf);
	ffmem_free(c);
}

static int danorm_f_process(void *ctx, fmed_filt *d)
{
	struct danorm *c = ctx;
	ssize_t r;

	switch (c->state) {

	case 0:
		if (d->audio.fmt.format != FFPCM_FLOAT64 || d->audio.fmt.ileaved) {
			struct fmed_aconv conv;
			conv.in = d->audio.fmt;
			conv.out = d->audio.fmt;
			conv.out.format = FFPCM_FLOAT64;
			conv.out.ileaved = 0;
			if (d->audio.convfmt.format == 0)
				d->audio.convfmt.format = d->audio.fmt.format;
			if (d->audio.convfmt.channels == 0)
				d->audio.convfmt.channels = d->audio.fmt.channels;
			if (d->audio.convfmt.sample_rate == 0)
				d->audio.convfmt.sample_rate = d->audio.fmt.sample_rate;
			d->audio.fmt = conv.out;
			void *f = (void*)d->track->cmd(d->trk, FMED_TRACK_FILT_ADDPREV, "afilter.conv");
			if (f == NULL)
				return FMED_RERR;
			const struct fmed_filter2 *aconv = core->getmod("afilter.conv");
			void *fi = (void*)d->track->cmd(d->trk, FMED_TRACK_FILT_INSTANCE, f);
			if (fi == NULL)
				return FMED_RERR;
			aconv->cmd(fi, 0, &conv);
			d->out = d->data,  d->outlen = d->datalen;
			c->state = 1;
			return FMED_RBACK;
		}
		// fall through

	case 1: {
		struct dynanorm_conf conf = sconf->conf;
		conf.channels = d->audio.fmt.channels;
		conf.sampleRate = d->audio.fmt.sample_rate;
		if (sconf->channels_coupled != 255)
			conf.channelsCoupled = sconf->channels_coupled;
		if (sconf->enable_dc_correction != 255)
			conf.enableDCCorrection = sconf->enable_dc_correction;
		if (sconf->alt_boundary_mode != 255)
			conf.altBoundaryMode = sconf->alt_boundary_mode;

		if (0 != dynanorm_open(&c->ctx, &conf)) {
			errlog(d->trk, "dynanorm_open()");
			return FMED_RERR;
		}

		uint ch = d->audio.fmt.channels;
		size_t cap = ffpcm_samples(conf.frameLenMsec, d->audio.fmt.sample_rate);
		if (NULL == ffarr_alloc(&c->buf, sizeof(void*) * ch + cap * sizeof(double) * ch))
			return FMED_RSYSERR;
		if (d->audio.fmt.channels > 8)
			return FMED_RERR;
		c->buf.len = cap;
		ffarrp_setbuf((void**)c->buf.ptr, ch, c->buf.ptr + sizeof(void*) * ch, cap * sizeof(double));
		ffpcm_fmtcopy(&c->fmt, &d->audio.fmt);
		c->state = 2;
		// fall through
	}

	case 2:
		break;
	}

	if (d->seek_req) {
		dynanorm_reset(c->ctx);
		return FMED_RMORE;
	}

	if (d->flags & FMED_FFWD)
		c->off = 0;

	ffbool done = 0;
	uint sampsize = ffpcm_size1(&c->fmt);
	void *in[8];
	size_t samples;
	while (d->datalen != 0) {
		for (uint i = 0;  i != c->fmt.channels;  i++) {
			in[i] = (char*)d->datani[i] + c->off;
		}
		samples = d->datalen / sampsize;
		size_t in_samps = samples;
		r = dynanorm_process(c->ctx, (const double*const*)in, &samples, (double**)c->buf.ptr, c->buf.len);
		dbglog(d->trk, "output:%L  input:%L/%L", r, samples, in_samps);
		d->datalen -= samples * sampsize;
		c->off += samples * sampsize;
		if (r < 0) {
			errlog(d->trk, "dynanorm_process()");
			return FMED_RERR;
		} else if (r != 0)
			goto data;
	}

	if (!(d->flags & FMED_FLAST))
		return FMED_RMORE;

	r = dynanorm_process(c->ctx, NULL, NULL, (double**)c->buf.ptr, c->buf.len);
	if (r < 0) {
		errlog(d->trk, "dynanorm_process()");
		return FMED_RERR;
	}
	dbglog(d->trk, "output:%L", r);
	done = ((size_t)r < c->buf.len);

data:
	d->outni = (void**)c->buf.ptr;
	d->outlen = r * sampsize;
	return (done) ? FMED_RDONE : FMED_RDATA;
}

static const fmed_filter fmed_danorm_f = { danorm_f_open, danorm_f_process, danorm_f_close };
