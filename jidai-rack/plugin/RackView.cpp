// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "RackView.h"
#include "BinaryData.h"
#include "CompactFace.h"
#include "RackCableLayer.h"
#include "RackStyle.h"
#include "RearPanel.h"
#include "RoninPanel.h"
#include "ShogunFace.h"

#include "origami/plugin/OrigamiPanel.h"
#include "ui/BushidoTabs.h"
#include "ui/PatternScreen.h"
#include "ui/RackPanel.h"

using namespace jidai;
using namespace rackstyle;

namespace {

// RONIN's layout has the glass, the LCD and the dropdown key, and no banks or SAVE.
PanelLayout::Screen roninScreen()
{
    PanelLayout::Screen s;
    s.bezel = { kPresetBezelX, kPresetBezelY, kPresetBezelW, kPresetBezelH };
    s.lcd = { kPresetLcdX, kPresetLcdY, kPresetLcdW, kPresetLcdH };
    s.button = { kPresetKeyX, kPresetKeyY, kPresetKeyW, kPresetKeyH };
    s.save = { 1032.0f, 15.0f, 26.0f, 24.0f };
    s.chars = 17;
    s.listRows = 10;
    s.bankSize = 999;
    s.banks.push_back ({ "A", 968.0f, 27.0f, 4.5f, { 958.0f, 15.0f, 30.0f, 24.0f } });
    s.banks.push_back ({ "B", 1000.0f, 27.0f, 4.5f, { 990.0f, 15.0f, 30.0f, 24.0f } });
    return s;
}

int roninKnob (const char* section, const char* label)
{
    for (int i = 0; i < kPanelKnobCount; ++i)
        if (juce::String (kPanelKnobs[i].section) == section && juce::String (kPanelKnobs[i].label) == label)
            return i;
    return -1;
}

int frontCableCount (Rack& rack, const Device& d)
{
    const std::string prefix = d.rackId() + "/";
    int n = 0;
    for (auto& c : rack.cables())
        for (const std::string* end : { &c.a, &c.b })
            if (end->rfind (prefix, 0) == 0)
            {
                const int j = d.findJack (end->substr (prefix.size()));
                if (j >= 0 && ! d.jacks()[(size_t) j].backOnly)
                {
                    ++n;
                    break;
                }
            }
    return n;
}

int deviceCableCount (Rack& rack, const Device& d)
{
    const std::string prefix = d.rackId() + "/";
    int n = 0;
    for (auto& c : rack.cables())
        if (c.a.rfind (prefix, 0) == 0 || c.b.rfind (prefix, 0) == 0)
            ++n;
    return n;
}

juce::String bpmText (double bpm)
{
    const double v = std::round (bpm * 10.0) / 10.0;
    return juce::String (v, 1);
}

} // namespace

// ---------------- bindings ----------------

class RackView::BushidoBinding : public RackPanel::Binding
{
public:
    explicit BushidoBinding (BushidoDevice& d) : b (d) {}
    float get (const juce::String& id) override
    {
        if (id == "TOP:BYPASS")
            return b.bypassed() ? 1.0f : 0.0f;
        return b.param (id.toStdString());
    }
    void set (const juce::String& id, float v) override
    {
        if (id == "TOP:BYPASS")
            b.setBypassed (v > 0.5f);
        else
            b.setParam (id.toStdString(), v);
    }
    void press (const juce::String& id, bool down) override { b.press (id.toStdString(), down); }
    float indicator (const juce::String& id) override { return b.indicator (id.toStdString()); }
    juce::String readoutText (const juce::String&) override
    {
        if (! readoutEnabled ({}))
            return "EXT";
        const double v = std::round (readoutValue ({}) * 10.0) / 10.0;
        return std::abs (v - std::round (v)) < 0.05 ? juce::String ((int) std::round (v)) : juce::String (v, 1);
    }
    bool readoutEnabled (const juce::String&) override { return get ("CLOCK:SOURCE") < 0.5f; }
    double readoutValue (const juce::String&) override { return BushidoModule::bpm (get ("CLOCK:TEMPO"), get ("CLOCK:DIV")); }
    void setReadoutValue (const juce::String&, double bpm) override
    {
        set ("CLOCK:TEMPO", BushidoModule::tempoForBpm (juce::jmax (0.1, bpm), get ("CLOCK:DIV")));
    }
    BushidoDevice& b;
};

// BUSHIDO's STEPS / CLOCK / MIDI / SETUP pages (its own TabPage, from the pinned bushido sources).
class RackView::BushidoTabHost : public bushido_ui::TabHost
{
public:
    BushidoTabHost (BushidoDevice& d, JidaiProcessor& p, std::function<void()> rescale) : b (d), proc (p), onRescale (std::move (rescale)) {}
    float value (const juce::String& id) override { return b.param (id.toStdString()); }
    void setValue (const juce::String& id, float v) override { b.setParam (id.toStdString(), v); }
    void edit (const juce::String&, bool) override {}
    float lamp (const juce::String& id) override { return b.indicator (id.toStdString()); }
    double extPeriod() override { return b.engine().measuredExtPeriod(); }
    double hostBpm() override { return b.engine().hostBpm(); }
    int settleSamples() override { return b.engine().settleSamples(); }
    std::vector<juce::String> migrationLines() override { return {}; }
    bool lawMismatch (int) override { return false; }
    bool readOnly() override { return false; }
    int scalePercent() override { return proc.scalePercent; }
    void setScalePercent (int percent) override
    {
        proc.scalePercent = percent;
        if (onRescale)
            onRescale();
    }
    BushidoDevice& b;
    JidaiProcessor& proc;
    std::function<void()> onRescale;
};

class RackView::OrigamiAccess : public OrigamiPanel::Access
{
public:
    OrigamiAccess (OrigamiDevice& d, Rack& r) : o (d), rack (r) {}
    double get (int p) const override { return o.param (p); }
    void set (int p, double v) override { o.setParam (p, v); }
    float inPeak (int ch) const override { return o.core().inPeak (ch) * 5.0f; }
    float outPeak (int ch) const override { return o.core().outPeak (ch) * 5.0f; }
    float follower() const override { return o.follower(); }
    bool over() const override { return o.overLit(); }
    int latencySamples() const override { return o.latencySamples(); }
    double sampleRate() const override { return rack.sampleRate(); }
    double stageCurve (int stage, double x) const override { return o.stageCurve (stage, x); }
    OrigamiDevice& o;
    Rack& rack;
};

// ---------------- the shade under the unit above ----------------

class RackView::Shade : public juce::Component
{
public:
    Shade() { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x8c000000), 0, 0, juce::Colour (0x00000000), 0, r.getHeight(), false));
        g.fillRect (r);
    }
};

// ---------------- slot ----------------

