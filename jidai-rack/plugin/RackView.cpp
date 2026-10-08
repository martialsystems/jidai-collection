// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "RackView.h"
#include "BinaryData.h"

using namespace jidai;

namespace {

std::vector<jidai::CableSpec> toRack (const std::vector<::CableSpec>& specs)
{
    std::vector<jidai::CableSpec> out;
    for (auto& c : specs)
        out.push_back ({ c.a.toStdString(), c.b.toStdString(), c.color, c.age });
    return out;
}

std::vector<::CableSpec> toLayer (const std::vector<jidai::CableSpec>& specs)
{
    std::vector<::CableSpec> out;
    for (auto& c : specs)
        out.push_back ({ juce::String (c.a), juce::String (c.b), c.color, c.age });
    return out;
}

// RONIN's layout has the glass, the LCD and the dropdown key, and no banks or SAVE.
// Those sit on the same x and y as BUSHIDO's. 17 characters fits "A013 DELAY BOUNCE".
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

}

// ---------------- rack hardware ----------------

namespace {

const juce::Colour kInk { 0xffe8e2cf };

void paintScrew (juce::Graphics& g, juce::Point<float> c, float r, float angle)
{
    g.setColour (juce::Colour (0xaa000000));                                  // countersink shadow
    g.fillEllipse (juce::Rectangle<float> (r * 2.5f, r * 2.5f).withCentre (c.translated (0.0f, r * 0.15f)));
    juce::ColourGradient metal (juce::Colour (0xffe6e4dc), c.x - r * 0.6f, c.y - r * 0.7f,
                                juce::Colour (0xff5d5c58), c.x + r * 0.7f, c.y + r * 0.8f, true);
    g.setGradientFill (metal);
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (juce::Colour (0xff2a2a28));
    g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), juce::jmax (0.6f, r * 0.12f));
    // Phillips cross
    const float len = r * 0.62f, wdt = juce::jmax (1.0f, r * 0.26f);
    for (int k = 0; k < 2; ++k)
    {
        const float a = angle + (float) k * juce::MathConstants<float>::halfPi;
        const juce::Point<float> d (std::cos (a) * len, std::sin (a) * len);
        g.setColour (juce::Colour (0xff3a3936));
        g.drawLine ({ c - d, c + d }, wdt);
        g.setColour (juce::Colour (0x55ffffff));
        g.drawLine ({ c - d + juce::Point<float> (0.0f, wdt * 0.6f), c + d + juce::Point<float> (0.0f, wdt * 0.6f) }, wdt * 0.35f);
    }
}

// A device's ear: dark brushed steel the colour of its faceplate, with a bevel.
void paintEar (juce::Graphics& g, juce::Rectangle<float> r, bool left, float s)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff302e2a), left ? r.getX() : r.getRight(), r.getY(),
                                             juce::Colour (0xff191816), left ? r.getRight() : r.getX(), r.getY(), false));
    g.fillRect (r);
    juce::Random grain (left ? 7 : 11);                                        // brushed lines, the same every paint
    for (float y = r.getY() + 1.0f; y < r.getBottom(); y += juce::jmax (1.5f, 2.2f * s))
    {
        g.setColour (juce::Colour (grain.nextBool() ? 0x0cffffff : 0x10000000));
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
    }
    g.setColour (juce::Colour (0x26ffffff));
    g.drawVerticalLine ((int) (left ? r.getX() : r.getRight() - 1.0f), r.getY(), r.getBottom());
    g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight());
    g.setColour (juce::Colour (0xcc000000));
    g.drawHorizontalLine ((int) (r.getBottom() - 1.0f), r.getX(), r.getRight());
    g.drawVerticalLine ((int) (left ? r.getRight() - 1.0f : r.getX()), r.getY(), r.getBottom());
}

}

// ---------------- the mount of one device: ears, screws, fold arrow, name, remove button ----------------
// It spans the rack from rail to rail, behind the device's panel, so it is reached on the ears; folded, it is the
// whole strip.

