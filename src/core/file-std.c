/** File std input/output.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>


extern const fmed_core *core;

#undef dbglog
#undef errlog
#undef syserrlog
#define dbglog(trk, ...)  fmed_dbglog(core, trk, "file", __VA_ARGS__)
#define errlog(trk, ...)  fmed_errlog(core, trk, "file", __VA_ARGS__)
#define syserrlog(trk, ...)  fmed_syserrlog(core, trk, "file", __VA_ARGS__)


//STDIN
static void* file_stdin_open(fmed_filt *d);
static int file_stdin_read(void *ctx, fmed_filt *d);
static void file_stdin_close(void *ctx);
const fmed_filter file_stdin = {
	&file_stdin_open, &file_stdin_read, &file_stdin_close
};

//STDOUT
static void* file_stdout_open(fmed_filt *d);
static int file_stdout_write(void *ctx, fmed_filt *d);
static void file_stdout_close(void *ctx);
const fmed_filter file_stdout = {
	&file_stdout_open, &file_stdout_write, &file_stdout_close
};


typedef struct stdin_ctx {
	fffd fd;
	uint64 total;
	ffarr buf;
} stdin_ctx;

static void* file_stdin_open(fmed_filt *d)
{
	stdin_ctx *f = ffmem_tcalloc1(stdin_ctx);
	if (f == NULL)
		return NULL;
	f->fd = ffstdin;

	if (NULL == ffarr_alloc(&f->buf, 64 * 1024)) {
		syserrlog(d->trk, "%s", ffmem_alloc_S);
		goto done;
	}

	return f;

done:
	file_stdin_close(f);
	return NULL;
}

static void file_stdin_close(void *ctx)
{
	stdin_ctx *f = ctx;
	ffarr_free(&f->buf);
	ffmem_free(f);
}

static int file_stdin_read(void *ctx, fmed_filt *d)
{
	stdin_ctx *f = ctx;
	ssize_t r;
	uint64 seek = 0;
	ffstr buf;

	if ((int64)d->input.seek != FMED_NULL) {
		uint64 off = f->total - f->buf.len;
		if (d->input.seek < off) {
			errlog(d->trk, "can't seek backward on stdin.  offset:%U", d->input.seek);
			return FMED_RERR;
		}
		seek = d->input.seek;
		d->input.seek = FMED_NULL;
		if (f->total > seek) {
			ffstr_set(&buf, f->buf.ptr, f->buf.len);
			ffstr_shift(&buf, seek - off);
			goto data;
		}
	}

	for (;;) {
		r = ffstd_fread(f->fd, f->buf.ptr, f->buf.cap);
		if (r == 0) {
			d->outlen = 0;
			return FMED_RDONE;
		} else if (r < 0) {
			syserrlog(d->trk, "%s", fffile_read_S);
			return FMED_RERR;
		}
		dbglog(d->trk, "read %L bytes from stdin"
			, r);
		f->total += r;
		f->buf.len = r;
		ffstr_set(&buf, f->buf.ptr, f->buf.len);
		if (seek == 0)
			break;
		else if (f->total > seek) {
			uint64 off = f->total - f->buf.len;
			ffstr_shift(&buf, seek - off);
			break;
		}
	}

data:
	d->out = buf.ptr, d->outlen = buf.len;
	return FMED_RDATA;
}


struct stdout_conf {
	size_t bufsize;
};
static struct stdout_conf out_conf;

static const fmed_conf_arg stdout_conf_args[] = {
	{ "buffer_size",  FMC_SIZENZ,  FMC_O(struct stdout_conf, bufsize) },
	{}
};

int stdout_config(fmed_conf_ctx *ctx)
{
	out_conf.bufsize = 64 * 1024;
	fmed_conf_addctx(ctx, &out_conf, stdout_conf_args);
	return 0;
}

typedef struct stdout_ctx {
	fffd fd;
	ffarr buf;
	uint64 fsize;

	struct {
		uint nmwrite;
		uint nfwrite;
	} stat;
} stdout_ctx;

static void* file_stdout_open(fmed_filt *d)
{
	stdout_ctx *f = ffmem_tcalloc1(stdout_ctx);
	if (f == NULL)
		return NULL;
	f->fd = ffstdout;

	if (NULL == ffarr_alloc(&f->buf, out_conf.bufsize)) {
		syserrlog(d->trk, "%s", ffmem_alloc_S);
		goto done;
	}

	return f;

done:
	file_stdout_close(f);
	return NULL;
}

static void file_stdout_close(void *ctx)
{
	stdout_ctx *f = ctx;
	ffarr_free(&f->buf);
	ffmem_free(f);
}

static int file_stdout_writedata(stdout_ctx *f, const char *data, size_t len, fmed_filt *d)
{
	size_t r;
	r = fffile_write(f->fd, data, len);
	if (r != len) {
		syserrlog(d->trk, "%s", fffile_write_S);
		return -1;
	}
	f->stat.nfwrite++;

	dbglog(d->trk, "written %L bytes at offset %U (%L pending)", r, f->fsize, d->datalen);
	f->fsize += r;
	return r;
}

static int file_stdout_write(void *ctx, fmed_filt *d)
{
	stdout_ctx *f = ctx;
	ssize_t r;
	ffstr dst;

	if ((int64)d->output.seek != FMED_NULL) {

		if (f->buf.len != 0) {
			if (-1 == file_stdout_writedata(f, f->buf.ptr, f->buf.len, d))
				return FMED_RERR;
			f->buf.len = 0;
		}

		errlog(d->trk, "can't seek on stdout.  offset:%U", d->output.seek);
		return FMED_RERR;
	}

	for (;;) {

		r = ffstr_gather((ffstr*)&f->buf, &f->buf.cap, d->data, d->datalen, f->buf.cap, &dst);
		d->data += r;
		d->datalen -= r;
		if (dst.len == 0) {
			f->stat.nmwrite++;
			if (!(d->flags & FMED_FLAST) || f->buf.len == 0)
				break;
			ffstr_set(&dst, f->buf.ptr, f->buf.len);
		}
		f->buf.len = 0;

		if (-1 == file_stdout_writedata(f, dst.ptr, dst.len, d))
			return FMED_RERR;

		if (d->datalen == 0)
			break;
	}

	if (d->flags & FMED_FLAST) {
		return FMED_RDONE;
	}

	return FMED_ROK;
}