struct RackView::Slot {
    Device* device = nullptr;
    std::unique_ptr<BushidoBinding> binding;
    std::unique_ptr<BushidoTabHost> tabHost;
    std::unique_ptr<OrigamiAccess> origamiAccess;
    std::unique_ptr<Mount> mount;
    std::unique_ptr<juce::Component> face;
    std::unique_ptr<Strip> strip;
    std::unique_ptr<Shade> shade;
    std::unique_ptr<PatternScreen> screen;
    std::unique_ptr<juce::Component> topLayer;        // the pattern screen, above the cables (its list drops over them)
    RackPanel* panel = nullptr;                       // BUSHIDO or RONIN panel (MAIN)
    OrigamiPanel* origami = nullptr;
    RearPanel* rear = nullptr;
    float top = 0.0f, height = 0.0f;                  // design units, strip included
    float faceY = 0.0f, faceH = 0.0f;                 // the face component inside the face area (design, from the area top)
};

// ---------------- mount: ears, screws, the plate behind the face ----------------

class RackView::Mount : public juce::Component
{
public:
    Mount (RackView& v, Device& d) : view (v), device (d) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float s = view.scale(), e = kRail * s;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222225), 0, 0, juce::Colour (0xff151517), 0, r.getBottom(), false));
        g.fillRect (r.reduced (e, 0.0f));
        paintEar (g, r.withWidth (e), true, s);
        paintEar (g, r.withTrimmedLeft (r.getWidth() - e), false, s);
        const float sr = 8.5f * s;
        const float salt = (float) device.number * 0.7f + (float) device.kind() * 0.4f;
        auto screwsAt = [&] (float y, float a) {
            paintScrew (g, { e * 0.5f, y }, sr, a);
            paintScrew (g, { r.getWidth() - e * 0.5f, y }, sr, a + 0.9f);
        };
        if (device.folded || r.getHeight() < 170.0f * s)
            screwsAt (r.getCentreY(), salt);
        else
            for (float u = 0.0f; u + 1.0f <= std::round (r.getHeight() / (kUnit * s)); u += 1.0f)
            {
                screwsAt ((u * kUnit + 24.0f) * s, salt + u);
                screwsAt (((u + 1.0f) * kUnit - 24.0f) * s, salt + u + 1.3f);
            }
    }

    void mouseDown (const juce::MouseEvent&) override { view.selectDevice (&device); }
    void mouseDrag (const juce::MouseEvent& e) override { startMove (view, device, *this, e); }

    static void startMove (RackView& view, Device& device, juce::Component& from, const juce::MouseEvent& e)
    {
        if (e.getDistanceFromDragStart() < 6 || device.kind() == DeviceKind::RackIO)
            return;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (&from))
        {
            if (container->isDragAndDropActive())
                return;
            const auto area = view.slotBounds (view.indexOfSlotFor (device));
            auto image = view.createComponentSnapshot (area, true, 1.0f);
            const int w = juce::jmin (360, image.getWidth());
            const int h = juce::jmax (1, image.getHeight() * w / juce::jmax (1, image.getWidth()));
            image = image.rescaled (w, h);
            image.multiplyAllAlphas (0.8f);
            container->startDragging ("move:" + juce::String (device.rackId()), &from, juce::ScaledImage (image), false, nullptr, &e.source);
        }
    }

    RackView& view;
    Device& device;
};

// ---------------- strip: fold, name, id, OPEN/CLOSED, tabs, LAT, view, BYPASS, remove ----------------

class RackView::Strip : public juce::Component
{
public:
    Strip (RackView& v, Device& d) : view (v), device (d) {}

    float s() const { return view.scale(); }
    float h() const { return (float) getHeight() / juce::jmax (0.001f, s()); }       // design

    bool hasBypass() const { return device.kind() == DeviceKind::Bushido || device.kind() == DeviceKind::Origami; }
    bool bypassed() const
    {
        if (auto* b = dynamic_cast<BushidoDevice*> (&device))
            return b->bypassed();
        if (auto* o = dynamic_cast<OrigamiDevice*> (&device))
            return o->param (origami::kBypass) > 0.5;
        return false;
    }
    void toggleBypass()
    {
        if (auto* b = dynamic_cast<BushidoDevice*> (&device))
            b->setBypassed (! b->bypassed());
        else if (auto* o = dynamic_cast<OrigamiDevice*> (&device))
            o->setParam (origami::kBypass, bypassed() ? 0.0 : 1.0);
    }

    // Part rectangles in design units, local to the strip.
    juce::Rectangle<float> part (int p) const
    {
        const float H = h();
        const bool front = ! view.showBack(), rackIO = device.kind() == DeviceKind::RackIO;
        switch (p)
        {
            case PartFold: return { 6.0f, (H - 22.0f) * 0.5f, 22.0f, 22.0f };
            case PartName: return { 34.0f, 0.0f, 380.0f, H };
            case PartRemove: return rackIO ? juce::Rectangle<float>() : juce::Rectangle<float> (1570.0f, (H - 22.0f) * 0.5f, 22.0f, 22.0f);
            case PartOpenClose:
                return front && ! rackIO && ! device.folded ? juce::Rectangle<float> (430.0f, 4.0f, 108.0f, 22.0f) : juce::Rectangle<float>();
            case PartBypass: return hasBypass() && ! device.folded ? juce::Rectangle<float> (1440.0f, 4.0f, 110.0f, 22.0f) : juce::Rectangle<float>();
            case PartBackBadge:
                return front && device.closed && ! device.folded && frontCableCount (view.proc.rack(), device) > 0
                           ? juce::Rectangle<float> (556.0f, 4.0f, 250.0f, 22.0f) : juce::Rectangle<float>();
            default: break;
        }
        if (p >= PartTab0 && front && ! device.closed && ! device.folded)
        {
            const auto names = tabNames (device.kind());
            const int k = p - PartTab0;
            if (k < names.size())
                return { 1180.0f - 84.0f * (float) (names.size() - k), 4.0f, 80.0f, 22.0f };
        }
        return {};
    }
    juce::Rectangle<float> partPx (int p) const
    {
        const auto r = part (p);
        return { r.getX() * s(), r.getY() * s(), r.getWidth() * s(), r.getHeight() * s() };
    }
    int partAt (juce::Point<float> px) const
    {
        for (int p : { (int) PartFold, (int) PartRemove, (int) PartOpenClose, (int) PartBypass, (int) PartBackBadge })
            if (partPx (p).expanded (2.0f).contains (px))
                return p;
        for (int k = 0; k < 8; ++k)
            if (partPx (PartTab0 + k).contains (px))
                return PartTab0 + k;
        if (partPx (PartName).contains (px))
            return PartName;
        return PartNone;
    }