class RackView::Mount : public juce::Component
{
public:
    Mount (RackView& v, Device& d) : view (v), device (d) {}

    float s() const { return view.scale(); }
    float ear() const { return kRail * s(); }
    bool folded() const { return device.folded; }

    juce::Rectangle<float> foldBox() const
    {
        const float b = 28.0f * s();
        return folded() ? juce::Rectangle<float> (b, b).withCentre ({ ear() + 26.0f * s(), (float) getHeight() * 0.5f })
                        : juce::Rectangle<float> (b, b).withCentre ({ ear() * 0.5f, 60.0f * s() });
    }
    juce::Rectangle<float> removeBox() const
    {
        const float b = 28.0f * s();
        return folded() ? juce::Rectangle<float> (b, b).withCentre ({ (float) getWidth() - ear() - 26.0f * s(), (float) getHeight() * 0.5f })
                        : juce::Rectangle<float> (b, b).withCentre ({ (float) getWidth() - ear() * 0.5f, 60.0f * s() });
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float sc = s(), e = ear();
        const auto leftEar = r.withWidth (e), rightEar = r.withTrimmedLeft (r.getWidth() - e);

        if (folded())
        {
            auto face = r.reduced (e, 0.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2925), 0, face.getY(), juce::Colour (0xff151513), 0, face.getBottom(), false));
            g.fillRect (face);
            g.setColour (juce::Colour (0x22ffffff));
            g.drawHorizontalLine ((int) face.getY(), face.getX(), face.getRight());
            g.setColour (juce::Colour (0xdd000000));
            g.drawHorizontalLine ((int) (face.getBottom() - 1.0f), face.getX(), face.getRight());
            g.setColour (kInk);
            g.setFont (juce::FontOptions (22.0f * sc, juce::Font::bold));
            const auto text = face.withTrimmedLeft (56.0f * sc);
            g.drawText (juce::String (device.title()), text, juce::Justification::centredLeft);
            g.setColour (kInk.withAlpha (0.55f));
            g.setFont (juce::FontOptions (15.0f * sc));
            g.drawText (device.kind() == DeviceKind::Bushido ? "3 x 12 step sequencer" : "modular synthesizer",
                        text.withTrimmedLeft (190.0f * sc), juce::Justification::centredLeft);
        }

        paintEar (g, leftEar, true, sc);
        paintEar (g, rightEar, false, sc);

        // Screws on the rail holes: top and bottom of each ear (one each, centred, when folded).
        const float sr = 8.5f * sc;
        const float salt = (float) device.number * 0.7f + (device.kind() == DeviceKind::Ronin ? 0.4f : 0.0f);
        auto screwsAt = [&] (float y, float a) {
            paintScrew (g, { leftEar.getCentreX(), y }, sr, a);
            paintScrew (g, { rightEar.getCentreX(), y }, sr, a + 0.9f);
        };
        if (folded())
            screwsAt (r.getCentreY(), salt);
        else
        {
            screwsAt (24.0f * sc, salt);
            screwsAt (r.getBottom() - 24.0f * sc, salt + 1.3f);
        }

        // Fold arrow: down while open, right while folded.
        {
            const auto b = foldBox();
            g.setColour (hover == 1 ? kInk : kInk.withAlpha (0.7f));
            juce::Path p;
            const auto c = b.getCentre();
            const float k = b.getWidth() * 0.28f;
            if (folded())
                p.addTriangle (c.x - k * 0.7f, c.y - k, c.x - k * 0.7f, c.y + k, c.x + k, c.y);
            else
                p.addTriangle (c.x - k, c.y - k * 0.7f, c.x + k, c.y - k * 0.7f, c.x, c.y + k);
            g.fillPath (p);
        }

        // Remove: a small round button with an x.
        {
            const auto b = removeBox().reduced (3.0f * sc);
            g.setColour (hover == 2 ? juce::Colour (0xffc8322a) : juce::Colour (0xff121211));
            g.fillEllipse (b);
            g.setColour (juce::Colour (0x40ffffff));
            g.drawEllipse (b, 1.0f);
            g.setColour (hover == 2 ? kInk : kInk.withAlpha (0.75f));
            const auto c = b.reduced (b.getWidth() * 0.32f);
            g.drawLine (c.getX(), c.getY(), c.getRight(), c.getBottom(), juce::jmax (1.0f, 2.2f * sc));
            g.drawLine (c.getRight(), c.getY(), c.getX(), c.getBottom(), juce::jmax (1.0f, 2.2f * sc));
        }

        // The device's name runs up the left ear, as on a real rack unit.
        if (! folded())
        {
            const auto area = leftEar.withTrimmedTop (90.0f * sc).withTrimmedBottom (50.0f * sc);
            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, area.getCentreX(), area.getCentreY()));
            g.setColour (kInk.withAlpha (0.78f));
            g.setFont (juce::FontOptions (17.0f * sc, juce::Font::bold));
            g.drawText (juce::String (device.title()), juce::Rectangle<float> (area.getHeight(), area.getWidth()).withCentre (area.getCentre()),
                        juce::Justification::centred);
        }
    }

    int partAt (juce::Point<float> p) const
    {
        if (foldBox().expanded (4.0f).contains (p)) return 1;
        if (removeBox().contains (p)) return 2;
        return 0;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int part = partAt (e.position);
        setMouseCursor (part != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor);
        if (part != hover)
        {
            hover = part;
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override { hover = 0; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override { pressed = partAt (e.position); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pressed != 0 || e.getDistanceFromDragStart() < 6)
            return;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            if (container->isDragAndDropActive())
                return;
            // The drag image is the device itself, shrunk.
            const auto area = view.slotBounds (view.indexOfSlotFor (device));
            auto image = view.createComponentSnapshot (area, true, 1.0f);
            const int w = juce::jmin (360, image.getWidth());
            const int h = juce::jmax (1, image.getHeight() * w / juce::jmax (1, image.getWidth()));
            image = image.rescaled (w, h);
            image.multiplyAllAlphas (0.8f);
            container->startDragging ("move:" + juce::String (device.rackId()), this, juce::ScaledImage (image), false, nullptr, &e.source);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const int part = pressed;
        pressed = 0;
        if (part == 0 || partAt (e.position) != part)
            return;
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RackView> (&view), d = &device, part]
        {
            if (sp == nullptr)
                return;
            if (part == 1)
                sp->setFolded (d, ! d->folded);
            else
                sp->removeDevice (d);
        });
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (partAt (e.position) == 0 && folded())
            juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RackView> (&view), d = &device]
                                             { if (sp != nullptr) sp->setFolded (d, false); });
    }

    RackView& view;
    Device& device;
    int hover = 0, pressed = 0;
};

