#include "PresetBrowserPanel.h"

namespace snaral::ui
{
namespace
{
juce::String fromStd (const std::string& s) { return juce::String::fromUTF8 (s.data(), static_cast<int> (s.size())); }

struct ChipSpec
{
    const char* name;
    const char* group;   // exclusive groups: "origin", "layer"; multi: "motion", "texture"
    int value;           // enum / bit value
};
const ChipSpec chipSpecs[] = {
    { "Factory", "origin", static_cast<int> (OriginFilter::Factory) },
    { "User", "origin", static_cast<int> (OriginFilter::User) },
    { "Favourites", "origin", static_cast<int> (OriginFilter::Favourites) },
    { "Sub-forward", "layer", static_cast<int> (LayerBalance::SubForward) },
    { "Balanced", "layer", static_cast<int> (LayerBalance::Balanced) },
    { "Mid-forward", "layer", static_cast<int> (LayerBalance::MidForward) },
    { "Static", "motion", static_cast<int> (motionStatic) },
    { "Contour", "motion", static_cast<int> (motionContour) },
    { "Synced", "motion", static_cast<int> (motionSynced) },
    { "Free", "motion", static_cast<int> (motionFree) },
    { "Clean", "texture", static_cast<int> (textureClean) },
    { "Noise", "texture", static_cast<int> (textureNoise) },
    { "Metal", "texture", static_cast<int> (textureMetal) },
};
constexpr int rowH = 40;
} // namespace

class PresetBrowserPanel::Model : public juce::ListBoxModel
{
public:
    explicit Model (PresetBrowserPanel& o) : owner (o) {}

    int getNumRows() override { return owner.ctl.browser().resultCount(); }

    juce::String getNameForRow (int row) override
    {
        auto& b = owner.ctl.browser();
        if (row < 0 || row >= b.resultCount())
            return {};
        const auto& e = b.entryAt (row);
        return fromStd (e.name) + (e.favourite ? ", favourite" : "") + ", " + (e.origin == PresetOrigin::Factory ? "factory" : "user");
    }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        auto& b = owner.ctl.browser();
        if (row < 0 || row >= b.resultCount())
            return;
        const auto& t = SnaralLookAndFeel::tokensFor (owner);
        const auto& e = b.entryAt (row);
        if (selected)
        {
            g.setColour (t.accentTint (t.panelRaised));
            g.fillRect (0, 0, w, h);
            g.setColour (t.accent);
            g.fillRect (0, 0, 3, h);   // shape cue as well as colour
        }
        const bool loaded = e.id == b.loadedId();
        // favourite star: filled when set, outline otherwise
        juce::Path star;
        star.addStar ({ 16.0f, static_cast<float> (h) * 0.5f }, 5, 4.5f, 9.0f);
        g.setColour (e.favourite ? t.warning : t.textMuted);
        if (e.favourite)
            g.fillPath (star);
        else
            g.strokePath (star, juce::PathStrokeType (1.2f));
        g.setColour (t.textPrimary);
        g.setFont (fonts::heading().withHeight (13.0f));
        auto name = fromStd (e.name);
        if (loaded)
            name << (b.hasUnsavedChanges() ? "  (modified)" : "  (loaded)");
        g.drawText (name, 34, 3, w - 34 - 70, 18, juce::Justification::centredLeft, true);
        g.setColour (t.textMuted);
        g.setFont (fonts::small());
        juce::String sub = e.author.empty() ? juce::String() : fromStd (e.author);
        for (const auto& tag : e.tags)
            sub << (sub.isEmpty() ? "" : "  ") << "#" << fromStd (tag);
        g.drawText (sub, 34, 21, w - 34 - 70, 16, juce::Justification::centredLeft, true);
        g.drawText (e.origin == PresetOrigin::Factory ? "Factory" : (e.origin == PresetOrigin::User ? "User" : "Pack"),
                    w - 66, 0, 60, h, juce::Justification::centredRight, false);
        g.setColour (t.border);
        g.drawHorizontalLine (h - 1, 0.0f, static_cast<float> (w));
    }

