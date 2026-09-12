set pagination off
set confirm off
handle SIGPIPE nostop noprint pass
run
echo \n===== STOPPED =====\n
info program
p $_siginfo._sifields._sigfault.si_addr
frame 0
p/x this
p/x (void*)((lmms::gui::TrackView*)this)->m_track
echo \n--- the outer vptr of the object the view still calls its model ---\n
p/x *(void**)((lmms::gui::TrackView*)this)->m_track
info symbol *(void**)((lmms::gui::TrackView*)this)->m_track
echo \n--- the string at that vptr-8, if it is a vtable ---\n
x/s *(char**)(*(void**)((lmms::gui::TrackView*)this)->m_track - 8) + 1
bt 6
quit
