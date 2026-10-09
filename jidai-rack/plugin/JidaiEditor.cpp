// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "JidaiEditor.h"
#include "ListControl.h"
#include "RackCableLayer.h"
#include "RackStyle.h"
#include "StarterRacks.h"

using namespace jidai;
using namespace rackstyle;


// ---------------- header ----------------

class JidaiEditor::Header : public juce::Component, public juce::TooltipClient
{
public:
    explicit Header (JidaiEditor& e) : ed (e) {}

    float u() const { return (float) getHeight() / (float) kHeaderHeight; }

    juce::Rectangle<float> button (int b) const
    {
        const float k = u(), y = 7.0f * k, h = 26.0f * k;
        switch (b)
        {
            case BtnFront: return { 150.0f * k, y, 66.0f * k, h };
            case BtnBack: return { 218.0f * k, y, 62.0f * k, h };
            case BtnModeAll: return { 372.0f * k, y, 48.0f * k, h };
            case BtnModeHidePass: return { 422.0f * k, y, 120.0f * k, h };
            case BtnModeSelected: return { 544.0f * k, y, 82.0f * k, h };
            case BtnModeHide: return { 628.0f * k, y, 52.0f * k, h };
            case BtnFoldAll: return { 714.0f * k, y, 90.0f * k, h };
            case BtnScale: return { (float) getWidth() - 74.0f * k, y, 64.0f * k, h };
            case BtnRacks: return { 812.0f * k, y, 78.0f * k, h };
            case BtnNotice:
                return ed.proc.migrationNotice.isEmpty() ? juce::Rectangle<float>()
                                                         : juce::Rectangle<float> (990.0f * k, y, (float) getWidth() - 1074.0f * k, h);
            default: break;
        }
        return {};
    }

