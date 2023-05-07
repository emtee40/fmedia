/**
 * fmedia/Android
 * 2023, Simon Zolin
 */

package com.github.stsaz.fmedia;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

class PlaylistViewHolder extends RecyclerView.ViewHolder
		implements View.OnClickListener, View.OnLongClickListener {

	private final PlaylistAdapter adapter;
	final TextView text;

	PlaylistViewHolder(PlaylistAdapter adapter, View itemView) {
		super(itemView);
		this.adapter = adapter;
		text = itemView.findViewById(R.id.list2_text);
		itemView.setClickable(true);
		itemView.setOnClickListener(this);
		itemView.setOnLongClickListener(this);
	}

	public void onClick(View v) {
		adapter.on_event(0, getAdapterPosition());
	}

	public boolean onLongClick(View v) {
		adapter.on_event(PlaylistAdapter.EV_LONGCLICK, getAdapterPosition());
		return true;
	}
}

class PlaylistAdapter extends RecyclerView.Adapter<PlaylistViewHolder> {

	private final Core core;
	private final Queue queue;
	private final LayoutInflater inflater;
	boolean view_explorer;
	private final Explorer explorer;

	PlaylistAdapter(Context ctx, Explorer explorer) {
		core = Core.init_once(null);
		queue = core.queue();
		this.explorer = explorer;
		inflater = LayoutInflater.from(ctx);
	}

	public PlaylistViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
		View v = inflater.inflate(R.layout.list_row2, parent, false);
		return new PlaylistViewHolder(this, v);
	}

	public void onBindViewHolder(@NonNull PlaylistViewHolder holder, int position) {
		String s;
		if (!view_explorer) {
			QueueItemInfo qi = queue.getInfo(position);
			if (!qi.title.isEmpty()) {
				s = String.format("%d. %s - %s [%d:%02d]"
					, position + 1, qi.artist, qi.title
					, qi.length_sec / 60, qi.length_sec % 60);
			} else {
				s = String.format("%d. %s", position + 1, Util.path_split2(qi.url)[1]);
			}
		} else {
			s = explorer.get(position);
		}
		holder.text.setText(s);
	}

	public int getItemCount() {
		if (view_explorer)
			return explorer.count();

		return queue.count();
	}

	static final int EV_LONGCLICK = 1;

	void on_event(int ev, int i) {
		if (view_explorer) {
			explorer.event(ev, i);
			return;
		}

		if (ev == EV_LONGCLICK)
			return;

		queue.play(i);
	}

	void on_change(int how, int pos) {
		if (pos < 0)
			notifyDataSetChanged();
		else if (how == QueueNotify.UPDATE)
			notifyItemChanged(pos);
		else if (how == QueueNotify.ADDED)
			notifyItemInserted(pos);
		else if (how == QueueNotify.REMOVED)
			notifyItemRemoved(pos);
	}
}
