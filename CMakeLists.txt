cmake_minimum_required(VERSION 3.22)

# ============================================================================
#  HighPrecisionEQ  -  Minimum-Phase IIR Precision EQ (OTODESK)
#  JUCE 8.0 系 / VST3 + Standalone
#  最適化: AVX2 SIMD, /fp:fast
# ============================================================================
project(HighPrecisionEQ VERSION 1.0.0 LANGUAGES C CXX)

# ----------------------------------------------------------------------------
#  C++20
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------------------------------------
#  JUCE の場所
# ----------------------------------------------------------------------------
set(JUCE_PATH "C:/JUCE" CACHE PATH "Path to the JUCE checkout")
add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)

# ----------------------------------------------------------------------------
#  プラグイン定義
# ----------------------------------------------------------------------------
juce_add_plugin(HighPrecisionEQ
    COMPANY_NAME             "OTODESK"
    PLUGIN_MANUFACTURER_CODE "Otdk"
    PLUGIN_CODE              "Hpeq"
    FORMATS                  VST3 Standalone
    PRODUCT_NAME             "HighPrecisionEQ"
    PLUGIN_DESCRIPTION       "Precision Minimum-Phase IIR EQ"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_AUTO_MANIFEST       FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
    VERSION                  "1.0.0"
)

# ----------------------------------------------------------------------------
#  バイナリ資産
# ----------------------------------------------------------------------------
juce_add_binary_data(HighPrecisionEQData
    SOURCES
        Source/Assets/logo.png
)

# ----------------------------------------------------------------------------
#  ソース (Sourceフォルダ経由)
# ----------------------------------------------------------------------------
target_sources(HighPrecisionEQ PRIVATE
    # --- プラグインコア ---
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp

    # --- DSP モジュール ---
    Source/DSP/MinimumPhaseEQ.h
    Source/DSP/MinimumPhaseEQ.cpp
    Source/DSP/SOSCoefficients.h
    Source/DSP/SPSCQueue.h
    Source/DSP/WaveformCatcher.h
    Source/DSP/WaveformCatcher.cpp
    Source/DSP/AnalyzerDSP.h
    Source/DSP/AnalyzerDSP.cpp

    # --- GUI モジュール ---
    Source/GUI/ArcDial.h
    Source/GUI/ArcDial.cpp
    Source/GUI/FreqResponseDisplay.h
    Source/GUI/FreqResponseDisplay.cpp
    Source/GUI/WaveformDisplay.h
    Source/GUI/WaveformDisplay.cpp
    Source/GUI/PhaseDisplay.h
    Source/GUI/PhaseDisplay.cpp
)

# ----------------------------------------------------------------------------
#  インクルードパス
# ----------------------------------------------------------------------------
target_include_directories(HighPrecisionEQ PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/GUI
)

# ----------------------------------------------------------------------------
#  JUCE モジュール
# ----------------------------------------------------------------------------
target_link_libraries(HighPrecisionEQ
    PRIVATE
        HighPrecisionEQData
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ----------------------------------------------------------------------------
#  コンパイル定義
# ----------------------------------------------------------------------------
target_compile_definitions(HighPrecisionEQ PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    HIGHPRECISIONEQ_VERSION="1.0.0"
)

# ----------------------------------------------------------------------------
#  JUCE ヘッダー生成
# ----------------------------------------------------------------------------
juce_generate_juce_header(HighPrecisionEQ)

# ----------------------------------------------------------------------------
#  MSVC コンパイルオプション (AVX2 SIMD 有効)
# ----------------------------------------------------------------------------
if(MSVC)
    target_compile_options(HighPrecisionEQ PRIVATE
        /arch:AVX2
        /fp:fast
        /utf-8
        /W3
        /wd4819
        $<$<CONFIG:Release>:/O2>
    )
endif()