    void paint (juce::Graphics& g) override
    {
        const float k = u();
        const auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d1d20), 0, 0, juce::Colour (0xff121214), 0, r.getBottom(), false));
        g.fillRect (r);
        g.setColour (juce::Colour (0xff000000));
        g.drawHorizontalLine (getHeight() - 1, 0.0f, r.getRight());
        g.setColour (kInk);
        g.setFont (font (19.0f * k, true));
        g.drawText ("JIDAI RACK", juce::Rectangle<float> (14.0f * k, 0.0f, 130.0f * k, r.getHeight()), juce::Justification::centredLeft);

        auto pill = [&] (int b, const juce::String& text, bool on)
        {
            const auto rr = button (b);
            if (rr.isEmpty())
                return;
            g.setColour (on ? kGold : (hover == b ? juce::Colour (0xff36363b) : juce::Colour (0xff28282c)));
            g.fillRoundedRectangle (rr, 3.0f * k);
            g.setColour (juce::Colour (0x40ffffff));
            g.drawRoundedRectangle (rr, 3.0f * k, 1.0f);
            g.setColour (on ? juce::Colour (0xff141414) : kInk);
            g.setFont (font (11.5f * k, true));
            g.drawFittedText (text, rr.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        };
        auto hint = [&] (const juce::String& t, float x, float w)
        {
            g.setColour (kDim);
            g.setFont (font (kHelpTextPx * k));
            g.drawText (t, juce::Rectangle<float> (x * k, 0.0f, w * k, r.getHeight()), juce::Justification::centredLeft);
        };
        const bool back = ed.proc.showBack;
        pill (BtnFront, "FRONT", ! back);
        pill (BtnBack, "BACK", back);
        hint ("Tab", tabHint().getX(), tabHint().getWidth());
        g.setColour (kInk.withAlpha (0.8f));
        g.setFont (font (11.5f * k, true));
        g.drawText ("CABLES", juce::Rectangle<float> (318.0f * k, 0.0f, 54.0f * k, r.getHeight()), juce::Justification::centredLeft);
        const int mode = ed.rack.cableMode();
        for (int m = 0; m < 4; ++m)
            pill (BtnModeAll + m, RackView::cableModeName (m), mode == m);
        hint ("K", kHint().getX(), kHint().getWidth());
        pill (BtnFoldAll, ed.rack.allFolded() ? "OPEN ALL" : "FOLD ALL", false);
        pill (BtnRacks, juce::String::fromUTF8 ("RACKS \xe2\x96\xbe"), false);
        const int lat = ed.proc.rack().latency();
        g.setColour (lat > 0 ? kAmber : kDim);
        g.setFont (font (13.0f * k, true));
        g.drawText ("LAT " + juce::String (lat), juce::Rectangle<float> (902.0f * k, 0.0f, 84.0f * k, r.getHeight()), juce::Justification::centredLeft);
        if (! ed.proc.migrationNotice.isEmpty())
        {
            const auto nr = button (BtnNotice);
            g.setColour (juce::Colour (0xff2c2410));
            g.fillRoundedRectangle (nr, 3.0f * k);
            g.setColour (kAmber.withAlpha (0.7f));
            g.drawRoundedRectangle (nr, 3.0f * k, 1.0f);
            g.setColour (kAmber);
            g.setFont (font (11.0f * k));
            g.drawFittedText (ed.proc.migrationNotice + juce::String::fromUTF8 ("   \xc3\x97"), nr.reduced (8.0f * k, 0.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.7f);
        }
        pill (BtnScale, juce::String (ed.proc.scalePercent) + "%", false);
    }

    int buttonAt (juce::Point<float> p) const
    {
        for (int b = 0; b < kHeaderButtons; ++b)
            if (button (b).contains (p))
                return b;
        return -1;
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int b = buttonAt (e.position);
        if (b != hover) { hover = b; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    // Help in large type on hover: the key hints, and how the list-like pills work.
    static juce::Rectangle<float> tabHint() { return { 286.0f, 0.0f, 30.0f, (float) kHeaderHeight }; }    // header units
    static juce::Rectangle<float> kHint() { return { 686.0f, 0.0f, 20.0f, (float) kHeaderHeight }; }
    juce::String getTooltip() override
    {
        const auto p = getMouseXYRelative().toFloat() / u();
        if (tabHint().contains (p)) return "Tab flips the rack between FRONT and BACK";
        if (kHint().contains (p)) return "K steps through the cable views";
        switch (buttonAt (getMouseXYRelative().toFloat()))
        {
            case BtnScale: return juce::String::fromUTF8 ("Window size \xc2\xb7 click: next, Shift-click: previous, right-click: the whole list");
            case BtnRacks: return "Starter racks";
            default: return {};
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int b = buttonAt (e.position);
        const bool menu = e.mods.isPopupMenu(), back = e.mods.isShiftDown();
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<JidaiEditor> (&ed), b, menu, back]
        {
            if (sp == nullptr)
                return;
            auto& rv = sp->rack;
            switch (b)
            {
                case BtnFront: rv.setShowBack (false); break;
                case BtnBack: rv.setShowBack (true); break;
                case BtnModeAll: case BtnModeHidePass: case BtnModeSelected: case BtnModeHide: rv.setCableMode (b - BtnModeAll); break;
                case BtnFoldAll: rv.foldAll (! rv.allFolded()); break;
                case BtnNotice: sp->proc.migrationNotice.clear(); break;
                case BtnRacks:
                    sp->starterRackMenu().showMenuAsync (
                        juce::PopupMenu::Options().withTargetComponent (sp->head.get())
                                                  .withTargetScreenArea (sp->head->localAreaToGlobal (sp->head->button (BtnRacks).toNearestInt())),
                        [sp] (int id)
                        {
                            if (sp != nullptr && id > 0)
                                sp->proc.setCurrentProgram (id - 1);
                        });
                    break;
                case BtnScale:
                    // A list control: click = next, Shift-click = previous, right-click = the whole list.
                    if (menu)
                    {
                        const auto options = juce::PopupMenu::Options().withTargetComponent (sp->head.get())
                                                 .withTargetScreenArea (sp->head->localAreaToGlobal (sp->head->button (BtnScale).toNearestInt()));
                        std::function<void (int)> done = [sp] (int id)
                        {
                            if (sp == nullptr) return;
                            if (const int pct = jidai_ui::scaleFromMenuResult (id); pct > 0)
                                sp->setScalePercent (pct);
                            sp->head->repaint();
                        };
                        if (sp->showMenu)
                            sp->showMenu (sp->scaleMenu(), options, std::move (done));
                        else
                            sp->scaleMenu().showMenuAsync (options, std::move (done));
                    }
                    else
                        sp->setScalePercent (jidai_ui::nextScaleStep (sp->proc.scalePercent, back));
                    break;
                default: break;
            }
            sp->head->repaint();
        });
    }

    JidaiEditor& ed;
    int hover = -1;
};

// ---------------- editor ----------------

JidaiEditor::JidaiEditor (JidaiProcessor& p)
    : AudioProcessorEditor (p), proc (p), rack (p)
{
    setLookAndFeel (&laf);
    list = std::make_unique<DeviceBrowser> (p);
    list->onAdd = [this] (const DeviceBrowser::Entry& e, bool skipRoute)
    {
        proc.addDevice (e.kind, -1, ! skipRoute, e.effect);     // a click adds one at the bottom
        rack.rebuild();
        view.setViewPosition (0, juce::jmax (0, rack.getHeight() - view.getHeight()));
    };
    list->onRemoveDrop = [this] (const juce::String& description)
    {
        dropLanded = true;
        removeLater (description);
    };
    list->onToggle = [this] { setBrowserOpen (! proc.browserOpen); };
    list->setClosed (! proc.browserOpen);
    addAndMakeVisible (*list);
    head = std::make_unique<Header> (*this);
    addAndMakeVisible (*head);
    view.setViewedComponent (&rack, false);
    view.setScrollBarsShown (true, false);
    view.setScrollBarThickness (10);
    addAndMakeVisible (view);
    rack.onLayoutChanged = [this] { layoutRack(); list->repaint(); if (head) head->repaint(); };
    rack.onViewChanged = [this] { if (head) head->repaint(); };
    rack.onDropped = [this] (const juce::String&) { dropLanded = true; };
    rack.onScrollTo = [this] (juce::Rectangle<int> r) { view.setViewPosition (0, juce::jmax (0, r.getY() - 8)); };
    proc.addChangeListener (this);
    setWantsKeyboardFocus (true);
    addMouseListener (this, true);       // any click in the window gives it the keys (unless a text box takes them)

    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, 2600, 1800);
    const float k = uiScale();
    setSize (juce::jlimit (kMinWidth, 2600, (int) std::round (kDefaultWidth * k)), juce::jlimit (kMinHeight, 1800, (int) std::round (kDefaultHeight * k)));
    startTimerHz (4);                    // LAT readouts
}