// ---------------- BUSHIDO's panel controls, bound to its engine ----------------

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
    // The BPM readout: the TEMPO parameter shown as BPM at the DIV setting (BUSHIDO's editor does the same).
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

// Four cable colours in BUSHIDO's top bar, where its own editor puts them. They set the rack's cable colour.
class RackView::Swatches : public juce::Component
{
public:
    Swatches (std::function<int()> g, std::function<void (int)> s) : getColour (std::move (g)), setColour (std::move (s)) {}
    void paint (juce::Graphics& g) override
    {
        const float d = (float) getHeight() * 0.62f;
        for (int i = 0; i < 4; ++i)
        {
            auto r = cell (i).withSizeKeepingCentre (d, d);
            g.setColour (CableLayer::cableColour (i, 0));
            g.fillEllipse (r);
            if (i == getColour())
            {
                g.setColour (juce::Colours::white);
                g.drawEllipse (r.expanded (2.5f), 1.5f);
            }
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 4; ++i)
            if (cell (i).contains (e.position))
                setColour (i);
    }

private:
    std::function<int()> getColour;
    std::function<void (int)> setColour;
    juce::Rectangle<float> cell (int i) const
    {
        const float w = (float) getWidth() / 4.0f;
        return { w * (float) i, 0, w, (float) getHeight() };
    }
};

