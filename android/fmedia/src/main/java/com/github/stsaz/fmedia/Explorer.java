/**
 * fmedia/Android
 * 2022, Simon Zolin
 */

package com.github.stsaz.fmedia;

import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;

import androidx.core.app.ActivityCompat;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;

class Explorer {
	private static final String TAG = "fmedia.Explorer";
	private final Core core;
	private final Queue queue;
	private final GUI gui;
	private final MainActivity main;

	private String[] fns; // file names
	private boolean updir; // "UP" directory link is shown
	private boolean uproot;
	private int ndirs; // number of directories shown

	Explorer(Core core, MainActivity main) {
		this.core = core;
		this.main = main;
		queue = core.queue();
		gui = core.gui();
	}

	void list_click(int pos) {
		if (pos == 0)
			return; // click on our current directory path
		pos--;

		if (uproot) {
			if (pos == 0) {
				main.list_set_data(list_show_root());
				return;
			}
			pos--;
		}

		if (pos < ndirs) {
			main.list_set_data(list_show(fns[pos]));
			return;
		}

		String[] pl = new String[1];
		pl[0] = fns[pos];
		main.pl_set(pl, Queue.ADD);
	}

	/**
	 * Read directory contents and update listview
	 */
	String[] list_show(String path) {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
			if (!Environment.isExternalStorageManager()) {
				Intent it = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION
						, Uri.parse("package:" + BuildConfig.APPLICATION_ID));
				ActivityCompat.startActivityForResult(main, it, MainActivity.REQUEST_STORAGE_ACCESS, null);
			}
		}

		core.dbglog(TAG, "list_show: %s", path);
		ArrayList<String> fnames = new ArrayList<>();
		ArrayList<String> names = new ArrayList<>();
		boolean updir = false;
		boolean uproot = false;
		int ndirs = 0;
		try {
			File fdir = new File(path);
			File[] files = fdir.listFiles();

			names.add(String.format("[%s]", path));

			/* Prevent from going upper than sdcard because
			 it may be impossible to come back (due to file permissions) */
			if (core.array_ifind(core.storage_paths, path) >= 0) {
				names.add("<UP>");
				updir = true;
				uproot = true;
			} else {
				String parent = fdir.getParent();
				if (parent != null) {
					fnames.add(parent);
					names.add("<UP>");
					updir = true;
					ndirs++;
				}
			}

			if (files != null) {
				// sort file names (directories first)
				Arrays.sort(files, (File f1, File f2) -> {
					if (f1.isDirectory() == f2.isDirectory())
						return f1.getName().compareToIgnoreCase(f2.getName());
					if (f1.isDirectory())
						return -1;
					return 1;
				});

				for (File f : files) {
					String s;
					if (f.isDirectory()) {
						s = "<DIR> ";
						s += f.getName();
						names.add(s);
						fnames.add(f.getPath());
						ndirs++;
						continue;
					}

					s = f.getName();
					names.add(s);
					fnames.add(f.getPath());
				}
			}
		} catch (Exception e) {
			core.errlog(TAG, "list_show: %s", e);
			fns = new String[0];
			return new String[]{};
		}

		fns = fnames.toArray(new String[0]);
		gui.cur_path = path;
		String[] anames = names.toArray(new String[0]);
		this.updir = updir;
		this.uproot = uproot;
		this.ndirs = ndirs;
		core.dbglog(TAG, "added %d files", fns.length - 1);
		return anames;
	}

	/**
	 * Show the list of all available storage directories
	 */
	String[] list_show_root() {
		fns = core.storage_paths;
		gui.cur_path = "";
		updir = false;
		uproot = false;
		ndirs = fns.length;

		ArrayList<String> names = new ArrayList<>();
		names.add("[Storage directories]");
		names.addAll(Arrays.asList(fns));
		return names.toArray(new String[0]);
	}

	/**
	 * UI event on listview long click.
	 * Add files to the playlist.  Recursively add directory contents.
	 */
	boolean add_files_r(int pos) {
		if (pos == 0)
			return false; // long click on our current directory path
		pos--;

		if (uproot)
			pos--;

		if (pos == 0 && updir)
			return false; // no action for a long click on "<UP>"

		String[] list = new String[1];
		list[0] = fns[pos];
		main.pl_set(list, Queue.ADD_RECURSE);
		return true;
	}

	int show_file(String fn) {
		gui.cur_path = new File(fn).getParent();
		main.explorer_click();

		for (int i = 0; i != fns.length; i++) {
			if (fns[i].equalsIgnoreCase(fn)) {
				return i;
			}
		}
		return -1;
	}
}
