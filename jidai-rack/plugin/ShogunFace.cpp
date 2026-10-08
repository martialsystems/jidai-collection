// Copyright (c) 2026 Martial Systems LLC. All rights reserved.

#include "ShogunFace.h"
#include "ShogunState.h"

#include <cmath>
#include <cstdio>
#include <functional>

using namespace shogun;

namespace {

struct LayoutOp {
    int kind, tab, flags;
    float x, y, w, h, r, z, v;
    std::uint32_t fill, stroke;
    float sw, opacity;
    const char* text;
    const char* text2;
    const char* bind;
    int steps, ticks;
};

[[maybe_unused]] constexpr float kNoRing = 0.0f;   // referenced by the generated table
// SHOGUN's generated panel table (plugin/Source/PanelLayout.inc at the pinned SHOGUN commit; include path from CMake).
const LayoutOp kOps[] = {
#include "PanelLayout.inc"
};
constexpr int kOpCount = (int) (sizeof kOps / sizeof kOps[0]);

// ---- drawing primitives: the SHOGUN editor's (PluginEditor.cpp), unchanged ----
enum OpKind { TEXT, RTEXT, RULE, BOX, KNOB, LED, KEY, LCD, TOGGLE, JACK, RECT, CIRCLE, LINE, PATH };
enum Flag { kAnchor = 3, kOn = 4, kOut = 8, kRound = 16, kBold = 32 };

const juce::Colour INK(0xFFF2F1EA), DIM(0xFF9A9A90), GRN(0xFF4AA862), RED(0xFFE0402E), AMB(0xFFF0B030);
const juce::Colour LCDGRN(0xFF7EE08C), METERGRN(0xFF37C25A);

int voiceIndex(const juce::String& s) {
  for (int v = 0; v < kVoices; ++v)
    if (s == kVoiceNames[v]) return v;
  return -1;
}

juce::String u8(const char* s) { return juce::String::fromUTF8(s); }

juce::Font font(float z, bool bold, bool mono = false, float ls = 0.0f) {
#if JUCE_MAC
  const char* sans = "Helvetica Neue";
#else
  const char* sans = "DejaVu Sans";
#endif
  juce::Font f(juce::FontOptions().withName(mono ? "DejaVu Sans Mono" : sans).withPointHeight(z).withStyle(
      bold ? "Bold" : "Regular"));
  if (!shogun::dsp::exactEq(ls, 0.0f) && z > 0.0f) f.setExtraKerningFactor(ls / z);
  return f;
}

void text(juce::Graphics& g, float x, float y, const juce::String& t, float z, int anchor, juce::Colour c, bool bold,
          float ls = 0.4f, bool mono = false) {
  if (t.isEmpty()) return;
  g.setColour(c);
  const juce::Font f = font(z, bold, mono, ls);
  juce::GlyphArrangement ga;
  ga.addLineOfText(f, t, 0.0f, 0.0f);
  const float w = ga.getBoundingBox(0, -1, true).getRight();
  const float x0 = anchor == 0 ? x : (anchor == 2 ? x - w : x - 0.5f * w);
  ga.draw(g, juce::AffineTransform::translation(x0, y));
}

juce::ColourGradient radial(juce::Rectangle<float> b, float cx, float cy, float r,
                            std::initializer_list<std::pair<float, std::uint32_t>> stops) {
  const juce::Point<float> c(b.getX() + cx * b.getWidth(), b.getY() + cy * b.getHeight());
  const float rad = r * b.getWidth();
  auto it = stops.begin();
  juce::ColourGradient grad(juce::Colour(it->second), c, juce::Colour((stops.end() - 1)->second),
                            c.translated(rad, 0.0f), true);
  for (const auto& s : stops)
    if (s.first > 0.0f && s.first < 1.0f) grad.addColour(static_cast<double>(s.first), juce::Colour(s.second));
  return grad;
}

// Fill codes 1–7 are the mockup gradients (kb, kc, js, jn, pl, grain, ear).
juce::FillType fillFor(std::uint32_t code, juce::Rectangle<float> b) {
  switch (code) {
    case 1: return radial(b, .4f, .3f, .9f, {{0.f, 0xFF3A3A3A}, {.6f, 0xFF151515}, {1.f, 0xFF080808}});
    case 2: return radial(b, .38f, .3f, .9f, {{0.f, 0xFFD9D8D2}, {.5f, 0xFF8D8C86}, {1.f, 0xFF3B3A37}});
    case 3: return radial(b, .4f, .3f, .9f, {{0.f, 0xFFE2E2DC}, {.55f, 0xFF8F8F8A}, {1.f, 0xFF3A3A38}});
    case 4: return radial(b, .4f, .35f, .9f, {{0.f, 0xFF5A5A58}, {1.f, 0xFF161616}});
    case 5: return juce::ColourGradient(juce::Colour(0xFF1B1D1B), b.getX(), b.getY(), juce::Colour(0xFF0F110F),
                                        b.getX(), b.getBottom(), false);
    case 6: return juce::FillType(juce::Colour(0xFF141614));
    case 7: {
      juce::ColourGradient grad(juce::Colour(0xFF2C2E2C), b.getX(), b.getY(), juce::Colour(0xFF262826), b.getRight(),
                                b.getY(), false);
      grad.addColour(.5, juce::Colour(0xFF4A4D4A));
      return grad;
    }
    default: return juce::FillType(juce::Colour(code));
  }
}

void fillShape(juce::Graphics& g, std::uint32_t fill, float opacity, juce::Rectangle<float> b,
               const std::function<void()>& draw) {
  if (fill == 0) return;
  juce::FillType f = fillFor(fill, b);
  f.setOpacity(f.getOpacity() * opacity);
  g.setFillType(f);
  draw();
}

void circle(juce::Graphics& g, float cx, float cy, float r, std::uint32_t fill, std::uint32_t stroke = 0,
            float sw = 1.0f, float opacity = 1.0f) {
  const juce::Rectangle<float> b(cx - r, cy - r, 2 * r, 2 * r);
  fillShape(g, fill, opacity, b, [&] { g.fillEllipse(b); });
  if (stroke) {
    g.setColour(juce::Colour(stroke).withMultipliedAlpha(opacity));
    g.drawEllipse(b, sw);
  }
}

void rect(juce::Graphics& g, float x, float y, float w, float h, float rx, std::uint32_t fill, std::uint32_t stroke = 0,
          float sw = 1.0f, float opacity = 1.0f) {
  const juce::Rectangle<float> b(x, y, w, h);
  fillShape(g, fill, opacity, b, [&] {
    if (rx > 0) g.fillRoundedRectangle(b, rx);
    else g.fillRect(b);
  });
  if (stroke) {
    g.setColour(juce::Colour(stroke).withMultipliedAlpha(opacity));
    if (rx > 0) g.drawRoundedRectangle(b, rx, sw);
    else g.drawRect(b, sw);
  }
}

void line(juce::Graphics& g, float x1, float y1, float x2, float y2, juce::Colour c, float sw, bool round = false) {
  g.setColour(c);
  juce::Path p;
  p.startNewSubPath(x1, y1);
  p.lineTo(x2, y2);
  g.strokePath(p, juce::PathStrokeType(sw, juce::PathStrokeType::mitered,
                                       round ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
}

// make_mockups.py primitives, drawn with live values.
void drawKnob(juce::Graphics& g, float cx, float cy, float r, const juce::String& label, float v, float lz, int ticks,
              int steps, float ring, bool dim) {
  const int n = steps ? steps : ticks;
  for (int i = 0; i < n && n > 1; ++i) {
    const float a = juce::degreesToRadians(-135.0f + 270.0f * static_cast<float>(i) / static_cast<float>(n - 1) - 90.0f);
    const float l = (i == 0 || i == n - 1 || i == (n - 1) / 2) ? 3.2f : 2.0f;
    line(g, cx + (r + 2.5f) * std::cos(a), cy + (r + 2.5f) * std::sin(a), cx + (r + 2.5f + l) * std::cos(a),
         cy + (r + 2.5f + l) * std::sin(a), juce::Colour(0xFFBDBCB2), 0.9f);
  }
  if (!shogun::dsp::exactEq(ring, 0.0f)) {  // modulation ring: green arc from the knob value over the summed depth
    const float a0 = -135.0f + 270.0f * v, a1 = juce::jlimit(-135.0f, 135.0f, a0 + 270.0f * ring);
    juce::Path p;
    p.addCentredArc(cx, cy, r + 1.2f, r + 1.2f, 0.0f, juce::degreesToRadians(a0), juce::degreesToRadians(a1), true);
    g.setColour(GRN);
    g.strokePath(p, juce::PathStrokeType(2.4f));
  }
  circle(g, cx, cy + 1.5f, r, 0xFF000000, 0, 1, .55f);
  circle(g, cx, cy, r, 1, 0xFF050505, 1.0f);
  circle(g, cx, cy, r * 0.68f, 2, 0xFF2B2B2B, 0.6f);
  const float a = juce::degreesToRadians(-135.0f + 270.0f * v - 90.0f);
  line(g, cx + r * 0.15f * std::cos(a), cy + r * 0.15f * std::sin(a), cx + r * 0.92f * std::cos(a),
       cy + r * 0.92f * std::sin(a), juce::Colour(0xFFF5F4EE), 2.0f, true);
  if (dim) circle(g, cx, cy, r + 1, 0xA0121412);
  if (label.isNotEmpty()) text(g, cx, cy + r + 11, label, lz, 1, dim ? DIM : INK, true);
}

void drawLed(juce::Graphics& g, float cx, float cy, float r, bool on, std::uint32_t c) {
  circle(g, cx, cy, r + 1.6f, 0xFF050505, 0xFF2A2C2A, 0.8f);
  const std::uint32_t off = (c & 0xFFFFFF) == 0xE0402E ? 0xFF3A1612u : ((c & 0xFFFFFF) == 0x4AA862 ? 0xFF173A20u : 0xFF3A2A10u);
  circle(g, cx, cy, r, on ? c : off);
  if (on) circle(g, cx, cy, r * 2.6f, c, 0, 1, .18f);
}

void drawKey(juce::Graphics& g, float x, float y, float w, float h, const juce::String& label, std::uint32_t lit,
             float z, std::uint32_t fill) {
  rect(g, x, y, w, h, 3, fill ? fill : 0xFF0A0A0A, 0xFF2F322F, 1.0f);
  rect(g, x + 1.5f, y + 1.5f, w - 3, h * 0.42f, 2, 0xFFFFFFFF, 0, 1, .05f);
  if (lit) rect(g, x + 2, y + 2, w - 4, h - 4, 2, lit, 0, 1, .85f);
  if (label.isNotEmpty()) text(g, x + w / 2, y + h / 2 + z * 0.36f, label, z, 1, lit ? juce::Colour(0xFF0B0C0B) : INK, true);
}

void drawLcd(juce::Graphics& g, float x, float y, float w, float h, const juce::String& t, float z, int anchor,
             std::uint32_t c) {
  rect(g, x, y, w, h, 2, 0xFF071008, 0xFF2C3A2C, 1.0f);
  g.saveState();
  g.reduceClipRegion(juce::Rectangle<float>(x + 1, y + 1, w - 2, h - 2).getSmallestIntegerContainer());
  text(g, anchor == 0 ? x + 6 : x + w / 2, y + h / 2 + z * 0.36f, t, z, anchor == 0 ? 0 : 1, juce::Colour(c), false,
       0.0f, true);
  g.restoreState();
}

void drawToggle(juce::Graphics& g, float cx, float cy, const juce::String& l, const juce::String& r, bool onRight,
                float z) {
  rect(g, cx - 9, cy - 4.5f, 18, 9, 4.5f, 0xFF050505, 0xFF3A3D3A, 1.0f);
  circle(g, cx + (onRight ? 5.0f : -5.0f), cy, 3.6f, 2);
  if (l.isNotEmpty()) text(g, cx - 12, cy + 2.8f, l, z, 2, INK, true);
  if (r.isNotEmpty()) text(g, cx + 12, cy + 2.8f, r, z, 0, INK, true);
}

void drawRtext(juce::Graphics& g, float x, float y, const juce::String& t, float z) {
  const float w = static_cast<float>(t.length()) * z * 0.62f + 6.0f;
  rect(g, x - w / 2, y - z + 0.5f, w, z + 3, 1.5f, 0xFFF2F1EA);
  text(g, x, y, t, z, 1, juce::Colour(0xFF0B0C0B), true);
}

void drawJack(juce::Graphics& g, float cx, float cy, const juce::String& label, bool out, const juce::String& normal,
              float z, juce::Colour role, bool patched) {
  circle(g, cx, cy, 10.5f, 0xFF000000, 0, 1, .5f);
  circle(g, cx, cy, 9.5f, 3, 0xFF2A2A2C, 0.9f);
  // JCS R14: the jack ring carries the role colour on the ROUTE bay.
  g.setColour(role);
  g.drawEllipse(cx - 9.5f, cy - 9.5f, 19.0f, 19.0f, patched ? 2.2f : 1.4f);
  circle(g, cx, cy, 6.4f, 4);
  circle(g, cx, cy, 3.8f, 0xFF030303);
  if (out) drawRtext(g, cx, cy + 21, label, z);
  else text(g, cx, cy + 21, label, z, 1, INK, true);
  if (normal.isNotEmpty()) text(g, cx, cy - 13, u8("\xE2\x96\xB8") + normal, 6.5f, 1, DIM, false);
}

void drawBox(juce::Graphics& g, float x, float y, float w, float h, const juce::String& title, float tz) {
  rect(g, x, y, w, h, 2, 0, 0xFF4AA862, 1.1f);
  if (title.isNotEmpty()) {
    rect(g, x + 6, y - 6, static_cast<float>(title.length()) * tz * 0.66f + 10.0f, 12, 0, 0xFF121412);
    text(g, x + 11, y + 3.5f, title, tz, 0, INK, true, 1.2f);
  }
}


juce::String paramDisplay(int id, float u) {
  char buf[48];
  formatParam(id, static_cast<double>(u), buf, sizeof buf);  // the engine's display law (same text as host automation)
  return u8(buf);
}



// MAIN-tab bindings.
enum B {
    B_NONE, B_PARAM, B_DISP, B_SEL, B_STEP, B_PLAYLED, B_STEPNUM, B_SK, B_TAB, B_TABTEXT, B_RUN, B_RST, B_RUNLED,
    B_POS, B_KIT, B_PATTERN, B_CPU, B_CPULED, B_CLIPLED, B_METER, B_TITLE, B_TRACKINFO, B_TRACKSCALE, B_TRK, B_PAGE,
    B_COPY, B_PASTE, B_CLEAR, B_RANDOM, B_OFF, B_STATIC,
    B_SOURCE, B_PROG_PREV, B_PROG_NEXT, B_PROG_LIST, B_AB, B_UNDO, B_REDO
};

// Controls whose click or drag changes the device state: bracketed as one undo step (the SHOGUN plugin's editsState).
bool editsState (int kind)
{
    switch (kind)
    {
        case B_PARAM: case B_DISP: case B_STEP: case B_SK: case B_SOURCE: case B_PASTE: case B_CLEAR: case B_RANDOM:
            return true;
        default: return false;
    }
}

} // namespace

int ShogunFace::opCount() { return kOpCount; }

ShogunFace::ShogunFace (jidai::ShogunDevice& d) : device_ (d), ui_ (d.edits())
{
    setOpaque (true);
    buildBindings();
    startTimerHz (30);
}

ShogunFace::~ShogunFace() { stopTimer(); }

void ShogunFace::buildBindings()
{
    struct Pre { const char* p; int k; };
    static const Pre pres[] = {
        { "p", B_PARAM }, { "disp", B_DISP }, { "sel", B_SEL }, { "step", B_STEP }, { "playled", B_PLAYLED },
        { "stepnum", B_STEPNUM }, { "sk", B_SK }, { "tab", B_TAB }, { "tabtext", B_TABTEXT }, { "run", B_RUN },
        { "rst", B_RST }, { "runled", B_RUNLED }, { "pos", B_POS }, { "kit", B_KIT }, { "pattern", B_PATTERN },
        { "cpu", B_CPU }, { "cpuled", B_CPULED }, { "clipled", B_CLIPLED }, { "meter", B_METER }, { "title", B_TITLE },
        { "trackinfo", B_TRACKINFO }, { "trackscale", B_TRACKSCALE }, { "trk", B_TRK }, { "page", B_PAGE },
        { "copy", B_COPY }, { "paste", B_PASTE }, { "clear", B_CLEAR }, { "random", B_RANDOM }, { "off", B_OFF },
        { "prog", B_PROG_NEXT }, { "browse", B_PROG_LIST }, { "src", B_SOURCE }, { "ab", B_AB }, { "undo", B_UNDO },
        { "redo", B_REDO },
    };
    for (int i = 0; i < kOpCount; ++i)
    {
        const LayoutOp& o = kOps[i];
        if (o.tab != 0)
            continue;
        const juce::String bind = u8 (o.bind);
        // Only the MAIN page is in the rack: the other page tabs are not drawn (the pages are in the SHOGUN plugin),
        // nor are the "off" placeholder knobs.
        if (bind == "off" || ((bind.startsWith ("tab:") || bind.startsWith ("tabtext:")) && bind.fromFirstOccurrenceOf (":", false, false).getIntValue() != 0))
            continue;
        if (bind.isEmpty())
        {
            staticOps_.push_back (i);
            continue;
        }
        const juce::String pre = bind.upToFirstOccurrenceOf (":", false, false);
        const juce::String rest = bind.fromFirstOccurrenceOf (":", false, false);
        Bound b { i, B_STATIC, -1, -1 };
        for (const auto& p : pres)
            if (pre == p.p)
                b.kind = p.k;
        switch (b.kind)
        {
            case B_PARAM:
            case B_DISP: b.a = findParam (rest.toRawUTF8()); break;
            case B_SEL: b.a = voiceIndex (rest); break;
            case B_SK:
            {
                static const char* const f[] = { "acc", "flam", "ratchet", "prob", "micro", "bend", "note", "tie" };
                for (int k = 0; k < 8; ++k)
                    if (rest == f[k])
                        b.a = k;
                break;
            }
            case B_TITLE: b.a = rest == "sel" ? 0 : 1; break;
            case B_SOURCE: b.a = findParam ("CLOCK:SOURCE"); break;
            case B_PROG_NEXT: b.kind = rest.getIntValue() < 0 ? B_PROG_PREV : B_PROG_NEXT; break;   // prog:-1 / prog:1
            default: b.a = rest.getIntValue(); break;
        }
        if (b.kind == B_PARAM && b.a < 0)
            b.kind = B_STATIC;
        bounds_.push_back (b);
    }
}

int ShogunFace::paramKnobCount() const
{
    int n = 0;
    for (const auto& b : bounds_)
        n += b.kind == B_PARAM && b.a >= 0 ? 1 : 0;
    return n;
}

juce::AffineTransform ShogunFace::toPanel() const
{
    const float k = (float) getWidth() / kW;
    return juce::AffineTransform::scale (k).translated (0.0f, ((float) getHeight() - kH * k) * 0.5f);
}

Step& ShogunFace::selStep() { return ui_.pattern.tracks[selVoice_].steps[selStep_]; }

void ShogunFace::commit()
{
    device_.setEdits (ui_);
    repaint();
}

void ShogunFace::timerCallback()
{
    meterL_ = std::fmax (device_.meter (0), meterL_ * 0.82f);
    meterR_ = std::fmax (device_.meter (1), meterR_ * 0.82f);
    repaint();
}

void ShogunFace::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF0A0B0A));
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int w = juce::roundToInt ((float) getWidth() * scale), h = juce::roundToInt ((float) getHeight() * scale);
    if (! cache_.isValid() || cacheW_ != w || cache_.getHeight() != h)
    {
        cache_ = juce::Image (juce::Image::RGB, juce::jmax (1, w), juce::jmax (1, h), true);
        juce::Graphics cg (cache_);
        cg.fillAll (juce::Colour (0xFF0A0B0A));
        cg.addTransform (toPanel().scaled (scale));
        for (int i : staticOps_)
            paintOp (cg, i, nullptr);
        cacheW_ = w;
    }
    g.drawImage (cache_, getLocalBounds().toFloat());
    g.saveState();
    g.addTransform (toPanel());
    for (const auto& b : bounds_)
        paintOp (g, b.op, &b);
    g.restoreState();
}