JidaiEditor::~JidaiEditor()
{
    removeMouseListener (this);
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

juce::PopupMenu JidaiEditor::scaleMenu() const
{
    juce::PopupMenu menu;
    for (int i = 0; i < jidai_ui::kScaleStepCount; ++i)
        menu.addItem (i + 1, juce::String (jidai_ui::kScaleSteps[i]) + " %", true, jidai_ui::kScaleSteps[i] == proc.scalePercent);
    return menu;
}

juce::PopupMenu JidaiEditor::starterRackMenu() const
{
    juce::PopupMenu menu;
    juce::StringArray categories;
    const auto& racks = starterRacks();
    for (size_t i = 0; i < racks.size(); ++i)
    {
        if (racks[i].category.isEmpty() || racks[i].category == "INIT")
            menu.addItem ((int) i + 1, racks[i].name);
        else
            categories.addIfNotAlreadyThere (racks[i].category);
    }
    for (const auto& c : categories)
    {
        juce::PopupMenu sub;
        for (size_t i = 0; i < racks.size(); ++i)
            if (racks[i].category == c)
                sub.addItem ((int) i + 1, racks[i].name);
        menu.addSubMenu (c, sub);
    }
    return menu;
}

juce::Component& JidaiEditor::deviceList() { return *list; }
juce::Component& JidaiEditor::header() { return *head; }

juce::Rectangle<int> JidaiEditor::headerButtonBounds (int b) const
{
    return head->button (b).toNearestInt() + head->getPosition();
}

void JidaiEditor::timerCallback()
{
    const int lat = proc.rack().latency();
    if (lat != lastLatency)
    {
        lastLatency = lat;
        head->repaint();
        rack.reloadCables();
    }
}

void JidaiEditor::removeLater (const juce::String& description)
{
    const auto id = description.fromFirstOccurrenceOf (":", false, false).toStdString();
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<JidaiEditor> (this), id]
    {
        if (sp == nullptr)
            return;
        if (Device* d = sp->proc.rack().findDevice (id))
            sp->rack.removeDevice (d);
    });
}