    void paint (juce::Graphics& g) override
    {
        const float sc = s();
        const auto r = getLocalBounds().toFloat();
        const bool sel = view.selectedDevice() == &device;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff26262a), 0, 0, juce::Colour (0xff18181b), 0, r.getBottom(), false));
        g.fillRect (r);
        g.setColour (juce::Colour (0x22ffffff));
        g.drawHorizontalLine (0, 0.0f, r.getRight());
        g.setColour (juce::Colour (0xff050505));
        g.drawHorizontalLine ((int) r.getBottom() - 1, 0.0f, r.getRight());
        if (sel)
        {
            g.setColour (kGold.withAlpha (0.75f));
            g.drawRect (r, juce::jmax (1.0f, 1.5f * sc));
        }

        // Fold arrow.
        {
            const auto b = partPx (PartFold);
            const auto c = b.getCentre();
            const float k = b.getWidth() * 0.26f;
            juce::Path p;
            if (device.folded)
                p.addTriangle (c.x - k * 0.7f, c.y - k, c.x - k * 0.7f, c.y + k, c.x + k, c.y);
            else
                p.addTriangle (c.x - k, c.y - k * 0.7f, c.x + k, c.y - k * 0.7f, c.x, c.y + k);
            g.setColour (hover == PartFold ? kInk : kInk.withAlpha (0.7f));
            g.fillPath (p);
        }

        // Name and id.
        const auto nameR = partPx (PartName);
        g.setColour (kInk);
        g.setFont (font (16.0f * sc, true));
        const auto name = juce::String (device.displayName());
        g.drawText (name, nameR, juce::Justification::centredLeft);
        const float nw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name);
        g.setColour (kDim);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f * sc, juce::Font::plain)));
        juce::String sub = juce::String (device.rackId());
        if (device.folded)
            sub << "   " << u8 (RearPanel::kindLine (device.kind())) << "   " << deviceCableCount (view.proc.rack(), device) << " cables";
        g.drawText (sub, nameR.withTrimmedLeft (nw + 12.0f * sc).withWidth (900.0f * sc), juce::Justification::centredLeft);

        auto button = [&] (int p, const juce::String& text, bool on, juce::Colour onColour)
        {
            const auto b = partPx (p);
            if (b.isEmpty())
                return;
            g.setColour (on ? onColour : (hover == p ? juce::Colour (0xff3a3a40) : juce::Colour (0xff2c2c31)));
            g.fillRoundedRectangle (b, 3.0f * sc);
            g.setColour (juce::Colour (0x40ffffff));
            g.drawRoundedRectangle (b, 3.0f * sc, 1.0f);
            g.setColour (on ? juce::Colour (0xff121212) : kInk.withAlpha (0.9f));
            g.setFont (font (11.0f * sc, true));
            g.drawFittedText (text, b.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        };

        if (! device.folded)
        {
            button (PartOpenClose, device.closed ? juce::String::fromUTF8 ("\xe2\x96\xad CLOSED") : juce::String::fromUTF8 ("\xe2\x96\xa3 OPEN"), false, kGold);
            const auto names = tabNames (device.kind());
            const int t = view.tabOf (&device);
            for (int k = 0; k < names.size(); ++k)
                button (PartTab0 + k, names[k], k == t, kGold);
            const int n = frontCableCount (view.proc.rack(), device);
            button (PartBackBadge, juce::String::fromUTF8 ("\xe2\x87\x84 ") + juce::String (n) + (n == 1 ? " cable" : " cables")
                                       + juce::String::fromUTF8 (" \xc2\xb7 patch on BACK"), false, kGold);

            // LAT, view, BYPASS.
            const int lat = device.kind() == DeviceKind::RackIO ? view.proc.rack().latency() : device.latencySamples();
            g.setFont (font (11.5f * sc, true));
            g.setColour (lat > 0 ? kAmber : kDim);
            g.drawText ("LAT " + juce::String (lat), juce::Rectangle<float> (1196.0f * sc, 0.0f, 110.0f * sc, r.getHeight()), juce::Justification::centredLeft);
            g.setColour (kDim);
            g.drawText (view.showBack() ? "BACK" : "FRONT", juce::Rectangle<float> (1320.0f * sc, 0.0f, 100.0f * sc, r.getHeight()), juce::Justification::centredLeft);
            button (PartBypass, bypassed() ? "BYPASS ON" : "BYPASS", bypassed(), juce::Colour (0xffe0782a));
        }

        // Remove.
        {
            const auto b = partPx (PartRemove).reduced (2.0f * sc);
            if (! b.isEmpty())
            {
                g.setColour (hover == PartRemove ? juce::Colour (0xffc8322a) : juce::Colour (0xff121211));
                g.fillEllipse (b);
                g.setColour (juce::Colour (0x40ffffff));
                g.drawEllipse (b, 1.0f);
                g.setColour (kInk.withAlpha (hover == PartRemove ? 1.0f : 0.75f));
                const auto c = b.reduced (b.getWidth() * 0.32f);
                g.drawLine (c.getX(), c.getY(), c.getRight(), c.getBottom(), juce::jmax (1.0f, 2.0f * sc));
                g.drawLine (c.getRight(), c.getY(), c.getX(), c.getBottom(), juce::jmax (1.0f, 2.0f * sc));
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int p = partAt (e.position);
        setMouseCursor (p != PartNone && p != PartName ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor);
        if (p != hover)
        {
            hover = p;
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override { hover = PartNone; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        pressed = partAt (e.position);
        view.selectDevice (&device);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pressed == PartNone || pressed == PartName)
            Mount::startMove (view, device, *this, e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int p = pressed;
        pressed = PartNone;
        if (p == PartNone || p == PartName || partAt (e.position) != p || e.mouseWasDraggedSinceMouseDown())
            return;
        if (p == PartBypass)
        {
            toggleBypass();
            repaint();
            return;
        }
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RackView> (&view), d = &device, p]
        {
            if (sp == nullptr || sp->proc.rack().indexOf (d) < 0)
                return;
            if (p == PartFold) sp->setFolded (d, ! d->folded);
            else if (p == PartRemove) sp->removeDevice (d);
            else if (p == PartOpenClose) sp->setClosed (d, ! d->closed);
            else if (p == PartBackBadge) { sp->setShowBack (true); sp->scrollTo (d); }
            else if (p >= PartTab0) sp->setTab (d, p - PartTab0);
        });
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (partAt (e.position) == PartName)
            startRename();
        else if (partAt (e.position) == PartNone && device.folded)
            juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RackView> (&view), d = &device]
                                             { if (sp != nullptr && sp->proc.rack().indexOf (d) >= 0) sp->setFolded (d, false); });
    }

    void startRename()
    {
        editor = std::make_unique<juce::TextEditor>();
        editor->setFont (font (15.0f * s(), true));
        editor->setText (juce::String (device.displayName()), false);
        editor->setSelectAllWhenFocused (true);
        editor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d0d0f));
        editor->setColour (juce::TextEditor::textColourId, kInk);
        editor->setBounds (partPx (PartName).withWidth (260.0f * s()).reduced (0.0f, 3.0f * s()).toNearestInt());
        auto commit = [this]
        {
            if (editor == nullptr)
                return;
            const auto text = editor->getText().trim();
            juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RackView> (&view), d = &device, text]
                                             { if (sp != nullptr && sp->proc.rack().indexOf (d) >= 0) sp->renameDevice (d, text); });
        };
        editor->onReturnKey = commit;
        editor->onFocusLost = commit;
        editor->onEscapeKey = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RackView> (&view)] { if (sp != nullptr) sp->rebuild(); }); };
        addAndMakeVisible (*editor);
        editor->grabKeyboardFocus();
    }

    RackView& view;
    Device& device;
    int hover = PartNone, pressed = PartNone;
    std::unique_ptr<juce::TextEditor> editor;
};

