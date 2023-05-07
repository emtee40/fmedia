/**
 * fmedia/Android
 * 2022, Simon Zolin
 */

package com.github.stsaz.fmedia;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.PorterDuff;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.ImageButton;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.ToggleButton;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.app.AppCompatDelegate;
import androidx.appcompat.widget.Toolbar;
import androidx.appcompat.widget.SearchView;
import androidx.core.app.ActivityCompat;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import java.io.File;
import java.util.Calendar;
import java.util.Date;
import java.util.GregorianCalendar;

public class MainActivity extends AppCompatActivity {
	private static final String TAG = "fmedia.MainActivity";
	private static final int REQUEST_PERM_READ_STORAGE = 1;
	private static final int REQUEST_PERM_RECORD = 2;
	static final int REQUEST_STORAGE_ACCESS = 1;

	private Core core;
	private GUI gui;
	private Queue queue;
	private QueueNotify quenfy;
	private Track track;
	private Filter trk_nfy;
	private TrackCtl trackctl;
	private int total_dur_msec;
	private int state;

	private TrackHandle trec;

	private boolean view_explorer;
	private Explorer explorer;
	private RecyclerView list;
	private PlaylistAdapter pl_adapter;

	private TextView lbl_name;
	private ImageButton brec;
	private ImageButton bplay;
	private TextView lbl_pos;
	private SeekBar progs;
	private ToggleButton bexplorer;
	private ToggleButton bplist;
	private SearchView tfilter;
	private Toolbar toolbar;

	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		if (0 != init_mods())
			return;
		init_ui();
		init_system();

		list.setAdapter(pl_adapter);
		plist_show();

		if (gui.cur_path.isEmpty())
			gui.cur_path = core.storage_path;

