#pragma once

// The JUCE test targets (see CMakeLists.txt) only link Source/Core, Source/State and Source/Parameters sources, not
// Source/UI. Rather than editing the shared CMake file, the UI tests compile the UI kit translation units directly
// into the test executable by including them here (Source/UI/Views is intentionally excluded).
// If CMake later adds Source/UI/*.cpp to the JUCE test library, delete the includes below.

#include "UI/UiOptions.cpp"
#include "UI/Theme.cpp"
#include "UI/ParamFormat.cpp"
#include "UI/ParamContext.cpp"
#include "UI/MacroKnob.cpp"
#include "UI/ParamControls.cpp"
#include "UI/SectionPanel.cpp"
#include "UI/AdvancedTabs.cpp"
#include "UI/StatusBar.cpp"
#include "UI/Displays.cpp"
#include "UI/TrajectoryEditor.cpp"
#include "UI/HelpContent.cpp"
#include "UI/HelpPanel.cpp"
#include "UI/OptionsPanel.cpp"
#include "UI/OnboardingOverlay.cpp"