// ---------------- RackView ----------------

RackView::RackView (JidaiProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (false);
    rebuild();
}

RackView::~RackView()
{
    cables.reset();
    slots.clear();
}

juce::StringArray RackView::tabNames (DeviceKind k)
{
    switch (k)
    {
        case DeviceKind::Bushido: return { "MAIN", "STEPS", "CLOCK", "MIDI", "SETUP" };
        case DeviceKind::Ronin: return { "MAIN" };
        case DeviceKind::Origami: return { "MAIN", "STAGES", "DYNAMICS", "SETUP" };
        case DeviceKind::Shogun: return { "MAIN" };
        case DeviceKind::RackIO: break;
    }
    return {};
}

const char* RackView::cableModeName (int mode)
{
    switch (mode)
    {
        case JidaiProcessor::CablesAll: return "ALL";
        case JidaiProcessor::CablesHidePassThru: return "HIDE PASS-THRU";
        case JidaiProcessor::CablesSelected: return "SELECTED";
        case JidaiProcessor::CablesHide: return "HIDE";
        default: break;
    }
    return "";
}

Device* RackView::slotDevice (int i) const
{
    return i >= 0 && i < (int) slots.size() ? slots[(size_t) i]->device : nullptr;
}

int RackView::indexOfSlotFor (const Device& d) const
{
    for (size_t i = 0; i < slots.size(); ++i)
        if (slots[i]->device == &d)
            return (int) i;
    return -1;
}

float RackView::unitsFor (const Device& d) const
{
    switch (d.kind())
    {
        case DeviceKind::RackIO: return 1.0f;
        case DeviceKind::Bushido: return proc.showBack ? 3.0f : (d.closed ? 1.0f : 3.0f);
        case DeviceKind::Ronin: return proc.showBack ? 4.0f : (d.closed ? 1.0f : 4.0f);
        case DeviceKind::Origami: return proc.showBack ? 1.0f : (d.closed ? 1.0f : 3.0f);
        // SHOGUN's 1200 x 672 MAIN page at the rack's 1600 width (896 + the 30 strip = 5.8 U); 153 jacks on a 4 U back.
        case DeviceKind::Shogun: return proc.showBack ? 4.0f : (d.closed ? 1.0f : 5.8f);
    }
    return 1.0f;
}

int RackView::tabOf (const Device* d) const
{
    const auto it = tabs.find (d);
    return it != tabs.end() ? it->second : 0;
}

float RackView::devicesHeight() const
{
    float h = 0.0f;
    for (auto& s : slots)
        h += s->height;
    return h;
}

int RackView::heightForWidth (int width, int minimum) const
{
    return juce::jmax (minimum, (int) std::ceil ((devicesHeight() + kEmptySpace) * (float) width / kDesignWidth));
}

juce::Rectangle<int> RackView::slotBounds (int index) const
{
    if (index < 0 || index >= (int) slots.size())
        return {};
    const float s = scale();
    const auto& slot = *slots[(size_t) index];
    return juce::Rectangle<float> (kSide * s, slot.top * s, (kDesignWidth - 2.0f * kSide) * s, slot.height * s).toNearestInt();
}

juce::Rectangle<int> RackView::stripBounds (int index) const
{
    if (index < 0 || index >= (int) slots.size())
        return {};
    const float s = scale();
    const auto& slot = *slots[(size_t) index];
    const float h = slot.device->folded ? kFoldHeight : kStripHeight;
    return juce::Rectangle<float> (kMargin * s, slot.top * s, kPanelWidth * s, h * s).toNearestInt();
}

juce::Rectangle<int> RackView::faceBounds (int index) const
{
    if (index < 0 || index >= (int) slots.size() || slots[(size_t) index]->device->folded)
        return {};
    const float s = scale();
    const auto& slot = *slots[(size_t) index];
    return juce::Rectangle<float> (kMargin * s, (slot.top + kStripHeight) * s, kPanelWidth * s, (slot.height - kStripHeight) * s).toNearestInt();
}

juce::Rectangle<int> RackView::partBounds (int index, int part) const
{
    if (index < 0 || index >= (int) slots.size())
        return {};
    const auto& st = *slots[(size_t) index]->strip;
    const auto r = st.partPx (part);
    return r.isEmpty() ? juce::Rectangle<int>() : r.toNearestInt() + st.getPosition();
}

juce::Component* RackView::faceComponent (int index) const
{
    return index >= 0 && index < (int) slots.size() ? slots[(size_t) index]->face.get() : nullptr;
}