    void selectedRowsChanged (int lastRow) override
    {
        if (owner.updating)
            return;
        auto& b = owner.ctl.browser();
        if (lastRow >= 0 && lastRow < b.resultCount())
            b.select (b.entryAt (lastRow).id);
        owner.updateButtons();
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        auto& b = owner.ctl.browser();
        if (row < 0 || row >= b.resultCount())
            return;
        if (e.x < 32)
        {
            owner.ctl.toggleFavourite (b.entryAt (row).id);
            owner.refresh();
        }
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { load (row); }
    void returnKeyPressed (int row) override { load (row); }

    void load (int row)
    {
        auto& b = owner.ctl.browser();
        if (row >= 0 && row < b.resultCount())
            owner.ctl.requestLoadPreset (b.entryAt (row).id);
    }

    PresetBrowserPanel& owner;
};

PresetBrowserPanel::PresetBrowserPanel (EditorController& c) : ctl (c)
{
    setTitle ("Preset browser");
    setWantsKeyboardFocus (true);
    setFocusContainerType (FocusContainerType::focusContainer);
    model = std::make_unique<Model> (*this);

    search.setTextToShowWhenEmpty ("Search name, tag, author", juce::Colours::grey);
    search.setTitle ("Search presets");
    search.setFont (fonts::body());
    search.onTextChange = [this] { queryChanged(); };
    search.onEscapeKey = [this] { if (search.getText().isNotEmpty()) search.clear(); else if (onCloseRequested) onCloseRequested(); };
    addAndMakeVisible (search);

    for (const auto& s : chipSpecs)
    {
        auto b = std::make_unique<juce::TextButton> (s.name);
        b->setClickingTogglesState (true);
        b->setTitle (juce::String ("Filter ") + s.name);
        b->setTooltip (juce::String ("Show only ") + s.name + " presets");
        const juce::String group = s.group;
        const int value = s.value;
        auto* raw = b.get();
        b->onClick = [this, raw, group, value]
        {
            if (group == "origin" || group == "layer")
                for (std::size_t i = 0; i < chips.size(); ++i)
                    if (chips[i].get() != raw && juce::String (chipSpecs[i].group) == group)
                        chips[i]->setToggleState (false, juce::dontSendNotification);
            juce::ignoreUnused (value);
            queryChanged();
        };
        addAndMakeVisible (*b);
        chips.push_back (std::move (b));
    }

    sortBox.addItem ("Sort: Name", 1);
    sortBox.addItem ("Sort: Recently used", 2);
    sortBox.addItem ("Sort: Category", 3);
    sortBox.setSelectedId (1, juce::dontSendNotification);
    sortBox.setTitle ("Sort order");
    sortBox.onChange = [this] { queryChanged(); };
    addAndMakeVisible (sortBox);
    tagBox.setTitle ("Tag filter");
    tagBox.onChange = [this] { queryChanged(); };
    addAndMakeVisible (tagBox);

    list.setModel (model.get());
    list.setRowHeight (rowH);
    list.setTitle ("Preset list");
    list.setWantsKeyboardFocus (true);
    list.setOutlineThickness (1);
    addAndMakeVisible (list);

    auditionBtn.setTooltip ("Load the selected preset temporarily and play a short note. Apply keeps it, Cancel restores your sound.");
    applyBtn.setTooltip ("Keep the auditioned preset");
    cancelBtn.setTooltip ("Restore the sound you had before the audition");
    loadBtn.setTooltip ("Load the selected preset (Enter)");
    rescanBtn.setTooltip ("Look for new or changed preset files");
    closeBtn.setTooltip ("Close the browser (Escape)");
    auditionBtn.onClick = [this]
    {
        const auto& id = ctl.browser().selectedId();
        if (! id.empty()) ctl.beginAudition (id);
        updateButtons();
    };
    applyBtn.onClick = [this] { ctl.confirmAudition(); refresh(); };
    cancelBtn.onClick = [this] { ctl.cancelAudition(); refresh(); };
    loadBtn.onClick = [this] { const auto& id = ctl.browser().selectedId(); if (! id.empty()) ctl.requestLoadPreset (id); };
    rescanBtn.onClick = [this] { ctl.rescanLibrary(); refresh(); };
    closeBtn.onClick = [this] { if (onCloseRequested) onCloseRequested(); };
    for (auto* b : { &auditionBtn, &applyBtn, &cancelBtn, &loadBtn, &rescanBtn, &closeBtn })
        addAndMakeVisible (*b);

    ctl.addListener (this);
    refresh();
}

PresetBrowserPanel::~PresetBrowserPanel()
{
    ctl.removeListener (this);
    list.setModel (nullptr);
}

juce::TextButton& PresetBrowserPanel::chip (const juce::String& name)
{
    for (std::size_t i = 0; i < chips.size(); ++i)
        if (juce::String (chipSpecs[i].name) == name)
            return *chips[i];
    jassertfalse;
    return *chips.front();
}

void PresetBrowserPanel::visibilityChanged()
{
    if (isVisible())
        refresh();
}

void PresetBrowserPanel::controllerStateChanged() { if (isVisible()) { list.repaint(); updateButtons(); } }

void PresetBrowserPanel::focusSearch() { search.grabKeyboardFocus(); }

void PresetBrowserPanel::queryChanged()
{
    if (updating)
        return;
    BrowserQuery q;
    q.text = search.getText().toStdString();
    for (std::size_t i = 0; i < chips.size(); ++i)
    {
        if (! chips[i]->getToggleState())
            continue;
        const juce::String g = chipSpecs[i].group;
        if (g == "origin") q.origin = static_cast<OriginFilter> (chipSpecs[i].value);
        else if (g == "layer") { q.filterLayer = true; q.layer = static_cast<LayerBalance> (chipSpecs[i].value); }
        else if (g == "motion") q.motionMask |= static_cast<unsigned> (chipSpecs[i].value);
        else if (g == "texture") q.textureMask |= static_cast<unsigned> (chipSpecs[i].value);
    }
    if (tagBox.getSelectedId() > 1)
        q.tags.push_back (tagBox.getText().upToFirstOccurrenceOf (" (", false, false).toStdString());
    q.sort = sortBox.getSelectedId() == 2 ? SortMode::RecentlyUsed : (sortBox.getSelectedId() == 3 ? SortMode::Category : SortMode::Name);
    ctl.browser().setQuery (q);
    refresh();
}

void PresetBrowserPanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    auto& b = ctl.browser();