// The shadow the unit above (or the rack's top) casts on a device. It is short: it fades out where the device's top
// screws begin, since the units are mounted upright and sit flush.
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

// ---------------- slots ----------------

struct RackView::Slot {
    Device* device = nullptr;
    std::unique_ptr<Mount> mount;
    std::unique_ptr<RackPanel> panel;
    std::unique_ptr<Shade> shade;
    std::unique_ptr<BushidoBinding> binding;
    std::unique_ptr<juce::Component> topLayer;      // pattern screen (and BUSHIDO's swatches), above the cables
    std::unique_ptr<PatternScreen> screen;
    std::unique_ptr<Swatches> swatches;
    float top = 0.0f;           // design units
    float height = 0.0f;        // design units: the open panel
    float shown() const { return device->folded ? kFoldHeight : height; }
};

// Passes a child's pointer events to a component that watches them all. Wheel events are not passed: a component's
// default mouseWheelMove hands the wheel to its parent, so a watching component would scroll the rack for every wheel
// a child had already used (the preset list, a knob).
struct RackView::PointerRelay final : public juce::MouseListener
{
    explicit PointerRelay (juce::Component& c) : to (c) {}
    void mouseMove (const juce::MouseEvent& e) override        { to.mouseMove (e); }
    void mouseEnter (const juce::MouseEvent& e) override       { to.mouseEnter (e); }
    void mouseExit (const juce::MouseEvent& e) override        { to.mouseExit (e); }
    void mouseDown (const juce::MouseEvent& e) override        { to.mouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override        { to.mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override          { to.mouseUp (e); }
    void mouseDoubleClick (const juce::MouseEvent& e) override { to.mouseDoubleClick (e); }
    juce::Component& to;
};

RackView::RackView (JidaiProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (false);
    selfRelay = std::make_unique<PointerRelay> (*this);
    addMouseListener (selfRelay.get(), true);       // RONIN's meter follows the jack pressed, even under the cable layer
    rebuild();
}

RackView::~RackView()
{
    if (cableRelay != nullptr)
        removeMouseListener (cableRelay.get());
    removeMouseListener (selfRelay.get());
}

int RackView::indexOfSlotFor (const Device& d) const
{
    for (size_t i = 0; i < slots.size(); ++i)
        if (slots[i]->device == &d)
            return (int) i;
    return -1;
}

float RackView::devicesHeight() const
{
    float h = 0.0f;
    for (auto& s : slots)
        h += s->shown();
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
    return juce::Rectangle<float> (kSide * s, slot.top * s, (kDesignWidth - 2.0f * kSide) * s, slot.shown() * s).toNearestInt();
}

juce::Rectangle<int> RackView::removeButtonBounds (int index) const
{
    if (index < 0 || index >= (int) slots.size())
        return {};
    const auto& m = *slots[(size_t) index]->mount;
    return m.removeBox().toNearestInt() + m.getPosition();
}

juce::Rectangle<int> RackView::foldButtonBounds (int index) const
{
    if (index < 0 || index >= (int) slots.size())
        return {};
    const auto& m = *slots[(size_t) index]->mount;
    return m.foldBox().toNearestInt() + m.getPosition();
}

void RackView::rebuild()
{
    if (cableRelay != nullptr)
        removeMouseListener (cableRelay.get());
    cableRelay.reset();
    cables.reset();
    slots.clear();

    auto& rack = proc.rack();
    float y = 0.0f;
    for (int i = 0; i < rack.deviceCount(); ++i)
    {
        Device* d = rack.device (i);
        auto slot = std::make_unique<Slot>();
        slot->device = d;
        slot->top = y;
        slot->mount = std::make_unique<Mount> (*this, *d);
        addAndMakeVisible (*slot->mount);
        if (auto* r = dynamic_cast<RoninDevice*> (d))
        {
            auto panel = std::make_unique<RoninPanel> (*r, [this] { return colour; },
                                                       [this] (int c) { colour = c; if (cables) cables->setColour (c); repaint(); });
            slot->height = RoninPanel::kHeight;
            slot->panel = std::move (panel);

            slot->topLayer = std::make_unique<juce::Component>();
            slot->topLayer->setInterceptsMouseClicks (false, true);
            slot->screen = std::make_unique<PatternScreen> (roninScreen(), RoninPanel::kWidth);
            slot->screen->names = [this] (int bank) { return proc.roninPresetNames (bank); };
            slot->screen->loaded = [this, r] { return proc.loadedPattern (r); };
            slot->screen->choose = [this, r] (int bank, int index)
            {
                proc.loadRoninPreset (r, bank, index);
                juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RackView> (this)] { if (sp != nullptr) sp->reloadCables(); });
            };
            slot->screen->save = [this, r] (int bank, const juce::String& name) { return proc.saveRoninPreset (r, bank, name); };
            slot->topLayer->addAndMakeVisible (*slot->screen);
        }
        else if (auto* b = dynamic_cast<BushidoDevice*> (d))
        {
            auto layout = PanelLayout::fromJson (juce::String::fromUTF8 (BinaryData::bushido_layout_json, BinaryData::bushido_layout_jsonSize));
            layout.rack = juce::String (d->rackId());          // BUSHIDO#N: one BUSHIDO's jacks among many
            auto bg = juce::Drawable::createFromImageData (BinaryData::bushido_panel_bg_svg, BinaryData::bushido_panel_bg_svgSize);
            slot->binding = std::make_unique<BushidoBinding> (*b);
            slot->height = layout.height;
            slot->panel = std::make_unique<RackPanel> (layout, std::move (bg), *slot->binding);

            slot->topLayer = std::make_unique<juce::Component>();
            slot->topLayer->setInterceptsMouseClicks (false, true);
            slot->screen = std::make_unique<PatternScreen> (layout.screen, layout.width);
            slot->screen->names = [this] (int bank) { return proc.patternNames (bank); };
            slot->screen->loaded = [this, b] { return proc.loadedPattern (b); };
            slot->screen->choose = [this, b] (int bank, int index)
            {
                proc.loadPattern (b, bank, index);
                juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RackView> (this)] { if (sp != nullptr) sp->reloadCables(); });
            };
            slot->screen->save = [this, b] (int bank, const juce::String& name) { return proc.savePattern (b, bank, name); };
            slot->topLayer->addAndMakeVisible (*slot->screen);
            slot->swatches = std::make_unique<Swatches> ([this] { return colour; },
                                                         [this] (int c) { colour = c; if (cables) cables->setColour (c); repaint(); });
            slot->topLayer->addAndMakeVisible (*slot->swatches);
        }
        addChildComponent (*slot->panel);
        slot->panel->setVisible (! d->folded);
        y += slot->shown();
        slots.push_back (std::move (slot));
    }
    for (auto& s : slots)                    // over every panel and ear, under the cables
    {
        s->shade = std::make_unique<Shade>();
        addAndMakeVisible (*s->shade);
    }

    // One cable layer over the panels' column. A folded device's panel is not on it: its cables stay in the rack,
    // hidden until it is opened again.
    cables = std::make_unique<CableLayer>();
    cables->setColour (colour);
    cables->setDesignSize (kPanelWidth, juce::jmax (1.0f, devicesHeight() + kEmptySpace));
    for (auto& s : slots)
        if (! s->device->folded)
            cables->addRack (s->panel.get(), s->top);
    cables->setPatch (toLayer (proc.rack().cables()));
    cables->onPatchChanged = [this] (const std::vector<::CableSpec>& c)
    {
        auto patch = toRack (c);
        for (auto& kept : proc.rack().cables())
            if (cables->jackIndex (juce::String (kept.a)) < 0 || cables->jackIndex (juce::String (kept.b)) < 0)
                patch.push_back (kept);
        proc.setCables (patch);
    };
    addAndMakeVisible (*cables);
    cableRelay = std::make_unique<PointerRelay> (*cables);
    addMouseListener (cableRelay.get(), true);     // cables see the pointer everywhere (hover push-away), as in BUSHIDO's editor

    for (auto& s : slots)
        if (s->topLayer != nullptr)
        {
            addChildComponent (*s->topLayer);
            s->topLayer->setVisible (! s->device->folded);
        }

    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