void ShogunFace::paintOp (juce::Graphics& g, int opIndex, const Bound* b)
{
    const LayoutOp& o = kOps[opIndex];
    juce::String t = u8 (o.text);
    const juce::String t2 = u8 (o.text2);
    const int anchor = o.flags & kAnchor;
    const bool bold = (o.flags & kBold) != 0;
    float v = o.v;
    float ring = 0.0f;
    bool on = (o.flags & kOn) != 0;
    std::uint32_t fill = o.fill, stroke = o.stroke;
    bool dim = false;
    float x = o.x, y = o.y, w = o.w, h = o.h;
    const int k = b ? b->kind : B_NONE;
    const Pattern& pat = ui_.pattern;
    const int curStep = device_.step() - 1;
    const bool running = device_.running();

    switch (k)
    {
        case B_PARAM:
        {
            const int pid = b->a;
            const float u = (float) device_.param (pid);
            const ParamInfo& pi = kParams[pid];
            v = u;
            if (o.kind == KNOB && pi.kind == ParamKind::Stepped && pi.steps > 1)
                v = (float) stepIndex (u, pi.steps) / (float) (pi.steps - 1);
            on = stepIndex (u, pi.steps > 0 ? pi.steps : 2) > 0;
            if (o.kind == KEY)
            {
                fill = on ? (t == "M" || t.containsIgnoreCase ("MUTE") ? RED.getARGB() : (fill ? fill : AMB.getARGB())) : 0;
                stroke = o.stroke;
            }
            if (o.kind == KNOB)
                for (const auto& row : ui_.rows)
                    if (row.on && row.src != mod::SRC_NONE && row.dst == pid)
                        ring += (float) row.depth;
            if (o.kind == LCD)
                t = paramDisplay (pid, u);
            break;
        }
        case B_DISP:
            if (b->a >= 0)
            {
                const juce::String d = paramDisplay (b->a, (float) device_.param (b->a));
                t = t.contains (": ") ? t.upToFirstOccurrenceOf (": ", true, false) + d : d;
            }
            break;
        case B_SEL: fill = b->a == selVoice_ ? GRN.getARGB() : 0; break;
        case B_STEP:
        {
            const int s = page_ * 16 + b->a;
            const Step& st = pat.tracks[selVoice_].steps[s];
            fill = st.on ? (st.acc >= 3 ? AMB.getARGB() : RED.getARGB()) : 0;
            if (running && s == curStep % juce::jmax (1, pat.tracks[selVoice_].len))
                fill = INK.getARGB();
            if (s == selStep_ && ! st.on && fill == 0)
                fill = 0xFF3A3D3A;
            dim = s >= pat.tracks[selVoice_].len;
            break;
        }
        case B_STEPNUM: t = juce::String (page_ * 16 + b->a + 1); break;
        case B_PLAYLED: on = running && (page_ * 16 + b->a) == curStep % juce::jmax (1, pat.tracks[selVoice_].len); break;
        case B_SK:
        {
            const Step& st = ui_.pattern.tracks[selVoice_].steps[selStep_];
            switch (b->a)
            {
                case 0: v = (st.acc - 1) / 2.0f; break;
                case 1: v = st.flam / 16.0f; break;
                case 2:
                {
                    int ri = 0;
                    for (int q = 0; q < 6; ++q)
                        if (kRatchets[q] == st.ratchet)
                            ri = q;
                    v = (float) ri / 5.0f;
                    break;
                }
                case 3: v = st.prob; break;
                case 4: v = st.micro + 0.5f; break;
                case 5: v = (st.bend + 12.0f) / 24.0f; break;
                case 6: v = (st.note - 24) / 72.0f; break;
                case 7: fill = st.tie ? AMB.getARGB() : 0; break;
                default: break;
            }
            dim = skDimmed (b->a);
            break;
        }
        case B_TAB:
            fill = b->a == 0 ? 0xFF4AA862u : 0xFF0B0C0Bu;
            stroke = b->a == 0 ? 0xFF4AA862u : 0xFF343834u;
            dim = b->a != 0;
            break;
        case B_TABTEXT: fill = b->a == 0 ? 0xFF071008u : 0xFF5A5C56u; break;   // the other pages live in the SHOGUN plugin
        case B_RUN: fill = running ? GRN.getARGB() : 0; break;
        case B_RUNLED: on = running; break;
        case B_POS:
        {
            const long gs = device_.globalStep();
            t = juce::String (gs / 16 + 1).paddedLeft ('0', 2) + ":" + juce::String (curStep + 1).paddedLeft ('0', 2)
                + (running ? u8 (" \xE2\x96\xB6") : u8 (" \xE2\x96\xA0"));
            break;
        }
        case B_KIT: t = juce::String::fromUTF8 (jidai::shogunstate::programName (device_.program()).c_str()); break;
        case B_PATTERN: t = u8 (pat.name); break;
        case B_CPU: t = "LAT " + juce::String (device_.latencySamples()); break;
        case B_CPULED: fill = GRN.getARGB(); on = true; break;
        case B_CLIPLED: on = meterL_ >= 1.0f || meterR_ >= 1.0f; break;
        case B_METER:
        {
            const float pk = b->a == 0 ? meterL_ : meterR_;
            rect (g, x, y, w, h, o.r, fill, stroke, o.sw);
            const float db = pk > 1e-6f ? 20.0f * std::log10 (pk) : -120.0f;
            const int lit = juce::jlimit (0, 34, juce::roundToInt ((db + 48.0f) / 48.0f * 34.0f));
            for (int i = 0; i < lit; ++i)
                rect (g, 959.5f + (float) i * 5.0f, y + 1.2f, 3.6f, 4.6f, 0, i < 24 ? METERGRN.getARGB() : (i < 30 ? AMB.getARGB() : RED.getARGB()));
            return;
        }
        case B_TITLE: t = t.replace ("%s", b->a == 0 ? juce::String (kVoiceNames[selVoice_]) : juce::String (selStep_ + 1)); break;
        case B_TRACKINFO:
            t = juce::String (kVoiceNames[selVoice_]) + u8 (" \xC2\xB7 ") + juce::String (pat.tracks[selVoice_].len) + " STEPS";
            break;
        case B_TRACKSCALE:
        {
            const Track& tr = pat.tracks[selVoice_];
            static const char* const s[] = { "1/32", "1/16", "1/8T", "1/8" };
            t = "LEN " + juce::String (tr.len) + u8 (" \xC2\xB7 ") + (tr.scale < 0 ? juce::String ("GLOBAL") : juce::String (s[tr.scale & 3]));
            break;
        }
        case B_PAGE: fill = b->a == page_ ? GRN.getARGB() : 0; break;
        case B_SOURCE:
            t = "SRC " + paramDisplay (b->a, (float) device_.param (b->a));
            fill = 0;
            break;
        case B_AB:
            fill = device_.history().abSlot == b->a ? AMB.getARGB() : 0;
            dim = ! jidai::shogunstate::abFilled (device_, b->a);
            break;
        case B_UNDO: dim = device_.history().undo.empty(); break;
        case B_REDO: dim = device_.history().redo.empty(); break;
        case B_OFF: dim = true; break;
        default: break;
    }

    switch (o.kind)
    {
        case TEXT: text (g, x, y, t, o.z, anchor, juce::Colour (fill), bold, v); break;
        case RTEXT: drawRtext (g, x, y, t, o.z); break;
        case RULE: line (g, x, y, w, h, juce::Colour (stroke), o.sw); break;
        case BOX: drawBox (g, x, y, w, h, t, o.z); break;
        case KNOB: drawKnob (g, x, y, o.r, t, juce::jlimit (0.0f, 1.0f, v), o.z, o.ticks, o.steps, ring, dim); break;
        case LED: drawLed (g, x, y, o.r, on, fill); break;
        case KEY:
            drawKey (g, x, y, w, h, t, fill, o.z, stroke);
            if (dim) rect (g, x, y, w, h, 3, 0x90121412);
            break;
        case LCD: drawLcd (g, x, y, w, h, t, o.z, anchor, fill); break;
        case TOGGLE: drawToggle (g, x, y, t, t2, b ? on : (o.flags & kOn) != 0, o.z); break;
        case JACK: drawJack (g, x, y, t, (o.flags & kOut) != 0, t2, o.z, DIM, false); break;
        case RECT:
            rect (g, x, y, w, h, o.r, fill, stroke, o.sw, o.opacity);
            if (dim && k == B_TAB) rect (g, x, y, w, h, o.r, 0x90121412);
            break;
        case CIRCLE: circle (g, x, y, o.r, fill, stroke, o.sw, o.opacity); break;
        case LINE: line (g, x, y, w, h, juce::Colour (stroke).withMultipliedAlpha (o.opacity), o.sw, (o.flags & kRound) != 0); break;
        case PATH:
        {
            juce::Path p = juce::Drawable::parseSVGPath (t);
            if (fill > 7 || (fill >= 1 && fill <= 7))
            {
                juce::FillType f = fillFor (fill, p.getBounds());
                f.setOpacity (o.opacity);
                g.setFillType (f);
                g.fillPath (p);
            }
            if (stroke)
            {
                g.setColour (juce::Colour (stroke).withMultipliedAlpha (o.opacity));
                g.strokePath (p, juce::PathStrokeType (o.sw, juce::PathStrokeType::curved,
                                                       (o.flags & kRound) ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
            }
            break;
        }
        default: break;
    }
}

int ShogunFace::inertControlCount() const
{
    auto control = [] (int kind) { return kind == KNOB || kind == KEY || kind == TOGGLE; };
    int n = 0;
    for (int i : staticOps_)
        n += control (kOps[i].kind) ? 1 : 0;
    for (const auto& b : bounds_)
        if (control (kOps[b.op].kind))
            n += (b.kind == B_STATIC || b.kind == B_OFF || b.kind == B_NONE || (b.kind == B_PARAM && b.a < 0)) ? 1 : 0;
    return n;
}

static juce::Rectangle<int> opRect (const LayoutOp& o, const juce::AffineTransform& t)
{
    return juce::Rectangle<float> (o.x, o.y, o.w, o.h).transformedBy (t).getSmallestIntegerContainer();
}

juce::Rectangle<int> ShogunFace::sourceKeyBounds() const
{
    for (const auto& b : bounds_)
        if (b.kind == B_SOURCE)
            return opRect (kOps[b.op], toPanel());
    return {};
}

juce::Rectangle<int> ShogunFace::keyBounds (const juce::String& bind) const
{
    for (const auto& b : bounds_)
        if (u8 (kOps[b.op].bind) == bind)
            return opRect (kOps[b.op], toPanel());
    return {};
}

juce::Rectangle<int> ShogunFace::programArrowBounds (int index) const
{
    int seen = 0;
    for (const auto& b : bounds_)
        if (b.kind == B_PROG_PREV || b.kind == B_PROG_NEXT)
            if (seen++ == index)
                return opRect (kOps[b.op], toPanel());
    return {};
}

bool ShogunFace::skDimmed (int field) const
{
    // NOTE and TIE are synth-only; BEND applies to the pitched drums (BD1, BD2, SD and the toms).
    if (field == 6 || field == 7)
        return isDrum (selVoice_);
    if (field == 5)
        return ! (selVoice_ <= SD || (selVoice_ >= LTC && selVoice_ <= HTC));
    return false;
}

// The KIT and PATTERN displays open SHOGUN's factory list: INIT, then the kits (each loads its own pattern).
void ShogunFace::showProgramMenu()
{
    juce::PopupMenu m;
    for (int p = 0; p < jidai::shogunstate::programCount(); ++p)
        m.addItem (p + 1, juce::String::fromUTF8 (jidai::shogunstate::programName (p).c_str()), true, p == device_.program());
    juce::Component::SafePointer<ShogunFace> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r) {
        if (safe != nullptr && r > 0)
            safe->loadProgram (r - 1);
    });
}

