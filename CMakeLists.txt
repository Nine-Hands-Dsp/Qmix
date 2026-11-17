cmake_minimum_required(VERSION 3.22)

# macOS : binaire universel (Apple Silicon + Intel), macOS 10.13 minimum (avant project())
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

project(Qmix VERSION 1.1.0)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.15
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(QmixAssets SOURCES
  logo/NineHandsLogoWhite.svg
  fonts/Poppins-Regular.ttf fonts/Poppins-Medium.ttf fonts/Poppins-Bold.ttf)

juce_add_plugin(Qmix
  COMPANY_NAME "NINE HANDS | DSP"
  PLUGIN_MANUFACTURER_CODE Wiga
  PLUGIN_CODE WgQm
  FORMATS VST3
  VST3_CATEGORIES Fx EQ
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  PRODUCT_NAME "Qmix"
  BUNDLE_ID "com.ninehandsdsp.qmix"
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Qmix PRIVATE PluginProcessor.cpp PluginEditor.cpp)

# verrou de taille de la fenetre du DAW (jamais sous 60 %, jamais au-dela de 150 %)
if(APPLE)
  target_sources(Qmix PRIVATE WindowMinSize.mm)
  target_link_libraries(Qmix PRIVATE "-framework Cocoa")
else()
  target_sources(Qmix PRIVATE WindowMinSize.cpp)
endif()
if(WIN32)
  target_link_libraries(Qmix PRIVATE comctl32 user32)
endif()

target_compile_definitions(Qmix PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Qmix
  PRIVATE
    QmixAssets
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