void RackView::reloadCables()
{
    if (cables != nullptr)
        cables->setPatch (toLayer (proc.rack().cables()));
    for (auto& slot : slots)
        if (slot->screen != nullptr)
            slot->screen->repaint();
    repaint();
}

void RackView::removeDevice (Device* d)
{
    if (proc.rack().indexOf (d) < 0)
        return;
    proc.removeDevice (d);
    rebuild();
}

void RackView::setFolded (Device* d, bool folded)
{
    if (proc.rack().indexOf (d) < 0 || d->folded == folded)
        return;
    d->folded = folded;
    rebuild();
}

void RackView::resized()
{
    const float s = scale();
    for (auto& slot : slots)
    {
        slot->mount->setBounds (slotBounds (indexOfSlotFor (*slot->device)));
        const auto panel = juce::Rectangle<float> (kMargin * s, slot->top * s, kPanelWidth * s, slot->height * s).toNearestInt();
        slot->panel->setBounds (panel);
        // The shadow runs over ears and panel and fades out where the top screws begin.
        slot->shade->setBounds (juce::Rectangle<float> (kSide * s, slot->top * s, (kDesignWidth - 2.0f * kSide) * s,
                                                        slot->device->folded ? 10.0f * s : 15.0f * s).toNearestInt());
        if (slot->topLayer != nullptr)
        {
            slot->topLayer->setBounds (panel);
            slot->screen->placeIn ({ 0, 0, panel.getWidth(), panel.getHeight() });
            if (slot->swatches != nullptr)
                slot->swatches->setBounds (juce::Rectangle<float> (1450 * s, 12 * s, 136 * s, 30 * s).toNearestInt());
        }
    }
    if (cables != nullptr)
    {
        const float h = juce::jmax (devicesHeight() + kEmptySpace, (float) getHeight() / juce::jmax (0.001f, s));
        cables->setDesignSize (kPanelWidth, h);                // cables can hang down into the empty rack
        cables->setBounds (juce::Rectangle<float> (kMargin * s, 0.0f, kPanelWidth * s, h * s).toNearestInt());
    }
}