void RackView::buildFace (Slot& slot)
{
    Device* d = slot.device;
    const float areaH = slot.height - kStripHeight;
    slot.faceY = 0.0f;
    slot.faceH = areaH;
    if (d->folded)
        return;
    if (proc.showBack)
    {
        auto rear = std::make_unique<RearPanel> (*d, areaH, [this, d]
        {
            return d->kind() == DeviceKind::RackIO ? proc.rack().latency() : d->latencySamples();
        });
        slot.rear = rear.get();
        slot.face = std::move (rear);
        return;
    }
    const auto ink = juce::Colour (0xffe8e2cf);
    switch (d->kind())
    {
        case DeviceKind::RackIO:
        {
            auto* io = static_cast<RackIODevice*> (d);
            std::vector<CompactFace::Item> items;
            items.push_back (CompactFace::Item::title ("RACK I/O", 250.0f, [] { return u8 ("host audio \xc2\xb7 MIDI \xc2\xb7 transport"); }));
            CompactFace::Item lcd (CompactFace::Item::Lcd, "", 330.0f);
            lcd.text = [io]
            {
                const auto t = io->transportView();
                if (! t.valid)
                    return juce::String ("NO HOST TRANSPORT");
                const double bar = std::floor (t.ppq / 4.0) + 1.0, beat = std::floor (std::fmod (t.ppq, 4.0)) + 1.0;
                return juce::String::fromUTF8 (t.playing ? "\xe2\x96\xb6 " : "\xe2\x96\xa0 ") + bpmText (t.bpm) + " BPM  BAR "
                       + juce::String ((int) bar) + "." + juce::String ((int) beat);
            };
            items.push_back (lcd);
            items.push_back (CompactFace::Item (CompactFace::Item::Space, {}, 30.0f));
            const char* names[] = { "IN L", "IN R", "OUT L", "OUT R" };
            for (int k = 0; k < 4; ++k)
            {
                CompactFace::Item m (CompactFace::Item::Meter, names[k], 110.0f);
                m.get = [io, k] { return (double) io->meter (k) * 5.0; };
                items.push_back (m);
            }
            items.push_back (CompactFace::Item (CompactFace::Item::Space, {}, 30.0f));
            CompactFace::Item level (CompactFace::Item::Knob, "MAIN", 100.0f);
            level.get = [io] { return (double) io->mainLevel() * 0.5; };
            level.set = [io] (double v) { io->setMainLevel ((float) (v * 2.0)); };
            level.text = [io] { return "MAIN " + juce::String (juce::Decibels::gainToDecibels (io->mainLevel(), -60.0f), 1) + " dB"; };
            items.push_back (level);
            CompactFace::Item lat (CompactFace::Item::Lcd, "", 300.0f);
            lat.text = [this] { return "LAT " + juce::String (proc.rack().latency()) + " smp\npath-aligned (JCS R11)"; };
            items.push_back (lat);
            slot.face = std::make_unique<CompactFace> (std::move (items),
                                                       CompactFace::Style { juce::Colour (0xff1f1f23), juce::Colour (0xff141417), ink, kGold });
            break;
        }
        case DeviceKind::Bushido:
        {
            auto* b = static_cast<BushidoDevice*> (d);
            slot.binding = std::make_unique<BushidoBinding> (*b);
            if (d->closed)
            {
                auto* bind = slot.binding.get();
                std::vector<CompactFace::Item> items;
                items.push_back (CompactFace::Item::title (juce::String (d->displayName()), 230.0f, [] { return u8 ("12-step sequencer \xc2\xb7 CLOSED"); }));
                CompactFace::Item lcd (CompactFace::Item::Lcd, "", 150.0f);
                lcd.text = [bind] { return bind->readoutText ({}) + (bind->readoutEnabled ({}) ? " BPM" : ""); };
                items.push_back (lcd);
                CompactFace::Item tempo (CompactFace::Item::Knob, "TEMPO", 90.0f);
                tempo.get = [b] { return (double) b->param ("CLOCK:TEMPO"); };
                tempo.set = [b] (double v) { b->setParam ("CLOCK:TEMPO", (float) v); };
                items.push_back (tempo);
                CompactFace::Item run (CompactFace::Item::Momentary, "START/STOP", 120.0f);
                run.get = [b] { return (double) b->indicator ("MODE:RUN"); };
                run.set = [b] (double v) { b->press ("MODE:START/STOP", v > 0.5); };
                items.push_back (run);
                CompactFace::Item lamps (CompactFace::Item::Lamps, "STEP", 480.0f);
                lamps.lamps = 12;
                lamps.lamp = [b] (int k) { return b->indicator ("STEP:" + std::to_string (k + 1)); };
                items.push_back (lamps);
                CompactFace::Item reset (CompactFace::Item::Momentary, "RESET", 100.0f);
                reset.set = [b] (double v) { b->press ("MODE:RESET", v > 0.5); };
                items.push_back (reset);
                CompactFace::Item lvl (CompactFace::Item::Knob, "LEVEL 1", 90.0f);
                lvl.get = [b] { return (double) b->param ("MIXER:LEVEL 1"); };
                lvl.set = [b] (double v) { b->setParam ("MIXER:LEVEL 1", (float) v); };
                items.push_back (lvl);
                slot.face = std::make_unique<CompactFace> (std::move (items),
                                                           CompactFace::Style { juce::Colour (0xff23211c), juce::Colour (0xff15140f), ink, kGold });
            }
            else if (tabOf (d) == 0)
            {
                auto layout = PanelLayout::fromJson (juce::String::fromUTF8 (BinaryData::bushido_layout_json, BinaryData::bushido_layout_jsonSize));
                layout.rack = juce::String (d->rackId());
                auto bg = juce::Drawable::createFromImageData (BinaryData::bushido_panel_bg_svg, BinaryData::bushido_panel_bg_svgSize);
                auto panel = std::make_unique<RackPanel> (layout, std::move (bg), *slot.binding);
                slot.panel = panel.get();
                slot.faceY = (areaH - layout.height) * 0.5f;
                slot.faceH = layout.height;
                slot.face = std::move (panel);
                slot.screen = std::make_unique<PatternScreen> (layout.screen, layout.width);
                slot.screen->names = [this] (int bank) { return proc.patternNames (bank); };
                slot.screen->loaded = [this, b] { return proc.loadedPattern (b); };
                slot.screen->choose = [this, b] (int bank, int index)
                {
                    proc.loadPattern (b, bank, index);
                    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RackView> (this)] { if (sp != nullptr) sp->reloadCables(); });
                };
                slot.screen->save = [this, b] (int bank, const juce::String& name) { return proc.savePattern (b, bank, name); };
            }
            else
            {
                slot.tabHost = std::make_unique<BushidoTabHost> (*b, proc, [] {});
                auto page = std::make_unique<bushido_ui::TabPage> (*slot.tabHost);
                page->setTab (tabOf (d));
                slot.faceY = (areaH - bushido_ui::TabPage::kDesignH) * 0.5f;
                slot.faceH = bushido_ui::TabPage::kDesignH;
                slot.face = std::move (page);
            }
            break;
        }
        case DeviceKind::Ronin:
        {
            auto* r = static_cast<RoninDevice*> (d);
            if (d->closed)
            {
                std::vector<CompactFace::Item> items;
                items.push_back (CompactFace::Item::title (juce::String (d->displayName()), 230.0f, [] { return u8 ("semi-modular voice \xc2\xb7 CLOSED"); }));
                CompactFace::Item lcd (CompactFace::Item::Lcd, "", 230.0f);
                lcd.text = [this, r]
                {
                    const auto names = proc.roninPresetNames (0);
                    const int p = r->program();
                    return p >= 0 && p < names.size() ? names[p] : juce::String ("PRESET");
                };
                items.push_back (lcd);
                const std::pair<const char*, const char*> knobs[] = { { "VCF", "CUTOFF" }, { "VCF", "PEAK" }, { "EG 1", "ATTACK" },
                                                                      { "EG 1", "RELEASE" }, { "OUTPUT", "MIX" }, { "OUTPUT", "LEVEL" } };
                for (auto& [sec, lab] : knobs)
                {
                    const int k = roninKnob (sec, lab);
                    if (k < 0)
                        continue;
                    CompactFace::Item it (CompactFace::Item::Knob, juce::String (sec) + " " + lab, 96.0f);
                    it.get = [r, k] { return (double) r->knob (k); };
                    it.set = [r, k] (double v) { r->setKnob (k, (float) v); };
                    it.defaultValue = kPanelKnobs[k].valueDefault;
                    items.push_back (it);
                }
                CompactFace::Item fx (CompactFace::Item::Toggle, "EFFECT", 110.0f);
                fx.get = [r] { return r->effectOn() ? 1.0 : 0.0; };
                fx.set = [r] (double v) { r->setEffectOn (v > 0.5); };
                items.push_back (fx);
                CompactFace::Item meter (CompactFace::Item::Meter, "OUTPUT", 120.0f);
                meter.get = [r] { return (double) r->meterVolts(); };
                items.push_back (meter);
                slot.face = std::make_unique<CompactFace> (std::move (items),
                                                           CompactFace::Style { juce::Colour (0xff222120), juce::Colour (0xff141312), ink, juce::Colour (0xffd0453a) });
            }
            else
            {
                auto panel = std::make_unique<RoninPanel> (*r, [] { return 0; }, [] (int) {});
                slot.panel = panel.get();
                slot.faceY = (areaH - RoninPanel::kHeight) * 0.5f;
                slot.faceH = RoninPanel::kHeight;
                slot.face = std::move (panel);
                slot.screen = std::make_unique<PatternScreen> (roninScreen(), RoninPanel::kWidth);
                slot.screen->names = [this] (int bank) { return proc.roninPresetNames (bank); };
                slot.screen->loaded = [this, r] { return proc.loadedPattern (r); };
                slot.screen->choose = [this, r] (int bank, int index)
                {
                    proc.loadRoninPreset (r, bank, index);
                    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RackView> (this)] { if (sp != nullptr) sp->reloadCables(); });
                };
                slot.screen->save = [this, r] (int bank, const juce::String& name) { return proc.saveRoninPreset (r, bank, name); };
            }
            break;
        }
        case DeviceKind::Shogun:
        {
            auto* sg = static_cast<ShogunDevice*> (d);
            if (d->closed)
            {
                std::vector<CompactFace::Item> items;
                items.push_back (CompactFace::Item::title (juce::String (d->displayName()), 230.0f, [] { return u8 ("drum machine \xc2\xb7 CLOSED"); }));
                CompactFace::Item lcd (CompactFace::Item::Lcd, "", 230.0f);
                lcd.text = [sg]
                {
                    return juce::String::fromUTF8 (sg->edits().pattern.name) + "\n" + juce::String::fromUTF8 (sg->running() ? "\xe2\x96\xb6 STEP " : "\xe2\x96\xa0 STEP ")
                           + juce::String (sg->step());
                };
                items.push_back (lcd);
                CompactFace::Item run (CompactFace::Item::Toggle, "RUN", 80.0f);
                run.get = [sg] { return sg->running() ? 1.0 : 0.0; };
                run.set = [sg] (double v) { sg->requestRun (v > 0.5); };
                items.push_back (run);
                for (const char* id : { "CLOCK:TEMPO", "CLOCK:SWING", "MASTER:ACCENT", "MASTER:DRIVE", "MASTER:GLUE", "MASTER:VOLUME" })
                {
                    const int p = sg->paramIndex (id);
                    if (p < 0)
                        continue;
                    CompactFace::Item it (CompactFace::Item::Knob, juce::String (id).fromFirstOccurrenceOf (":", false, false), 90.0f);
                    it.get = [sg, p] { return sg->param (p); };
                    it.set = [sg, p] (double v) { sg->setParam (p, v); };
                    it.defaultValue = shogun::kParams[p].def;
                    items.push_back (it);
                }
                CompactFace::Item meter (CompactFace::Item::Meter, "MIX", 110.0f);
                meter.get = [sg] { return (double) std::max (sg->meter (0), sg->meter (1)) * 5.0; };
                items.push_back (meter);
                slot.face = std::make_unique<CompactFace> (std::move (items),
                                                           CompactFace::Style { juce::Colour (0xff1b1d1b), juce::Colour (0xff0f110f), ink, juce::Colour (0xff4aa862) });
            }
            else
            {
                const float h = kPanelWidth * ShogunFace::kH / ShogunFace::kW;
                slot.faceY = (areaH - h) * 0.5f;
                slot.faceH = h;
                slot.face = std::make_unique<ShogunFace> (*sg);
            }
            break;
        }
        case DeviceKind::Origami:
        {
            auto* o = static_cast<OrigamiDevice*> (d);
            slot.origamiAccess = std::make_unique<OrigamiAccess> (*o, proc.rack());
            auto panel = std::make_unique<OrigamiPanel> (*slot.origamiAccess, d->closed ? OrigamiPanel::Mode::RackClosed : OrigamiPanel::Mode::RackOpen);
            panel->setPage ((origami::Page) juce::jlimit (0, 3, tabOf (d)));
            panel->onPageChange = [this, d] (origami::Page p) { tabs[d] = (int) p; };
            slot.origami = panel.get();
            slot.face = std::move (panel);
            break;
        }
    }
    if (slot.screen != nullptr)
    {
        slot.topLayer = std::make_unique<juce::Component>();
        slot.topLayer->setInterceptsMouseClicks (false, true);
        slot.topLayer->addAndMakeVisible (*slot.screen);
    }
}