    const auto tagText = tagBox.getText().upToFirstOccurrenceOf (" (", false, false);
    tagBox.clear (juce::dontSendNotification);
    tagBox.addItem ("Tag: any", 1);
    int id = 2, keep = 1;
    for (const auto& [tag, count] : b.availableTags())
    {
        tagBox.addItem (fromStd (tag) + " (" + juce::String (count) + ")", id);
        if (fromStd (tag) == tagText)
            keep = id;
        ++id;
    }
    tagBox.setSelectedId (keep, juce::dontSendNotification);

    list.updateContent();
    emptyText = b.resultCount() == 0 ? fromStd (b.emptyStateMessage()) : juce::String();
    const int sel = b.selectedResultIndex();
    if (sel >= 0)
        list.selectRow (sel, true, false);
    else
        list.deselectAllRows();
    list.repaint();
    updateButtons();
    repaint();
}

void PresetBrowserPanel::updateButtons()
{
    const bool haveSel = ctl.browser().selectedResultIndex() >= 0;
    const bool aud = ctl.isAuditioning();
    auditionBtn.setEnabled (haveSel);
    loadBtn.setEnabled (haveSel);
    applyBtn.setEnabled (aud);
    cancelBtn.setEnabled (aud);
    auditionBtn.setButtonText (aud ? "Audition next" : "Audition");
}

int PresetBrowserPanel::layoutChips (juce::Rectangle<int> area, bool apply)
{
    int x = area.getX(), y = area.getY(), rowBottom = y;
    const int h = 30, gap = 6;
    for (auto& c : chips)
    {
        const int w = static_cast<int> (textWidth (fonts::body().withHeight (12.0f), c->getButtonText())) + 22;
        if (x + w > area.getRight())
        {
            x = area.getX();
            y += h + gap;
        }
        if (apply)
            c->setBounds (x, y, w, h);
        x += w + gap;
        rowBottom = y + h;
    }
    return rowBottom - area.getY();
}

void PresetBrowserPanel::paint (juce::Graphics& g)
{
    const auto& t = SnaralLookAndFeel::tokensFor (*this);
    SnaralLookAndFeel::drawPanelShadow (g, t, getLocalBounds().toFloat(), 0.0f);
    g.fillAll (t.panel);
    g.setColour (t.outline);
    g.drawVerticalLine (0, 0.0f, static_cast<float> (getHeight()));
    g.setColour (t.textPrimary);
    g.setFont (fonts::title());
    g.drawText ("Presets", 14, 0, 120, 40, juce::Justification::centredLeft, false);
    if (emptyText.isNotEmpty())
    {
        g.setColour (t.textMuted);
        g.setFont (fonts::body());
        g.drawFittedText (emptyText, list.getBounds().reduced (16, 24), juce::Justification::centredTop, 4);
    }
}

void PresetBrowserPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 0);
    auto head = r.removeFromTop (40);
    closeBtn.setBounds (head.removeFromRight (72).reduced (0, 4));
    r.removeFromTop (2);
    search.setBounds (r.removeFromTop (32));
    r.removeFromTop (8);
    r.removeFromTop (layoutChips (r.withHeight (140), true));
    r.removeFromTop (8);
    auto row = r.removeFromTop (32);
    sortBox.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
    row.removeFromLeft (6);
    tagBox.setBounds (row);
    r.removeFromTop (8);
    auto foot = r.removeFromBottom (32);
    r.removeFromBottom (6);
    auto foot2 = r.removeFromBottom (32);
    r.removeFromBottom (6);
    const int w = (foot.getWidth() - 12) / 3;
    auditionBtn.setBounds (foot2.removeFromLeft (w + 20));
    foot2.removeFromLeft (6);
    loadBtn.setBounds (foot2.removeFromLeft (w - 14));
    foot2.removeFromLeft (6);
    rescanBtn.setBounds (foot2);
    applyBtn.setBounds (foot.removeFromLeft (foot.getWidth() / 2 - 3));
    foot.removeFromLeft (6);
    cancelBtn.setBounds (foot);
    list.setBounds (r);
}

bool PresetBrowserPanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && onCloseRequested)
    {
        onCloseRequested();
        return true;
    }
    if (k.getTextCharacter() == 'f' && list.hasKeyboardFocus (true) && ! search.hasKeyboardFocus (true))
    {
        auto& b = ctl.browser();
        const int row = list.getSelectedRow();
        if (row >= 0 && row < b.resultCount())
        {
            ctl.toggleFavourite (b.entryAt (row).id);
            refresh();
            return true;
        }
    }
    return false;
}
} // namespace snaral::ui