void RackView::paint (juce::Graphics& g)
{
    const float s = scale();
    const auto r = getLocalBounds().toFloat();
    const float emptyTop = devicesHeight() * s;

    // The back of the cabinet, seen through the empty rack space.
    g.setColour (juce::Colour (0xff0e0e10));
    g.fillRect (r);
    const auto interior = juce::Rectangle<float> (kMargin * s, emptyTop, kPanelWidth * s, r.getBottom() - emptyTop);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17171a), interior.getCentreX(), interior.getY(),
                                             juce::Colour (0xff0b0b0c), interior.getCentreX(), interior.getBottom(), false));
    g.fillRect (interior);

    // Cabinet walls.
    for (int side = 0; side < 2; ++side)
    {
        const auto wall = side == 0 ? r.withWidth (kSide * s) : r.withTrimmedLeft (r.getWidth() - kSide * s);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff232326), wall.getX(), 0, juce::Colour (0xff141416), wall.getRight(), 0, false));
        g.fillRect (wall);
        g.setColour (juce::Colour (0xff000000));
        g.drawVerticalLine ((int) (side == 0 ? wall.getRight() - 1.0f : wall.getX()), 0.0f, r.getBottom());
    }

    // Rails, with the three holes of every rack unit. Devices' ears cover them; the empty space shows them.
    for (int side = 0; side < 2; ++side)
    {
        const float x0 = side == 0 ? kSide * s : (kDesignWidth - kMargin) * s;
        const auto rail = juce::Rectangle<float> (x0, 0.0f, kRail * s, r.getHeight());
        juce::ColourGradient steel (juce::Colour (0xff3a3a3e), rail.getX(), 0, juce::Colour (0xff2a2a2d), rail.getRight(), 0, false);
        steel.addColour (0.5, juce::Colour (0xff47474c));
        g.setGradientFill (steel);
        g.fillRect (rail);
        g.setColour (juce::Colour (0x30ffffff));
        g.drawVerticalLine ((int) rail.getX(), 0.0f, r.getBottom());
        g.setColour (juce::Colour (0xcc000000));
        g.drawVerticalLine ((int) rail.getRight() - 1, 0.0f, r.getBottom());

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

    // The bottom unit's shadow on the empty space under it: as short as a device's own top shadow.
    if (! slots.empty())
    {
        const auto shadow = juce::Rectangle<float> (kSide * s, emptyTop, (kDesignWidth - 2.0f * kSide) * s, 15.0f * s);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x8c000000), 0, shadow.getY(), juce::Colour (0x00000000), 0, shadow.getBottom(), false));
        g.fillRect (shadow);
    }
    else
    {
        g.setColour (juce::Colour (0x88dcd6c2));
        g.setFont (juce::FontOptions (juce::jmax (12.0f, 24.0f * s)));
        g.drawText ("Empty rack. Drag BUSHIDO or RONIN in from the device list.", interior.withHeight (juce::jmin (interior.getHeight(), 260.0f * s)),
                    juce::Justification::centred);
    }
}