void RackView::rebuild()
{
    if (cables != nullptr)
        removeChildComponent (cables.get());     // the layer stays (it may be mid-drag); only the devices are rebuilt
    slots.clear();
    spots.clear();

    auto& rack = proc.rack();
    if (selected != nullptr && rack.indexOf (selected) < 0)
        selected = nullptr;
    for (auto it = tabs.begin(); it != tabs.end();)
        it = rack.indexOf (it->first) < 0 ? tabs.erase (it) : std::next (it);

    float y = 0.0f;
    for (int i = 0; i < rack.deviceCount(); ++i)
    {
        Device* d = rack.device (i);
        auto slot = std::make_unique<Slot>();
        slot->device = d;
        slot->top = y;
        slot->height = d->folded ? kFoldHeight : unitsFor (*d) * kUnit;
        slot->mount = std::make_unique<Mount> (*this, *d);
        addAndMakeVisible (*slot->mount);
        buildFace (*slot);
        if (slot->face != nullptr)
            addAndMakeVisible (*slot->face);
        slot->strip = std::make_unique<Strip> (*this, *d);
        addAndMakeVisible (*slot->strip);
        y += slot->height;
        slots.push_back (std::move (slot));
    }
    for (auto& s : slots)
    {
        s->shade = std::make_unique<Shade>();
        addAndMakeVisible (*s->shade);
    }
    if (cables == nullptr)
        cables = std::make_unique<RackCableLayer> (*this, proc);
    addAndMakeVisible (*cables);
    for (auto& s : slots)
        if (s->topLayer != nullptr)
            addAndMakeVisible (*s->topLayer);

    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

void RackView::rebuildLater()
{
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RackView> (this)] { if (sp != nullptr) sp->rebuild(); });
}