bool ShogunFace::loadProgram (int program)
{
    jidai::shogunstate::beginUndoStep (device_);     // a program load is one undo step, as in the SHOGUN plugin
    const bool ok = jidai::shogunstate::loadProgram (device_, program);
    jidai::shogunstate::settleUndoStep (device_);
    if (! ok)
        return false;
    ui_ = device_.edits();
    repaint();
    return true;
}

int ShogunFace::findBound (juce::Point<float> p) const
{
    for (auto it = bounds_.rbegin(); it != bounds_.rend(); ++it)
    {
        const LayoutOp& o = kOps[it->op];
        bool hit = false;
        switch (o.kind)
        {
            case KNOB: hit = p.getDistanceFrom ({ o.x, o.y }) <= o.r + 4; break;
            case TOGGLE: hit = std::fabs (p.x - o.x) <= 26 && std::fabs (p.y - o.y) <= 7; break;
            case KEY:
            case LCD:
            case RECT: hit = juce::Rectangle<float> (o.x, o.y, o.w, o.h).contains (p); break;
            default: break;
        }
        if (! hit)
            continue;
        if (it->kind == B_SK && skDimmed (it->a))
            continue;   // dimmed for this voice: not an input
        switch (it->kind)
        {
            case B_PARAM: case B_SEL: case B_STEP: case B_SK: case B_RUN: case B_RST: case B_TRK: case B_PAGE:
            case B_COPY: case B_PASTE: case B_CLEAR: case B_RANDOM: case B_DISP: case B_KIT: case B_PATTERN:
            case B_SOURCE: case B_PROG_PREV: case B_PROG_NEXT: case B_PROG_LIST: case B_AB: case B_UNDO: case B_REDO:
                return (int) std::distance (bounds_.begin(), it.base()) - 1;
            default: break;
        }
    }
    return -1;
}