		if (core.setts.rec_path.isEmpty())
			core.setts.rec_path = core.storage_path + "/Recordings";
	}

	protected void onStart() {
		super.onStart();
		core.dbglog(TAG, "onStart()");

		show_ui();

		// If already playing - get in sync
		track.filter_notify(trk_nfy);
	}

	protected void onResume() {
		super.onResume();
		if (core != null) {
			core.dbglog(TAG, "onResume()");
		}
	}

	protected void onStop() {
		if (core != null) {
			core.dbglog(TAG, "onStop()");
			queue.saveconf();
			if (!view_explorer)
				pl_leave();
			core.saveconf();
		}
		super.onStop();
	}

	public void onDestroy() {
		if (core != null) {
			core.dbglog(TAG, "onDestroy()");
			track.filter_rm(trk_nfy);
			trackctl.close();
			queue.nfy_rm(quenfy);
			core.close();
		}
		super.onDestroy();
	}

	public boolean onCreateOptionsMenu(Menu menu) {
		toolbar.inflateMenu(R.menu.menu);
		toolbar.setOnMenuItemClickListener(this::onOptionsItemSelected);
		return true;
	}

	public boolean onOptionsItemSelected(@NonNull MenuItem item) {
		switch (item.getItemId()) {
			case R.id.action_settings:
				startActivity(new Intent(this, SettingsActivity.class));
				return true;

			case R.id.action_list_rm:
				list_rm();
				return true;

			case R.id.action_file_del:
				file_del_cur();
				return true;

			case R.id.action_file_move:
				file_move_cur();
				return true;

			case R.id.action_file_tags_show:
				startActivity(new Intent(this, TagsActivity.class));
				return true;

			case R.id.action_file_convert:
				startActivity(new Intent(this, ConvertActivity.class)
						.putExtra("iname", track.cur_url())
						.putExtra("length", total_dur_msec / 1000 + 1));
				return true;

			case R.id.action_list_add:
				startActivity(new Intent(this, AddURLActivity.class));
				return true;

			case R.id.action_list_clear:
				queue.clear();
				return true;

			case R.id.action_list_save:
				list_save();
				return true;

			case R.id.action_list_showcur: {
				plist_click();
				int pos = queue.cur();
				if (pos >= 0)
					list.scrollToPosition(queue.cur());
				return true;
			}

			case R.id.action_list_switch: {
				int qi = queue.switch_list();
				if (view_explorer)
					plist_click();
				else
					list_update();
				core.gui().msg_show(this, "Switched to L%d", qi+1);
				return true;
			}

			case R.id.action_list_l2add:
				if (queue.l2_add_cur())
					core.gui().msg_show(this, "Added 1 item to L2");
				return true;

			case R.id.action_file_showcur: {
				String fn = track.cur_url();
				if (fn.isEmpty())
					return true;
				gui.cur_path = new File(fn).getParent();
				if (!view_explorer) {
					explorer_click();
				} else {
					explorer.fill();
					pl_adapter.view_explorer = true;
					list_update();
				}
				int pos = explorer.file_idx(fn);
				if (pos >= 0)
					list.scrollToPosition(pos);
				return true;
			}

			case R.id.action_about:
				startActivity(new Intent(this, AboutActivity.class));
				return true;
		}
		return super.onOptionsItemSelected(item);
	}

	/**
	 * Called by OS with the result of requestPermissions().
	 */
	public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
		super.onRequestPermissionsResult(requestCode, permissions, grantResults);
		if (grantResults.length != 0)
			core.dbglog(TAG, "onRequestPermissionsResult: %d: %d", requestCode, grantResults[0]);
	}

	public void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);

		if (resultCode != RESULT_OK) {
			if (resultCode != RESULT_CANCELED)
				core.errlog(TAG, "onActivityResult: requestCode:%d resultCode:%d", requestCode, resultCode);
			return;
		}

		switch (requestCode) {
			case REQUEST_STORAGE_ACCESS:
				if (Environment.isExternalStorageManager()) {
				}
				break;
		}
	}

	/**
	 * Request system permissions
	 */
	private void init_system() {
		String[] perms = new String[]{
				Manifest.permission.READ_EXTERNAL_STORAGE,
				Manifest.permission.WRITE_EXTERNAL_STORAGE,
		};
		for (String perm : perms) {
			if (ActivityCompat.checkSelfPermission(this, perm) != PackageManager.PERMISSION_GRANTED) {
				core.dbglog(TAG, "ActivityCompat.requestPermissions: %s", perm);
				ActivityCompat.requestPermissions(this, perms, REQUEST_PERM_READ_STORAGE);
				break;
			}
		}
	}

	private boolean user_ask_record() {
		String perm = Manifest.permission.RECORD_AUDIO;
		if (ActivityCompat.checkSelfPermission(this, perm) != PackageManager.PERMISSION_GRANTED) {
			core.dbglog(TAG, "ActivityCompat.requestPermissions: %s", perm);
			ActivityCompat.requestPermissions(this, new String[]{perm}, REQUEST_PERM_RECORD);
			return false;
		}
		return true;
	}

	/**
	 * Initialize core and modules
	 */
	private int init_mods() {
		core = Core.init_once(getApplicationContext());
		if (core == null)
			return -1;
		core.dbglog(TAG, "init_mods()");

		gui = core.gui();
		queue = core.queue();
		quenfy = new QueueNotify() {
			public void on_change(int how, int pos) {
				if (view_explorer) return;

				pl_adapter.on_change(how, pos);
			}
		};
		queue.nfy_add(quenfy);
		track = core.track();
		trk_nfy = new Filter() {
			public int open(TrackHandle t) {
				return new_track(t);
			}

			public void close(TrackHandle t) {
				close_track(t);
			}

			public int process(TrackHandle t) {
				return update_track(t);
			}
		};
		track.filter_add(trk_nfy);
		trackctl = new TrackCtl(core, this);
		trackctl.connect();
		trec = track.trec;
		return 0;
	}

	/**
	 * Set UI objects and register event handlers
	 */
	private void init_ui() {
		setContentView(R.layout.main);

		toolbar = findViewById(R.id.toolbar);
		setSupportActionBar(toolbar);

		explorer = new Explorer(core, this);
		brec = findViewById(R.id.brec);
		brec.setOnClickListener((v) -> rec_click());

		bplay = findViewById(R.id.bplay);
		bplay.setOnClickListener((v) -> play_pause_click());

		findViewById(R.id.bnext).setOnClickListener((v) -> trackctl.next());

		findViewById(R.id.bprev).setOnClickListener((v) -> trackctl.prev());

		bexplorer = findViewById(R.id.bexplorer);
		bexplorer.setOnClickListener((v) -> explorer_click());

		bplist = findViewById(R.id.bplaylist);
		bplist.setOnClickListener((v) -> plist_click());
		bplist.setChecked(true);

		lbl_name = findViewById(R.id.lname);
		lbl_pos = findViewById(R.id.lpos);

		progs = findViewById(R.id.seekbar);
		progs.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
			int val; // last value

			public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
				if (fromUser)
					val = progress;
			}

			public void onStartTrackingTouch(SeekBar seekBar) {
				val = -1;
			}

			public void onStopTrackingTouch(SeekBar seekBar) {
				if (val != -1)
					seek(val);
			}
		});

		tfilter = findViewById(R.id.tfilter);
		tfilter.setOnQueryTextListener(new SearchView.OnQueryTextListener() {
			public boolean onQueryTextSubmit(String query) {
				return true;
			}

			public boolean onQueryTextChange(String newText) {
				plist_filter(newText);
				return true;
			}
		});

		list = findViewById(R.id.list);
		list.setLayoutManager(new LinearLayoutManager(this));
		pl_adapter = new PlaylistAdapter(this, explorer);

		gui.cur_activity = this;
	}

	private void show_ui() {
		int mode = AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM;
		if (core.gui().theme == GUI.THM_DARK)
			mode = AppCompatDelegate.MODE_NIGHT_YES;
		AppCompatDelegate.setDefaultNightMode(mode);

		int v = View.VISIBLE;
		if (gui.record_hide)
			v = View.INVISIBLE;
		brec.setVisibility(v);

		v = View.VISIBLE;
		if (gui.filter_hide)
			v = View.INVISIBLE;
		tfilter.setVisibility(v);

		if (trec != null) {
			rec_state_set(true);
		}
		state(STATE_DEF);
	}

	private void rec_state_set(boolean active) {
		int res = R.color.text;
		if (active)
			res = R.color.recording;
		int color = getResources().getColor(res);
		if (Build.VERSION.SDK_INT >= 21) {
			brec.setImageTintMode(PorterDuff.Mode.SRC_IN);
			brec.setImageTintList(ColorStateList.valueOf(color));
		}
	}

	private void rec_click() {
		if (trec == null) {
			rec_start();
		} else {
			track.record_stop(trec);
			trec = null;
			rec_state_set(false);
			core.gui().msg_show(this, "Finished recording");
		}
	}

	private void play_pause_click() {
		if (track.state() == Track.STATE_PLAYING) {
			trackctl.pause();
		} else {
			trackctl.unpause();
		}
	}

	void explorer_click() {
		bexplorer.setChecked(true);
		if (view_explorer) return;

		pl_leave();
		view_explorer = true;
		bplist.setChecked(false);
		tfilter.setVisibility(View.INVISIBLE);

		explorer.fill();
		pl_adapter.view_explorer = true;
		list_update();
	}

	void plist_click() {
		bplist.setChecked(true);
		if (!view_explorer) return;

		view_explorer = false;
		bexplorer.setChecked(false);
		if (!gui.filter_hide)
			tfilter.setVisibility(View.VISIBLE);

		pl_adapter.view_explorer = false;
		list_update();
		plist_show();
	}

	/**
	 * Delete file and update view
	 */
	private void file_del(int pos, String fn) {
		if (!core.setts.file_del) {
			String e = core.fmedia.trash(core.setts.trash_dir, fn);
			if (!e.isEmpty()) {
				core.errlog(TAG, "Can't trash file %s: %s", fn, e);
				return;
			}
			gui.msg_show(this, "Moved file to Trash directory");
		} else {
			if (!core.file_delete(fn))
				return;
			gui.msg_show(this, "Deleted file");
		}
		queue.remove(pos);
	}

	/**
	 * Ask confirmation before deleting the currently playing file from storage
	 */
	private void file_del_cur() {
		int pos = queue.cur();
		if (pos < 0)
			return;
		String fn = queue.get(pos);

		AlertDialog.Builder b = new AlertDialog.Builder(this);
		b.setIcon(android.R.drawable.ic_dialog_alert);
		b.setTitle("File Delete");
		String msg, btn;
		if (core.setts.file_del) {
			msg = String.format("Delete file from storage: %s ?", fn);
			btn = "Delete";
		} else {
			msg = String.format("Move file to Trash: %s ?", fn);
			btn = "Trash";
		}
		b.setMessage(msg);
		b.setPositiveButton(btn, (dialog, which) -> file_del(pos, fn));
		b.setNegativeButton("Cancel", null);
		b.show();
	}

	private void file_move_cur() {
		if (core.setts.quick_move_dir.isEmpty()) {
			core.errlog(TAG, "Please set move-directory in Settings");
			return;
		}

		int pos = queue.cur();
		if (pos < 0)
			return;
		String fn = queue.get(pos);

		String e = core.fmedia.fileMove(fn, core.setts.quick_move_dir);
		if (!e.isEmpty()) {
			core.errlog(TAG, "file move: %s", e);
			return;
		}

		gui.msg_show(this, "Moved file to %s", core.setts.quick_move_dir);
	}

	void explorer_event(String fn, int flags) {
		if (fn == null) {
			list.setAdapter(pl_adapter);
			return;
		}

		int n = queue.count();
		String[] ents = new String[1];
		ents[0] = fn;
		queue.addmany(ents, flags);
		core.dbglog(TAG, "added %d items", ents.length);
		gui.msg_show(this, "Added %d items to playlist", ents.length);
		if (flags == Queue.ADD)
			queue.play(n);
	}

	private void list_update() {
		pl_adapter.on_change(0, -1);
	}

	private void plist_show() {
		list.scrollToPosition(gui.list_pos);
	}

	/** Called when we're leaving the playlist tab */
	void pl_leave() {
		LinearLayoutManager llm = (LinearLayoutManager) list.getLayoutManager();
		gui.list_pos = llm.findLastCompletelyVisibleItemPosition();
	}

	private void plist_filter(String filter) {
		core.dbglog(TAG, "list_filter: %s", filter);
		queue.filter(filter);
		list_update();
	}

	/**
	 * Remove currently playing track from playlist
	 */
	private void list_rm() {
		int pos = queue.cur();
		if (pos < 0)
			return;
		queue.remove(pos);
		gui.msg_show(this, "Removed 1 entry");
	}

	/**
	 * Show dialog for saving playlist file
	 */
	private void list_save() {
		startActivity(new Intent(this, ListSaveActivity.class));
	}

	/**
	 * Start recording
	 */
	private void rec_start() {
		if (!user_ask_record())
			return;

		core.dir_make(core.setts.rec_path);
		Date d = new Date();
		Calendar cal = new GregorianCalendar();
		cal.setTime(d);
		int dt[] = {
				cal.get(Calendar.YEAR),
				cal.get(Calendar.MONTH) + 1,
				cal.get(Calendar.DAY_OF_MONTH),
				cal.get(Calendar.HOUR_OF_DAY),
				cal.get(Calendar.MINUTE),
				cal.get(Calendar.SECOND),
		};
		String fname = String.format("%s/rec_%04d%02d%02d_%02d%02d%02d.%s"
				, core.setts.rec_path, dt[0], dt[1], dt[2], dt[3], dt[4], dt[5]
				, core.setts.rec_fmt);
		trec = track.rec_start(fname, () -> {
				Handler mloop = new Handler(Looper.getMainLooper());
				mloop.post(this::rec_click);
			});
		if (trec == null)
			return;
		rec_state_set(true);
		core.gui().msg_show(this, "Started recording");
	}

	/**
	 * UI event from seek bar
	 */
	private void seek(int percent) {
		trackctl.seek(percent * total_dur_msec / 100);
	}

	private static final int STATE_DEF = 1;
	private static final int STATE_PLAYING = 2;
	private static final int STATE_PAUSED = 3;

	private void state(int st) {
		if (st == state)
			return;
		String fm = "φfmedia";
		switch (st) {
			case STATE_DEF:
				getSupportActionBar().setTitle(fm);
				bplay.setImageResource(R.drawable.ic_play);
				break;

			case STATE_PLAYING:
				getSupportActionBar().setTitle(String.format("%s [Playing]", fm));
				bplay.setImageResource(R.drawable.ic_pause);
				break;

			case STATE_PAUSED:
				getSupportActionBar().setTitle(String.format("%s [Paused]", fm));
				bplay.setImageResource(R.drawable.ic_play);
				break;
		}
		core.dbglog(TAG, "state: %d -> %d", state, st);
		state = st;
	}

	/**
	 * Called by Track when a new track is initialized
	 */
	private int new_track(TrackHandle t) {
		String title = t.name;
		if (core.gui().ainfo_in_title && !t.info.isEmpty())
			title = String.format("%s [%s]", t.name, t.info);
		lbl_name.setText(title);

		progs.setProgress(0);
		if (t.state == Track.STATE_PAUSED) {
			state(STATE_PAUSED);
		} else {
			state(STATE_PLAYING);
		}
		return 0;
	}

	/**
	 * Called by Track after a track is finished
	 */
	private void close_track(TrackHandle t) {
		lbl_name.setText("");
		lbl_pos.setText("");
		progs.setProgress(0);
		state(STATE_DEF);
	}

	/**
	 * Called by Track during playback
	 */
	private int update_track(TrackHandle t) {
		core.dbglog(TAG, "update_track: state:%d pos:%d", t.state, t.pos_msec);
		switch (t.state) {
			case Track.STATE_PAUSED:
				state(STATE_PAUSED);
				break;

			case Track.STATE_PLAYING:
				state(STATE_PLAYING);
				break;
		}

		int pos = t.pos_msec / 1000;
		total_dur_msec = t.time_total_msec;
		int dur = t.time_total_msec / 1000;
		int progress = 0;
		if (dur != 0)
			progress = pos * 100 / dur;
		progs.setProgress(progress);
		String s = String.format("%d:%02d / %d:%02d"
				, pos / 60, pos % 60, dur / 60, dur % 60);
		lbl_pos.setText(s);
		return 0;
	}
}
