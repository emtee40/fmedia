/** fmedia: core: workers
2015,2021, Simon Zolin */

static void wrk_destroy(struct worker *w);
static int FFTHDCALL work_loop(void *param);
static void on_timer(void *param);

/** Initialize worker object */
static int wrk_init(struct worker *w, uint thread)
{
	fftask_init(&w->taskmgr);
	if (FFTIMER_NULL == (w->timer = fftimer_create(0))) {
		syserrlog("fftimer_create");
		return 1;
	}
	fftimerqueue_init(&w->timerq);

	if (FF_BADFD == (w->kq = ffkqu_create())) {
		syserrlog("%s", ffkqu_create_S);
		return 1;
	}
	if (0 != ffkqu_post_attach(&w->kqpost, w->kq))
		syserrlog("%s", "ffkqu_post_attach");

	ffkev_init(&w->evposted);
	w->evposted.oneshot = 0;
	w->evposted.handler = &core_posted;

	if (thread) {
		w->thd = ffthd_create(&work_loop, w, 0);
		if (w->thd == FFTHD_INV) {
			syserrlog("%s", ffthd_create_S);
			wrk_destroy(w);
			return 1;
		}
		// w->id is set inside a new thread
	} else {
		w->id = ffthd_curid();
	}

	w->init = 1;
	return 0;
}

/** Destroy worker object */
static void wrk_destroy(struct worker *w)
{
	if (w->thd != FFTHD_INV) {
		ffthd_join(w->thd, -1, NULL);
		dbglog0("thread %xU exited", (int64)w->id);
		w->thd = FFTHD_INV;
	}
	fftimer_close(w->timer, w->kq);
	if (w->kq != FF_BADFD) {
		ffkqu_post_detach(&w->kqpost, w->kq);
		ffkqu_close(w->kq);
		w->kq = FF_BADFD;
	}
}

/** Find the worker with the least number of active jobs.
Initialize data and create a thread if necessary.
Return worker ID */
static uint work_assign(uint flags)
{
	struct worker *w, *ww = (void*)fmed->workers.ptr;
	uint id = 0, j = -1;

	if (flags == 0) {
		id = 0;
		w = &ww[0];
		goto done;
	}

	FFSLICE_WALK(&fmed->workers, w) {
		uint nj = ffatom_get(&w->njobs);
		if (nj < j) {
			id = w - ww;
			j = nj;
			if (nj == 0)
				break;
		}
	}
	w = &ww[id];

	if (!w->init
		&& 0 != wrk_init(w, 1)) {
		id = 0;
		w = &ww[0];
		goto done;
	}

done:
	if (flags & FMED_WORKER_FPARALLEL)
		(void)ffatom_incret(&w->njobs);
	return id;
}

/** A job is completed */
static void work_release(uint wid, uint flags)
{
	struct worker *w = ffslice_itemT(&fmed->workers, wid, struct worker);
	if (flags & FMED_WORKER_FPARALLEL) {
		ssize_t n = ffatom_decret(&w->njobs);
		(void)n;
		FF_ASSERT(n >= 0);
	}
}

/** Get the number of available workers */
static uint work_avail()
{
	struct worker *w;
	FFSLICE_WALK(&fmed->workers, w) {
		if (ffatom_get(&w->njobs) == 0)
			return 1;
	}
	return 0;
}

void core_job_enter(uint id, size_t *ctx)
{
	struct worker *w = ffslice_itemT(&fmed->workers, id, struct worker);
	FF_ASSERT(w->id == ffthd_curid());
	*ctx = w->taskmgr.tasks.len;
}

ffbool core_job_shouldyield(uint id, size_t *ctx)
{
	struct worker *w = ffslice_itemT(&fmed->workers, id, struct worker);
	FF_ASSERT(w->id == ffthd_curid());
	return (*ctx != w->taskmgr.tasks.len);
}

ffbool core_ismainthr(void)
{
	struct worker *w = ffslice_itemT(&fmed->workers, 0, struct worker);
	return (w->id == ffthd_curid());
}

