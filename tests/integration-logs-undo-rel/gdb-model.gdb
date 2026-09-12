set pagination off
set confirm off
handle SIGPIPE nostop noprint pass
run
echo \n===== STOPPED =====\n
info program
p $_siginfo._sifields._sigfault.si_addr
frame 0
p/x this
echo \n--- TrackView::m_track (raw) ---\n
p/x (void*)((lmms::gui::TrackView*)this)->m_track
echo \n--- ModelView::m_model (what model()/castModel actually casts) ---\n
p/x (void*)((lmms::gui::ModelView*)this)->m_model
echo \n--- m_window ---\n
p/x (void*)((lmms::gui::InstrumentTrackView*)this)->m_window
bt 5
quit