void RackView::reloadCables()
{
    for (auto& slot : slots)
    {
        if (slot->screen != nullptr)
            slot->screen->repaint();
        slot->strip->repaint();
    }
    if (cables != nullptr)
        cables->refresh();
    repaint();
}

void RackView::setShowBack (bool back)
{
    if (proc.showBack == back)
        return;
    // Keep the device at the top of the window where it was (JIDAI_RACK_Redesign 3.3: the flip is anchored).
    auto* port = findParentComponentOfClass<juce::Viewport>();
    Device* anchor = nullptr;
    float offset = 0.0f;
    if (port != nullptr)
    {
        const float y = (float) port->getViewPositionY() / juce::jmax (0.001f, scale());
        for (auto& s : slots)
            if (y >= s->top && y < s->top + s->height)
            {
                anchor = s->device;
                offset = juce::jmin (y - s->top, kStripHeight);
            }
    }
    proc.showBack = back;
    if (cables != nullptr)
        cables->clearMessage();
    rebuild();
    if (port != nullptr && anchor != nullptr)
    {
        const int i = indexOfSlotFor (*anchor);
        if (i >= 0)
            port->setViewPosition (0, (int) std::round ((slots[(size_t) i]->top + offset) * scale()));
    }
    if (onViewChanged)
        onViewChanged();
}

void RackView::setCableMode (int mode)
{
    mode = juce::jlimit (0, 3, mode);
    if (proc.showBack)
        proc.cableModeBack = mode;
    else
        proc.cableModeFront = mode;
    if (cables != nullptr)
        cables->refresh();
    if (onViewChanged)
        onViewChanged();
}

void RackView::setClosed (Device* d, bool closed)
{
    if (proc.rack().indexOf (d) < 0 || d->kind() == DeviceKind::RackIO || d->closed == closed)
        return;
    d->closed = closed;
    rebuild();
}

void RackView::setFolded (Device* d, bool folded)
{
    if (proc.rack().indexOf (d) < 0 || d->folded == folded)
        return;
    d->folded = folded;
    rebuild();
}

bool RackView::allFolded() const
{
    for (auto& s : slots)
        if (! s->device->folded)
            return false;
    return ! slots.empty();
}

void RackView::foldAll (bool folded)
{
    for (int i = 0; i < proc.rack().deviceCount(); ++i)
        proc.rack().device (i)->folded = folded;
    rebuild();
}

void RackView::setTab (Device* d, int tab)
{
    if (proc.rack().indexOf (d) < 0)
        return;
    tabs[d] = juce::jlimit (0, juce::jmax (0, tabNames (d->kind()).size() - 1), tab);
    rebuild();
}

void RackView::removeDevice (Device* d)
{
    if (proc.rack().indexOf (d) < 0 || d->kind() == DeviceKind::RackIO)
        return;
    proc.removeDevice (d);
    rebuild();
}

void RackView::renameDevice (Device* d, const juce::String& name)
{
    if (proc.rack().indexOf (d) < 0)
        return;
    d->name = name == juce::String (d->title()) ? std::string() : name.toUpperCase().substring (0, 24).toStdString();
    rebuild();
}

void RackView::selectDevice (Device* d)
{
    if (selected == d)
        return;
    selected = d;
    for (auto& s : slots)
        s->strip->repaint();
    if (cables != nullptr)
        cables->refresh();
}

void RackView::scrollTo (Device* d)
{
    const int i = d != nullptr ? indexOfSlotFor (*d) : -1;
    if (i >= 0 && onScrollTo)
        onScrollTo (slotBounds (i));
}

const RackView::JackSpot* RackView::spotFor (const std::string& id) const
{
    for (auto& s : spots)
        if (s.id == id)
            return &s;
    return nullptr;
}

juce::String RackView::whyHidden (const std::string& id) const
{
    Device* d = nullptr;
    int j = -1;
    if (! proc.rack().resolve (id, d, j))
        return "missing";
    if (d->folded)
        return "folded";
    if (proc.showBack)
        return {};
    if (d->jacks()[(size_t) j].backOnly)
        return "back";
    if (d->closed)
        return "closed";
    if (tabOf (d) != 0)
        return "tab";
    return {};
}

void RackView::updateSpots()
{
    spots.clear();
    for (int i = 0; i < (int) slots.size(); ++i)
    {
        auto& slot = *slots[(size_t) i];
        Device* d = slot.device;
        if (d->folded || slot.face == nullptr)
            continue;
        const auto origin = slot.face->getPosition().toFloat();
        auto add = [&] (int j, juce::Point<float> p, float r)
        {
            if (j < 0 || j >= (int) d->jacks().size())
                return;
            JackSpot s;
            s.id = Rack::jackId (*d, j);
            s.p = origin + p;
            s.r = r;
            s.slot = i;
            s.jack = j;
            s.role = d->jackRole (j);
            s.out = d->jacks()[(size_t) j].desc.dir == PortDir::Out;
            spots.push_back (s);
        };
        if (slot.rear != nullptr)
        {
            for (int j = 0; j < (int) d->jacks().size(); ++j)
                add (j, slot.rear->jackCentre (j), slot.rear->jackRadius());
        }
        else if (slot.panel != nullptr)
        {
            const auto& L = slot.panel->layout();
            const float ps = (float) slot.panel->getWidth() / juce::jmax (1.0f, L.width);
            for (auto& jack : L.jacks)
                add (d->findJack (jack.id.toStdString()), { jack.x * ps, jack.y * ps }, juce::jmax (4.0f, jack.r * ps));
        }
        else if (slot.origami != nullptr && slot.origami->mode() == OrigamiPanel::Mode::RackOpen)
        {
            for (int k = 0; k < OrigamiPanel::kJackCount; ++k)
            {
                const auto c = slot.origami->jackCentre (k);
                if (c.x > 0.0f || c.y > 0.0f)
                    add (d->findJack (OrigamiPanel::jackId (k)), c, juce::jmax (4.0f, slot.origami->jackRadius()));
            }
        }
    }
}

