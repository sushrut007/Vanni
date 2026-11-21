cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VAANI_VERSION_RAW)
string(STRIP "${VAANI_VERSION_RAW}" VAANI_VERSION_STRING)
project(Vaani VERSION ${VAANI_VERSION_STRING} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "Vaani is a Windows desktop application (configure on Windows).")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

set(VAANI_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/include" CACHE PATH
    "Whisper / GGML / PortAudio headers and import libs")

set(VAANI_SPEECH_SOURCES
    src/Utils/SingleInstanceGuard.cpp
    src/Utils/VaaniColdStart.cpp
    src/Utils/VaaniLogging.cpp
    src/Utils/WhisperModelPaths.cpp
    src/Models/VaaniModelCatalog.cpp
    src/Models/VaaniModelDownloader.cpp
    src/AudioProcessing/audioprocessor.cpp
    src/AudioProcessing/MicrophoneResolver.cpp
    src/AudioProcessing/AdaptiveProcessingManager.cpp
    src/AudioProcessing/AdvancedNoiseReduction.cpp
    src/SpeechRecognition/speechrecognitionmanager.cpp
    src/SpeechRecognition/WhisperRecognizer.cpp
    src/SpeechRecognition/WhisperWorker.cpp
    src/SpeechRecognition/SileroVad.cpp
    src/SpeechRecognition/VoiceActivityDetector.cpp
    src/SpeechRecognition/SpeechSliceAssembler.cpp
    src/SpeechRecognition/HoldUtteranceAssembler.cpp
    src/LanguageProcessing/LanguageDetector.cpp
    src/LanguageProcessing/LanguageRules.cpp
    src/LanguageProcessing/AntiRepetitionManager.cpp
    src/LanguageProcessing/OptimizedPostProcessor.cpp
    src/LanguageProcessing/MultilangPostProcessing.cpp
    src/Settings/VaaniSettingsStore.cpp
    src/Settings/VaaniSettingsDialog.cpp
    src/Settings/VaaniHotkeyCaptureField.cpp
    src/Settings/CaptureEndpointList.cpp
    src/Settings/VaaniSettingsStyle.cpp
    src/UI/VaaniListeningPill.cpp
    src/UI/VaaniDictationCapturePanel.cpp
    src/UI/VaaniOnboardingWizard.cpp
    src/UI/VaaniHomeWindow.cpp
    src/UI/VaaniToggleSwitch.cpp
    src/Storage/VaaniHistoryStore.cpp
    src/Utils/VaaniAutoStartManager.cpp
    src/Core/VaaniCoreController.cpp
    src/Tray/TrayController.cpp
    src/Injection/ExternalTextInjector.cpp
    src/Utils/UnicodeKeystrokes.cpp
    src/Input/VaaniHotkey.cpp
    src/Input/HoldToTalkTracker.cpp
    src/Input/GlobalPushToTalkMonitor.cpp
)

add_executable(Vaani WIN32
    src/main.cpp
    ${VAANI_SPEECH_SOURCES}
    resources/vaani_settings.qrc
    resources/vaani_app.qrc
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/vaani.rc")
    target_sources(Vaani PRIVATE resources/vaani.rc)
endif()

target_include_directories(Vaani PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${VAANI_DEPS_DIR}"
)

target_link_libraries(Vaani
    PRIVATE
        Qt6::Widgets
        Qt6::Network
)

target_compile_definitions(Vaani PRIVATE
    VAANI_VERSION_STR="${VAANI_VERSION_STRING}")

if(EXISTS "${VAANI_DEPS_DIR}/whisper.lib")
    target_link_libraries(Vaani PRIVATE
        "${VAANI_DEPS_DIR}/whisper.lib"
        "${VAANI_DEPS_DIR}/ggml.lib"
        "${VAANI_DEPS_DIR}/portaudio_x64.lib"
        psapi
        ole32
        oleaut32
    )
else()
    message(FATAL_ERROR "Missing speech dependencies under ${VAANI_DEPS_DIR}")
endif()

foreach(_dll whisper.dll ggml.dll ggml-base.dll ggml-cpu.dll portaudio_x64.dll)
    if(EXISTS "${VAANI_DEPS_DIR}/${_dll}")
        add_custom_command(TARGET Vaani POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VAANI_DEPS_DIR}/${_dll}"
                "$<TARGET_FILE_DIR:Vaani>"
            COMMENT "Copying ${_dll}")
    endif()
endforeach()

message(STATUS "")
message(STATUS "===========================================")
message(STATUS "Vaani ${PROJECT_VERSION}")
message(STATUS "  Dependencies: ${VAANI_DEPS_DIR}")
message(STATUS "===========================================")
message(STATUS "")