void ShogunFace::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position.transformedBy (toPanel().inverted());
    dragBound_ = findBound (p);
    dragStart_ = e.position;
    if (dragBound_ < 0)
        return;
    const Bound& b = bounds_[(size_t) dragBound_];
    const LayoutOp& o = kOps[b.op];
    Pattern& pat = ui_.pattern;
    if (editsState (b.kind))
        jidai::shogunstate::beginUndoStep (device_);     // settled at mouse-up
    switch (b.kind)
    {
        case B_PARAM:
        case B_DISP:
        {
            if (b.a < 0)
                return;
            const ParamInfo& pi = kParams[b.a];
            if (o.kind == KNOB)
                dragStartU_ = (float) device_.param (b.a);
            else
            {
                // keys, toggles, LCDs: step to the next choice (toggles flip); right-click steps back
                const int n = pi.steps > 0 ? pi.steps : 2;
                const int i = (stepIndex (device_.param (b.a), n) + (e.mods.isRightButtonDown() ? n - 1 : 1)) % n;
                device_.setParam (b.a, pi.kind == ParamKind::Toggle ? (double) i : stepU (i, n));
                dragBound_ = -1;
            }
            return;
        }
        case B_SEL:
            selVoice_ = b.a;
            device_.trigger (b.a);     // MAIN select keys also audition
            break;
        case B_STEP:
        {
            const int s = page_ * 16 + b.a;
            Step& st = pat.tracks[selVoice_].steps[s];
            selStep_ = s;
            if (e.mods.isRightButtonDown())
                break;
            if (e.mods.isShiftDown())
            {
                st.on = true;
                st.acc = st.acc >= 3 ? 2 : 3;
            }
            else
                st.on = ! st.on;
            commit();
            break;
        }
        case B_KIT:
        case B_PATTERN:
        case B_PROG_LIST: showProgramMenu(); return;
        case B_PROG_PREV:
        case B_PROG_NEXT:
        {
            const int n = jidai::shogunstate::programCount();
            loadProgram ((device_.program() + (b.kind == B_PROG_PREV ? n - 1 : 1)) % n);
            return;
        }
        case B_SOURCE:
        {
            const int i = (stepIndex (device_.param (b.a), 3) + (e.mods.isRightButtonDown() ? 2 : 1)) % 3;
            device_.setParam (b.a, stepU (i, 3));
            break;
        }
        case B_AB:
            if (e.mods.isRightButtonDown() || e.mods.isShiftDown())
                jidai::shogunstate::copyAB (device_, 1 - b.a, b.a);      // right-click B = copy A onto B
            else
                jidai::shogunstate::selectAB (device_, b.a);
            ui_ = device_.edits();
            break;
        case B_UNDO:
        case B_REDO:
            if (b.kind == B_UNDO ? jidai::shogunstate::undo (device_) : jidai::shogunstate::redo (device_))
                ui_ = device_.edits();
            break;
        case B_RUN: device_.requestRun (! device_.running()); break;
        case B_RST: device_.requestRestart(); break;
        case B_TRK: selVoice_ = (selVoice_ + (b.a < 0 ? kVoices - 1 : 1)) % kVoices; break;
        case B_PAGE: page_ = juce::jlimit (0, 1, b.a); break;
        case B_COPY:
            for (int s = 0; s < kMaxSteps; ++s)
                clipboard_[(size_t) s] = pat.tracks[selVoice_].steps[s];
            clipLen_ = pat.tracks[selVoice_].len;
            break;
        case B_PASTE:
            if (clipLen_ > 0)
            {
                for (int s = 0; s < kMaxSteps; ++s)
                    pat.tracks[selVoice_].steps[s] = clipboard_[(size_t) s];
                pat.tracks[selVoice_].len = clipLen_;
                commit();
            }
            break;
        case B_CLEAR:
            for (auto& st : pat.tracks[selVoice_].steps)
                st = Step();
            commit();
            break;
        case B_RANDOM:
        {
            juce::Random r;
            for (int s = 0; s < pat.tracks[selVoice_].len; ++s)
                pat.tracks[selVoice_].steps[s].on = r.nextFloat() < 0.3f;
            commit();
            break;
        }
        case B_SK:
            if (b.a == 7)
            {
                selStep().tie = ! selStep().tie;
                commit();
            }
            break;
        default: break;
    }
    repaint();
}

