INCLUDE(BuildPlugin)

BUILD_PLUGIN(console Console.cpp)
TARGET_LINK_LIBRARIES(console hiir)