void RackView::paintOverChildren (juce::Graphics& g)
{
    if (insertAt < 0)
        return;
    float y = 0.0f;
    for (int i = 0; i < insertAt && i < (int) slots.size(); ++i)
        y += slots[(size_t) i]->shown();
    const float s = scale(), py = y * s;
    const auto line = juce::Rectangle<float> (kSide * s, juce::jmax (0.0f, py - 2.0f), (kDesignWidth - 2.0f * kSide) * s, 4.0f);
    g.setColour (juce::Colour (0x55eabd2c));
    g.fillRect (line.expanded (0.0f, 4.0f));
    g.setColour (juce::Colour (0xffeabd2c));
    g.fillRect (line);
}

void RackView::mouseDown (const juce::MouseEvent& e)
{
    // RONIN's meter reads the jack last pressed (RONIN's PatchBayView does this on mouse down on a jack).
    if (cables == nullptr || e.eventComponent != cables.get())
        return;
    const auto p = e.getEventRelativeTo (cables.get()).position / scale();
    for (auto& slot : slots)
    {
        auto* r = dynamic_cast<RoninDevice*> (slot->device);
        if (r == nullptr || r->folded || p.y < slot->top || p.y > slot->top + slot->height)
            continue;
        const float py = p.y - slot->top;
        for (int j = 0; j < kPanelJackCount; ++j)
            if (std::hypot (kPanelJacks[j].x - p.x, kPanelJacks[j].y - py) < 17.0f)
                r->setMeterJack (j);
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
        if (y < slots[i]->top + slots[i]->shown() * 0.5f)
            return (int) i;
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
    if (s == "add:BUSHIDO" || s == "add:RONIN")
    {
        proc.addDevice (s == "add:RONIN" ? DeviceKind::Ronin : DeviceKind::Bushido, at);
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
    // Rebuilt after the drag has finished: the mount that started a move is one of the components replaced.
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RackView> (this)] { if (sp != nullptr) sp->rebuild(); });
}