void RackView::resized()
{
    const float s = scale();
    for (auto& slot : slots)
    {
        const int i = indexOfSlotFor (*slot->device);
        slot->mount->setBounds (slotBounds (i));
        slot->strip->setBounds (stripBounds (i));
        if (slot->face != nullptr)
        {
            const float top = slot->top + kStripHeight + slot->faceY;
            const auto r = juce::Rectangle<float> (kMargin * s, top * s, kPanelWidth * s, slot->faceH * s).toNearestInt();
            slot->face->setBounds (r);
            if (slot->topLayer != nullptr)
            {
                slot->topLayer->setBounds (r);
                slot->screen->placeIn ({ 0, 0, r.getWidth(), r.getHeight() });
            }
        }
        slot->shade->setBounds (juce::Rectangle<float> (kSide * s, slot->top * s, (kDesignWidth - 2.0f * kSide) * s, 10.0f * s).toNearestInt());
    }
    updateSpots();
    if (cables != nullptr)
    {
        cables->setBounds (getLocalBounds());
        cables->refresh();
    }
}

void RackView::paint (juce::Graphics& g)
{
    const float s = scale();
    const auto r = getLocalBounds().toFloat();
    const float emptyTop = devicesHeight() * s;

    g.setColour (juce::Colour (0xff0e0e10));
    g.fillRect (r);
    const auto interior = juce::Rectangle<float> (kMargin * s, emptyTop, kPanelWidth * s, r.getBottom() - emptyTop);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17171a), interior.getCentreX(), interior.getY(),
                                             juce::Colour (0xff0b0b0c), interior.getCentreX(), interior.getBottom(), false));
    g.fillRect (interior);

    for (int side = 0; side < 2; ++side)
    {
        const auto wall = side == 0 ? r.withWidth (kSide * s) : r.withTrimmedLeft (r.getWidth() - kSide * s);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff232326), wall.getX(), 0, juce::Colour (0xff141416), wall.getRight(), 0, false));
        g.fillRect (wall);
        g.setColour (juce::Colour (0xff000000));
        g.drawVerticalLine ((int) (side == 0 ? wall.getRight() - 1.0f : wall.getX()), 0.0f, r.getBottom());
    }
    for (int side = 0; side < 2; ++side)
    {
        const float x0 = side == 0 ? kSide * s : (kDesignWidth - kMargin) * s;
        const auto rail = juce::Rectangle<float> (x0, 0.0f, kRail * s, r.getHeight());
        juce::ColourGradient steel (juce::Colour (0xff3a3a3e), rail.getX(), 0, juce::Colour (0xff2a2a2d), rail.getRight(), 0, false);
        steel.addColour (0.5, juce::Colour (0xff47474c));
        g.setGradientFill (steel);
        g.fillRect (rail);
        const float hw = 13.0f * s, hh = 15.0f * s;
        for (float u = devicesHeight(); u * s < r.getBottom(); u += kUnit)
            for (float f : { 0.143f, 0.5f, 0.857f })
            {
                const auto hole = juce::Rectangle<float> (hw, hh).withCentre ({ rail.getCentreX(), (u + f * kUnit) * s });
                g.setColour (juce::Colour (0xff070708));
                g.fillRect (hole);
                g.setColour (juce::Colour (0x38ffffff));
                g.drawHorizontalLine ((int) hole.getBottom(), hole.getX(), hole.getRight());
            }
    }
    const auto shadow = juce::Rectangle<float> (kSide * s, emptyTop, (kDesignWidth - 2.0f * kSide) * s, 15.0f * s);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x8c000000), 0, shadow.getY(), juce::Colour (0x00000000), 0, shadow.getBottom(), false));
    g.fillRect (shadow);
    g.setColour (juce::Colour (0x66dcd6c2));
    g.setFont (font (juce::jmax (11.0f, 17.0f * s)));
    g.drawText (u8 (slots.size() <= 1 ? "Drag a device in from the browser \xc2\xb7 empty rack space"
                                      : "drop a device here \xc2\xb7 empty rack space"),
                interior.withHeight (juce::jmin (interior.getHeight(), kUnit * s)), juce::Justification::centred);
}

void RackView::paintOverChildren (juce::Graphics& g)
{
    if (insertAt < 0)
        return;
    float y = 0.0f;
    for (int i = 0; i < insertAt && i < (int) slots.size(); ++i)
        y += slots[(size_t) i]->height;
    const float s = scale(), py = y * s;
    const auto line = juce::Rectangle<float> (kSide * s, juce::jmax (0.0f, py - 2.0f), (kDesignWidth - 2.0f * kSide) * s, 4.0f);
    g.setColour (kAmber.withAlpha (0.33f));
    g.fillRect (line.expanded (0.0f, 4.0f));
    g.setColour (kAmber);
    g.fillRect (line);
}

void RackView::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == this)
    {
        selectDevice (nullptr);
        if (cables != nullptr)
            cables->selectCable (-1);
    }
}

// ---------------- drag and drop ----------------

bool RackView::isInterestedInDragSource (const SourceDetails& d)
{
    const auto s = d.description.toString();
    return s.startsWith ("add:") || s.startsWith ("move:");
}

int RackView::insertionIndex (float localY) const
{
    const float y = localY / juce::jmax (0.001f, scale());
    for (size_t i = 0; i < slots.size(); ++i)
        if (y < slots[i]->top + slots[i]->height * 0.5f)
            return juce::jmax (proc.rack().rackIO() != nullptr ? 1 : 0, (int) i);
    return (int) slots.size();
}

void RackView::itemDragEnter (const SourceDetails& d) { itemDragMove (d); }

void RackView::itemDragMove (const SourceDetails& d)
{
    const int at = insertionIndex ((float) d.localPosition.y);
    if (at != insertAt)
    {
        insertAt = at;
        repaint();
    }
}

void RackView::itemDragExit (const SourceDetails&)
{
    insertAt = -1;
    repaint();
}

void RackView::itemDropped (const SourceDetails& d)
{
    const auto s = d.description.toString();
    const int at = insertionIndex ((float) d.localPosition.y);
    insertAt = -1;
    const bool route = ! juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown();     // Shift: no auto-route
    if (s.startsWith ("add:"))
    {
        const auto what = s.fromFirstOccurrenceOf (":", false, false);
        if (what == "RONIN FX")
            proc.addDevice (DeviceKind::Ronin, at, route, true);
        else
        {
            DeviceKind kind;
            if (deviceKindFromName (what.toStdString(), kind) != nullptr && ! (kind == DeviceKind::RackIO && proc.rack().rackIO() != nullptr))
                proc.addDevice (kind, at, route);
        }
    }
    else if (s.startsWith ("move:"))
    {
        if (Device* dev = proc.rack().findDevice (s.fromFirstOccurrenceOf (":", false, false).toStdString()))
        {
            const int from = proc.rack().indexOf (dev);
            proc.moveDevice (dev, at > from ? at - 1 : at);
        }
    }
    if (onDropped)
        onDropped (s);
    rebuildLater();
}
