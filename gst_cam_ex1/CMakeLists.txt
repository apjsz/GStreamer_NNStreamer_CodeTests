cmake_minimum_required(VERSION 3.25)
project(gst_cam_ex1
	VERSION 0.1.0
	DESCRIPTION "Split the outputs the webcam in two windows."
	LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig) 
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-video-1.0 gstreamer-audio-1.0)
include_directories(
	${GLIB_INCLUDEDIRS}
	${GSTREAMER_INCLUDE_DIRS}
)
	
link_directories(
	${GLIB_LIBRARY_DIRS}
	${GSTREAMER_LIBRARY_DIRS}
)	
	
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(${PROJECT_NAME}
	src/main.cpp
)

# target_link_libraries(${PROJECT_NAME} ${GSTREAMER_LIBRARIES} ${OpenCV_LIBS})

target_link_libraries(${PROJECT_NAME} ${GSTREAMER_LIBRARIES})

