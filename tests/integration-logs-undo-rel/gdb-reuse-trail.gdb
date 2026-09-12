set pagination off
set confirm off
handle SIGPIPE nostop noprint pass

break lmms::InstrumentTrack::InstrumentTrack(lmms::TrackContainer*)
commands
silent
printf "CTOR InstrumentTrack %p\n", this
continue
end

break lmms::InstrumentTrack::~InstrumentTrack()
commands
silent
printf "DTOR InstrumentTrack %p\n", this
continue
end

break lmms::gui::InstrumentTrackView::~InstrumentTrackView()
commands
silent
printf ">>> DEFERRED-VIEW-DTOR this=%p track=%p window=%p\n", this, (void*)((lmms::gui::TrackView*)this)->m_track, (void*)((lmms::gui::InstrumentTrackView*)this)->m_window
continue
end

run
echo \n===== STOPPED =====\n
info program
p $_siginfo._sifields._sigfault.si_addr
bt 6
quit