static int xtask(int signo, fftask *task, uint wid)
{
	struct worker *w = (void*)fmed->workers.ptr;
	FF_ASSERT(wid < fmed->workers.len);
	if (wid >= fmed->workers.len) {
		return -1;
	}
	w = &w[wid];

	dbglog0("task:%p, cmd:%u, active:%u, handler:%p, param:%p"
		, task, signo, fftask_active(&w->taskmgr, task), task->handler, task->param);

	if (w->kq == FFKQ_NULL) {

	} else if (signo == FMED_TASK_XPOST) {
		if (1 == fftask_post(&w->taskmgr, task))
			if (0 != ffkqu_post(&w->kqpost, &w->evposted))
				syserrlog("%s", "ffkqu_post");
	} else {
		fftask_del(&w->taskmgr, task);
	}
	return 0;
}

static void core_task(fftask *task, uint cmd)
{
	struct worker *w = (void*)fmed->workers.ptr;

	dbglog0("task:%p, cmd:%u, active:%u, handler:%p, param:%p"
		, task, cmd, fftask_active(&w->taskmgr, task), task->handler, task->param);

	if (w->kq == FFKQ_NULL) {
		return;
	}

	switch (cmd) {
	case FMED_TASK_POST:
		if (1 == fftask_post(&w->taskmgr, task))
			if (0 != ffkqu_post(&w->kqpost, &w->evposted))
				syserrlog("%s", "ffkqu_post");
		break;
	case FMED_TASK_DEL:
		fftask_del(&w->taskmgr, task);
		break;
	default:
		FF_ASSERT(0);
	}
}

static int core_timer(fftimerqueue_node *t, int64 _interval, uint flags)
{
	struct worker *w = (void*)fmed->workers.ptr;
	int interval = _interval;
	uint period = ffmin((uint)ffabs(interval), TMR_INT);
	dbglog0("timer:%p  interval:%d  handler:%p  param:%p"
		, t, interval, t->func, t->param);

	if (w->kq == FF_BADFD) {
		dbglog0("timer's not ready", 0);
		return -1;
	}

	if (interval == 0) {
		fftimerqueue_remove(&w->timerq, t);
		return 0;
	}

	if (period < w->timer_period) {
		fftimer_stop(w->timer, w->kq);
		w->timer_period = 0;
		dbglog0("restarting kernel timer", 0);
	}

	if (w->timer_period == 0) {
		w->timer_kev.handler = on_timer;
		w->timer_kev.udata = w;
		if (0 != fftimer_start(w->timer, w->kq, &w->timer_kev, period)) {
			syserrlog("%s", "fftimer_start()");
			return -1;
		}
		w->timer_period = period;
		dbglog0("started kernel timer  interval:%u", period);
	}

	fftime now = fftime_monotonic();
	ffuint now_msec = now.sec*1000 + now.nsec/1000000;
	fftimerqueue_add(&w->timerq, t, now_msec, interval, t->func, t->param);
	return 0;
}

static void on_timer(void *param)
{
	struct worker *w = param;
	fftime now = fftime_monotonic();
	ffuint now_msec = now.sec*1000 + now.nsec/1000000;
	fftimerqueue_process(&w->timerq, now_msec);
	fftimer_consume(w->timer);
}

/** Worker's event loop */
static int FFTHDCALL work_loop(void *param)
{
	struct worker *w = param;
	w->id = ffthd_curid();
	ffkq_event *ents = ffmem_callocT(FMED_KQ_EVS, ffkq_event);
	if (ents == NULL)
		return -1;

	dbglog0("entering kqueue loop", 0);

	while (!FF_READONCE(fmed->stopped)) {

		uint nevents = ffkqu_wait(w->kq, ents, FMED_KQ_EVS, &fmed->kqutime);

		if ((int)nevents < 0) {
			if (fferr_last() != EINTR) {
				syserrlog("%s", ffkqu_wait_S);
				break;
			}
			continue;
		}

		for (uint i = 0;  i != nevents;  i++) {
			ffkq_event *ev = &ents[i];
			ffkev_call(ev);

			fftask_run(&w->taskmgr);
		}
	}

	ffmem_free(ents);
	dbglog0("leaving kqueue loop", 0);
	return 0;
}
