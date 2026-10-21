#pragma once

#include "Theme.h"

namespace snaral::ui
{
/// Collapsible titled group (ui-layout.md: "Use collapsible groups when a tab contains multiple related control
/// groups; collapsed state remains visible and keyboard-operable").
///
/// The header is a real toggle button (Space/Enter, accessible as an expandable section). A chevron AND the word
/// "expanded/collapsed" in the accessible text carry the state, not colour.
/// The panel does not own `content`. If the content implements PreferredHeightProvider the panel reports the height
/// it needs for a given width (SectionPanel itself is a PreferredHeightProvider so panels can be stacked in
/// scroll containers).
class SectionPanel : public juce::Component,
                     public PreferredHeightProvider
{
public:
    explicit SectionPanel (const juce::String& title, juce::Component* content = nullptr);
    ~SectionPanel() override;

    static constexpr int headerHeight = 32;
    static constexpr int padding = 10;

    void setTitle (const juce::String&);
    const juce::String& getSectionTitle() const noexcept { return sectionTitle; }

    void setContent (juce::Component*);
    juce::Component* getContent() const noexcept { return content; }

    void setExpanded (bool shouldBeExpanded, bool notify = true);
    bool isExpanded() const noexcept { return expanded; }

    /// Fired after the expanded state changed so a parent can re-layout / resize its scroll content.
    std::function<void()> onLayoutChanged;

    juce::Button& getHeaderButton() noexcept;

    int getPreferredHeight (int width) const override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class HeaderButton;
    void updateAccessibleText();

    juce::String sectionTitle;
    juce::Component* content = nullptr;
    bool expanded = true;
    std::unique_ptr<HeaderButton> header;
};
} // namespace snaral::ui
