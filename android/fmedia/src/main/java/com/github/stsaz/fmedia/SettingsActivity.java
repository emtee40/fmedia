/**
 * fmedia/Android
 * 2022, Simon Zolin
 */

package com.github.stsaz.fmedia;

import android.os.Bundle;
import android.widget.TextView;

import androidx.appcompat.app.ActionBar;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.SwitchCompat;

public class SettingsActivity extends AppCompatActivity {
	private static final String TAG = "fmedia.SettingsActivity";
	private Core core;
	private SwitchCompat brandom, brepeat, bfilter_hide, brec_hide, bsvc_notif_disable, bfile_del;
	private SwitchCompat bnotags, blist_rm_on_next, blist_rm_on_err, bdark;
	private TextView tdata_dir, trecdir, tbitrate, ttrash_dir, tautoskip, tcodepage;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.settings);
		ActionBar actionBar = getSupportActionBar();
		if (actionBar != null)
			actionBar.setDisplayHomeAsUpEnabled(true);

		core = Core.getInstance();
		setup();
		load();
	}

	@Override
	protected void onPause() {
		core.dbglog(TAG, "onPause");
		save();
		super.onPause();
	}

	@Override
	protected void onDestroy() {
		core.dbglog(TAG, "onDestroy");
		core.unref();
		super.onDestroy();
	}

	private void setup() {
		// Interface
		bdark = findViewById(R.id.bdark);
		bdark.setChecked(core.gui().theme == GUI.THM_DARK);

		bfilter_hide = findViewById(R.id.bshowfilter);
		bfilter_hide.setChecked(core.gui().filter_hide);

		brec_hide = findViewById(R.id.bshowrec);
		brec_hide.setChecked(core.gui().record_hide);

		bsvc_notif_disable = findViewById(R.id.bsvc_notif_disable);
		bsvc_notif_disable.setChecked(core.setts.svc_notification_disable);

		// Playback
		brandom = findViewById(R.id.brandom);
		brandom.setChecked(core.queue().is_random());

		brepeat = findViewById(R.id.brepeat);
		brepeat.setChecked(core.queue().is_repeat());

		bnotags = findViewById(R.id.bnotags);
		bnotags.setChecked(core.setts.no_tags);

		blist_rm_on_next = findViewById(R.id.blist_rm_on_next);
		blist_rm_on_next.setChecked(core.setts.list_rm_on_next);
		blist_rm_on_err = findViewById(R.id.blist_rm_on_err);

		tcodepage = findViewById(R.id.tcodepage);
		tcodepage.setText(core.setts.codepage);

		tautoskip = findViewById(R.id.tautoskip);
		tautoskip.setText(Integer.toString(core.queue().autoskip_msec / 1000));

		// Operation
		tdata_dir = findViewById(R.id.tdata_dir);
		tdata_dir.setText(core.setts.pub_data_dir);

		ttrash_dir = findViewById(R.id.ttrash_dir);
		ttrash_dir.setText(core.setts.trash_dir);

		bfile_del = findViewById(R.id.bfile_del);
		bfile_del.setChecked(core.setts.file_del);

		// Recording
		trecdir = findViewById(R.id.trecdir);
		trecdir.setText(core.setts.rec_path);

		tbitrate = findViewById(R.id.tbitrate);
		tbitrate.setText(Integer.toString(core.setts.enc_bitrate));
	}

	private void load() {
		blist_rm_on_err.setChecked(core.setts.qu_rm_on_err);
	}

	private void save() {
		core.queue().random(brandom.isChecked());
		core.queue().repeat(brepeat.isChecked());
		core.setts.no_tags = bnotags.isChecked();
		core.setts.list_rm_on_next = blist_rm_on_next.isChecked();
		core.setts.qu_rm_on_err = blist_rm_on_err.isChecked();
		core.setts.set_codepage(tcodepage.getText().toString());
		core.fmedia.setCodepage(core.setts.codepage);
		core.queue().autoskip_msec = core.str_to_uint(tautoskip.getText().toString(), 0) * 1000;

		String s = tdata_dir.getText().toString();
		if (s.isEmpty())
			s = core.storage_path + "/fmedia";
		core.setts.pub_data_dir = s;

		core.setts.svc_notification_disable = bsvc_notif_disable.isChecked();
		core.setts.trash_dir = ttrash_dir.getText().toString();
		core.setts.file_del = bfile_del.isChecked();

		int i = GUI.THM_DEF;
		if (bdark.isChecked())
			i = GUI.THM_DARK;
		core.gui().theme = i;

		core.gui().filter_hide = bfilter_hide.isChecked();
		core.gui().record_hide = brec_hide.isChecked();
		core.setts.rec_path = trecdir.getText().toString();
		core.setts.enc_bitrate = core.str_to_uint(tbitrate.getText().toString(), core.setts.enc_bitrate);
	}
}
