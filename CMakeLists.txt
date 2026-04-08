cmake_minimum_required(VERSION 3.10)
project(PaperVision_System)

set(CMAKE_CXX_STANDARD 17)
# Pylon Paths
set(PYLON_ROOT /opt/pylon)
include_directories(${PYLON_ROOT}/include)
link_directories(${PYLON_ROOT}/lib) # Ubuntu X86_64 uses lib for Pylon 6.x

# Find Packages
find_package(OpenCV REQUIRED)
find_package(Qt5 COMPONENTS Core Widgets Gui Concurrent Svg REQUIRED)

# Pylon - We might need a custom finder or just skip if mocking
# list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
# find_package(Pylon) 
# If mocking, we might not need Pylon headers if we hide them behind the implementation.

# Init Sources
set(SOURCES
src/main.cpp
src/core/CameraManager.cpp
src/core/EventController.cpp
src/core/EventDatabase.cpp
src/core/VideoStreamReader.cpp
src/processing/ImageBuffer.cpp
src/processing/DefectDetector.cpp
src/processing/VideoEncoder.cpp
src/gui/MainWindow.cpp
src/gui/LiveDashboard.cpp
src/gui/AnalysisView.cpp
src/gui/DetailView.cpp
src/gui/widgets/CameraWidget.cpp
src/gui/widgets/AnalysisVideoWidget.cpp
src/gui/ConfigDialog.cpp
src/config/CameraConfig.cpp
src/gui/widgets/ToggleSwitch.cpp
src/gui/widgets/IconManager.cpp
src/gui/widgets/CameraCard.cpp
src/gui/widgets/NetworkSummaryHeader.cpp
src/gui/widgets/DeleteConfirmationDialog.cpp
)

# Header files for Qt MOC
set(HEADERS
src/gui/MainWindow.h
src/gui/LiveDashboard.h
src/gui/AnalysisView.h
src/gui/DetailView.h
src/gui/ConfigDialog.h
src/gui/widgets/CameraWidget.h
src/gui/widgets/AnalysisVideoWidget.h
src/gui/widgets/ToggleSwitch.h
src/gui/widgets/IconManager.h
src/gui/widgets/CameraCard.h
src/gui/widgets/NetworkSummaryHeader.h
src/gui/widgets/DeleteConfirmationDialog.h
)

# Include Directories
include_directories(
    src
    ${OpenCV_INCLUDE_DIRS}
)

# Qt Mocking
qt5_wrap_cpp(MOC_SOURCES ${HEADERS})
qt5_add_resources(RESOURCES resources.qrc)

# Executable
add_executable(PaperVision_App ${SOURCES} ${MOC_SOURCES} ${RESOURCES})

# Link Libraries
target_link_libraries(PaperVision_App
    ${OpenCV_LIBS}
    Qt5::Core Qt5::Widgets Qt5::Gui Qt5::Concurrent Qt5::Svg
    pthread
    pylonbase
    pylonutility
    GenApi_gcc_v3_1_Basler_pylon
    GCBase_gcc_v3_1_Basler_pylon
)
# If not mocking, would link Pylon here:
# target_link_libraries(PaperVision_App ${Pylon_LIBRARIES})