void ShogunFace::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBound_ < 0)
        return;
    const Bound& b = bounds_[(size_t) dragBound_];
    const LayoutOp& o = kOps[b.op];
    const float u = juce::jlimit (0.0f, 1.0f, dragStartU_ + (dragStart_.y - e.position.y) / (e.mods.isShiftDown() ? 800.0f : 200.0f));
    if (b.kind == B_PARAM && b.a >= 0 && o.kind == KNOB)
        device_.setParam (b.a, u);
    else if (b.kind == B_SK && e.mouseWasDraggedSinceMouseDown())
    {
        Step& st = selStep();
        switch (b.a)
        {
            case 0: st.acc = (std::uint8_t) (1 + juce::roundToInt (2.0f * u)); break;
            case 1: st.flam = (std::uint8_t) juce::roundToInt (16.0f * u); break;
            case 2: st.ratchet = (std::uint8_t) kRatchets[juce::roundToInt (5.0f * u)]; break;
            case 3: st.prob = u; break;
            case 4: st.micro = u - 0.5f; break;
            case 5: st.bend = (float) (juce::roundToInt (24.0f * u) - 12); break;
            case 6: st.note = (std::int8_t) (24 + juce::roundToInt (72.0f * u)); break;
            default: break;
        }
        device_.setEdits (ui_);
    }
    repaint();
}

void ShogunFace::mouseUp (const juce::MouseEvent&)
{
    dragBound_ = -1;
    jidai::shogunstate::settleUndoStep (device_);
    repaint();
}

void ShogunFace::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int bi = findBound (e.position.transformedBy (toPanel().inverted()));
    if (bi < 0)
        return;
    const Bound& b = bounds_[(size_t) bi];
    if (b.kind == B_PARAM && b.a >= 0 && kOps[b.op].kind == KNOB)
    {
        jidai::shogunstate::beginUndoStep (device_);
        device_.setParam (b.a, kParams[b.a].def);     // double-click = default (noon kit value)
        jidai::shogunstate::settleUndoStep (device_);
    }
}

void ShogunFace::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int bi = findBound (e.position.transformedBy (toPanel().inverted()));
    if (bi < 0)
        return;
    const Bound& b = bounds_[(size_t) bi];
    if (b.kind == B_PARAM && b.a >= 0 && kOps[b.op].kind == KNOB)
    {
        jidai::shogunstate::beginUndoStep (device_);
        device_.setParam (b.a, device_.param (b.a) + w.deltaY * 0.05f);
        jidai::shogunstate::settleUndoStep (device_);
    }
}
