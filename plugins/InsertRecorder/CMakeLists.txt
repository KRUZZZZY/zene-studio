INCLUDE(BuildPlugin)

BUILD_PLUGIN(insertrecorder InsertRecorder.cpp)
TARGET_LINK_LIBRARIES(insertrecorder SndFile::sndfile)
