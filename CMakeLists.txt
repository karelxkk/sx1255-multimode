cmake_minimum_required(VERSION 3.20)
project(sx1255-multimode VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)
find_package(ALSA REQUIRED)

add_library(sx1255_core
    src/core/PluginRegistry.cpp
    src/core/SessionManager.cpp
    src/core/IQDispatcher.cpp
    src/dsp/Goertzel.cpp
    src/fm/FMDsp.cpp
    src/fm/FmSvxlinkBridge.cpp
    src/fm/PtyPtt.cpp
    src/fm/Resampler.cpp
    src/fm/SqlPty.cpp
    src/frontend/RadioFrontend.cpp
    src/frontend/NullSX1255.cpp
    src/frontend/SX1255SPI.cpp
    src/frontend/SX1255GPIO.cpp
    src/frontend/SX1255Hardware.cpp
    src/config/Config.cpp
    src/dstar/DStarDetector.cpp
    src/dstar/DStarGateway.cpp
    src/dstar/DStarTX.cpp
    src/debug/IQFileFormat.cpp
    src/debug/IQRecorder.cpp
    src/debug/IQReplay.cpp
    src/debug/AutomaticCapture.cpp
    src/diag/SystemStatistics.cpp
    src/diag/HttpStatusServer.cpp
    src/dataset/DatasetRunner.cpp
    src/m17/M17DSP.cpp
    src/m17/M17Synchronizer.cpp
    src/m17/M17FrameDecoder.cpp
    src/m17/M17Network.cpp
    src/monitor/HealthMonitor.cpp
    src/spectrum/FFT.cpp
    src/spectrum/SpectrumAnalyzer.cpp
    src/spectrum/WaterfallGenerator.cpp
    src/soak/SoakTester.cpp
    src/util/Logger.cpp
    src/util/SystemInfo.cpp
    src/web/WebDashboard.cpp
    src/plugins/FmPlugin.cpp
    src/plugins/DStarPlugin.cpp
    src/plugins/M17Plugin.cpp
)

target_include_directories(sx1255_core PUBLIC include)
target_compile_features(sx1255_core PUBLIC cxx_std_20)
target_link_libraries(sx1255_core PUBLIC Threads::Threads ALSA::ALSA)

add_executable(sx1255-multimode src/main.cpp)
target_link_libraries(sx1255-multimode PRIVATE sx1255_core)

add_executable(sx1255-soak src/tools/soak_main.cpp)
target_link_libraries(sx1255-soak PRIVATE sx1255_core)

add_executable(sx1255-bench src/tools/bench_main.cpp)
target_link_libraries(sx1255-bench PRIVATE sx1255_core)

add_executable(sx1255-fp-audit src/tools/fp_audit.cpp)
target_link_libraries(sx1255-fp-audit PRIVATE sx1255_core)

enable_testing()

# Offline analysis helper for recorded IQ files (not installed).
add_executable(m17_rx_dump tools/m17_rx_dump.cpp)
target_link_libraries(m17_rx_dump PRIVATE sx1255_core)

# Offline AFC comparison against the original m17-gateway Go implementation.
add_executable(m17_afc_audit tools/m17_afc_audit.cpp)
target_link_libraries(m17_afc_audit PRIVATE sx1255_core)

add_executable(m17_iq_audit tools/m17_iq_audit.cpp)
target_link_libraries(m17_iq_audit PRIVATE sx1255_core)

add_executable(m17-frame-audit tools/m17_frame_audit.cpp)
target_link_libraries(m17-frame-audit PRIVATE sx1255_core)
add_subdirectory(tests)

# ---------------------------------------------------------------------------
# Installation layout for the Debian package / system install.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS sx1255-multimode sx1255-soak sx1255-bench sx1255-fp-audit
                m17_rx_dump m17_iq_audit m17-frame-audit
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES sx1255-multimode.yaml.example
        DESTINATION /etc/sx1255-multimode
        RENAME sx1255-multimode.yaml)

install(FILES deploy/systemd/sx1255-multimode.service
        DESTINATION /lib/systemd/system)

install(PROGRAMS scripts/record_dataset.sh scripts/rc1_validate.sh scripts/deploy_test.sh
        DESTINATION ${CMAKE_INSTALL_DATADIR}/sx1255-multimode/scripts)

install(FILES README.md CHANGELOG.md RELEASE_NOTES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/sx1255-multimode)

install(FILES deploy/svxlink/svxlink.conf deploy/svxlink/README.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/sx1255-multimode/svxlink)