void JidaiEditor::setBrowserOpen (bool open)
{
    proc.browserOpen = open;
    list->setClosed (! open);
    resized();
}

void JidaiEditor::setScalePercent (int percent)
{
    percent = juce::jlimit (75, 200, percent);
    const float old = uiScale();
    proc.scalePercent = percent;
    const float k = uiScale() / juce::jmax (0.01f, old);
    setSize (juce::jlimit (kMinWidth, 2600, (int) std::round ((float) getWidth() * k)),
             juce::jlimit (kMinHeight, 1800, (int) std::round ((float) getHeight() * k)));
    resized();
}

void JidaiEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0d0f));
}

void JidaiEditor::resized()
{
    const float k = uiScale();
    auto r = getLocalBounds();
    head->setBounds (r.removeFromTop ((int) std::round (kHeaderHeight * k)));
    list->setBounds (r.removeFromLeft (proc.browserOpen ? (int) std::round (kListWidth * k) : DeviceBrowser::kClosedWidth));
    view.setBounds (r);
    layoutRack();
}

void JidaiEditor::layoutRack()
{
    const int w = juce::jmax (1, view.getWidth() - view.getScrollBarThickness());
    rack.setSize (w, rack.heightForWidth (w, view.getHeight()));
}

void JidaiEditor::mouseDown (const juce::MouseEvent& e)
{
    if (dynamic_cast<juce::TextEditor*> (e.originalComponent) == nullptr && ! hasKeyboardFocus (true))
        grabKeyboardFocus();
}

bool JidaiEditor::keyPressed (const juce::KeyPress& key)
{
    if (dynamic_cast<juce::TextEditor*> (getCurrentlyFocusedComponent()) != nullptr)
        return false;
    const auto code = key.getKeyCode();
    const bool shift = key.getModifiers().isShiftDown();
    Device* sel = rack.selectedDevice();
    if (code == juce::KeyPress::tabKey)
    {
        rack.flip();
        return true;
    }
    if (code == juce::KeyPress::escapeKey)
    {
        if (! rack.cableLayer().cancelDrag())
        {
            rack.selectDevice (nullptr);
            rack.cableLayer().selectCable (-1);
        }
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        return rack.cableLayer().deleteSelected();
    const auto c = juce::CharacterFunctions::toUpperCase (key.getTextCharacter());
    if (c == 'K' || code == 'K')
    {
        rack.cycleCableMode();
        return true;
    }
    if (c == 'F' || code == 'F')
    {
        if (shift)
            rack.foldAll (! rack.allFolded());
        else if (sel != nullptr)
            rack.setFolded (sel, ! sel->folded);
        return true;
    }
    if (c == 'C' || code == 'C')
    {
        if (sel != nullptr)
            rack.setClosed (sel, ! sel->closed);
        return true;
    }
    return false;
}

void JidaiEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rack.rebuild();         // a saved rack was loaded
    list->setClosed (! proc.browserOpen);
    resized();
    head->repaint();
}

void JidaiEditor::dragOperationStarted (const juce::DragAndDropTarget::SourceDetails&)
{
    dropLanded = false;
}

void JidaiEditor::dragOperationEnded (const juce::DragAndDropTarget::SourceDetails& d)
{
    const auto s = d.description.toString();
    if (s.startsWith ("move:") && ! dropLanded)
        removeLater (s);
    dropLanded = false;
}
