#include "lucid/tokens.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace lucid {
namespace {

std::string trim(std::string s) {
    const auto ws = " \t\r\n";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

bool parse_bool(const std::string& in, bool* out) {
    std::string s = in;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "true" || s == "yes" || s == "1" || s == "on")  { *out = true;  return true; }
    if (s == "false" || s == "no" || s == "0" || s == "off") { *out = false; return true; }
    return false;
}

// Parse into the type the schema declares. A value that does not fit its
// declared type is a diagnostic, not an exception.
bool coerce(const std::string& raw, Type type, Value* out) {
    try {
        switch (type) {
            case Type::Bool: {
                bool b = false;
                if (!parse_bool(raw, &b)) return false;
                *out = b;
                return true;
            }
            case Type::Int: {
                std::size_t used = 0;
                const long long v = std::stoll(raw, &used);
                if (used != raw.size()) return false;
                *out = static_cast<std::int64_t>(v);
                return true;
            }
            case Type::Double: {
                std::size_t used = 0;
                const double v = std::stod(raw, &used);
                if (used != raw.size()) return false;
                *out = v;
                return true;
            }
            case Type::String:
            case Type::Color:
                *out = raw;
                return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}

double as_number(const Value& v) {
    if (const auto* d = std::get_if<double>(&v)) return *d;
    if (const auto* i = std::get_if<std::int64_t>(&v)) return static_cast<double>(*i);
    return 0.0;
}

}  // namespace

const char* layer_name(Layer l) {
    switch (l) {
        case Layer::Default: return "default";
        case Layer::Theme:   return "theme";
        case Layer::Distro:  return "distro";
        case Layer::User:    return "user";
        case Layer::Session: return "session";
    }
    return "?";
}

const char* type_name(Type t) {
    switch (t) {
        case Type::Bool:   return "bool";
        case Type::Int:    return "int";
        case Type::Double: return "double";
        case Type::String: return "string";
        case Type::Color:  return "color";
    }
    return "?";
}

Type type_of(const Value& v) {
    if (std::holds_alternative<bool>(v))         return Type::Bool;
    if (std::holds_alternative<std::int64_t>(v)) return Type::Int;
    if (std::holds_alternative<double>(v))       return Type::Double;
    return Type::String;
}

std::string to_string(const Value& v) {
    if (const auto* b = std::get_if<bool>(&v)) return *b ? "true" : "false";
    if (const auto* i = std::get_if<std::int64_t>(&v)) return std::to_string(*i);
    if (const auto* d = std::get_if<double>(&v)) {
        std::ostringstream os;
        os << *d;
        return os.str();
    }
    return std::get<std::string>(v);
}

void Schema::add(KeyDef def) { keys_.push_back(std::move(def)); }

const KeyDef* Schema::find(const std::string& key) const {
    for (const auto& k : keys_) {
        if (k.key == key) return &k;
    }
    return nullptr;
}

KeyDef* Schema::find_mutable(const std::string& key) {
    for (auto& k : keys_) {
        if (k.key == key) return &k;
    }
    return nullptr;
}

// Every entry below replaces a constant that used to be hardcoded in
// lucid_dock.cpp. Ranges are what the dock can actually render sensibly, and
// they are enforced on load so a hand-edited file cannot produce a dock that
// is invisible or fills the screen.
const std::vector<std::string>& default_page_order() {
    static const std::vector<std::string>* order = new std::vector<std::string>{
        "Appearance",
        "Wallpaper",
        "Display",
        "Lock Screen",
        "Dock",
        "Panel",
        "Control Centre",
    };
    return *order;
}

const Schema& default_schema() {
    static const Schema* s = [] {
        auto* out = new Schema();
        out->set_version(1);
        // `title` is what a settings window calls the key. Left off, the window
        // derives one from the key name, which is right for almost all of these.
        auto num = [&](const char* k, double def, double lo, double hi, const char* doc,
                       const char* title = "") {
            out->add({k, Type::Double, def, lo, hi, doc, 1, {}, {}, title});
        };
        // These three ranges are not taste, they are what the dock can contain.
        //
        // The dock's surface is a compile-time constant sized for the largest
        // configuration it accepts -- that is what stops it walking down the
        // screen when magnification changes -- so an icon larger than
        // MAX_ICON_SIZE, or a scale above MAX_MAX_SCALE, overflows a surface
        // that cannot grow, and the icons are simply clipped.
        //
        // Because a range here is *enforced* by clamping rather than merely
        // advertised, the range is the thing that keeps a config file from
        // producing a broken dock. A range wider than the consumer's real
        // limit is therefore not a harmless approximation: it is the safety
        // claim failing at the exact point it is supposed to hold. These were
        // 256, 4.0 and 12.0, all of which the dock cannot honour.
        num("dock.icon-size",          57.6,  24.0,  80.0, "Icon size at rest, logical px");
        num("dock.magnify-scale",       2.0,   1.0,   3.0, "Magnified size as a multiple of icon size");
        num("dock.magnify-range",       6.0,   1.5,   8.0, "Influence radius, in icon widths");
        num("dock.item-gap",           10.0,   0.0,  64.0, "Gap between icons, logical px");
        num("dock.padding-x",          10.0,   0.0,  64.0, "Panel horizontal padding");
        num("dock.padding-y",           8.0,   0.0,  64.0, "Panel vertical padding");
        num("dock.bottom-margin",       8.0,   0.0, 256.0, "Gap between dock and screen edge");
        num("dock.corner-radius",      19.0,   0.0,  64.0, "Panel corner radius");
        num("dock.background-opacity",  0.4,   0.0,   1.0, "Panel background alpha");
        // The dock's panel was white with an alpha and no way to say
        // otherwise, so a desktop whose panel is dark had a light dock beside
        // it and no setting to reconcile them. Default white, which is what it
        // has always drawn: a dock dropped onto another desktop looks exactly
        // as it did, and LucidOS says something different in its own layer.
        // Empty means "follow desktop.color-scheme", which is what almost
        // everybody wants: one switch that moves the whole desktop rather than
        // a colour per component that has to be kept in step by hand.
        out->add({"dock.background-colour", Type::String, std::string(""),
                  {}, {}, "Panel background colour, hex. Empty follows desktop.color-scheme",
                  1, {}});
        num("dock.bounce-height",      40.0,   0.0, 200.0, "Launch bounce height, logical px");
        num("dock.bounce-duration",     0.4,   0.0,   3.0, "Launch bounce duration, seconds");
        // The dock's easing is a damped spring, not exponential decay. It was
        // dock.magnify-tau and dock.release-tau, and those keys described a
        // mechanism the dock deleted: two time constants cannot express a
        // spring, and having one of them also meant tracking speed and release
        // speed were the same control, which is the bug that motivated the
        // change. Marked replaced_by so a config carrying the old keys is
        // migrated rather than silently ignored.
        num("dock.spring-omega",       30.0,   1.0, 120.0,
            "Undamped natural frequency, rad/s. Higher reacts quicker; the feel is unchanged");
        num("dock.spring-zeta",         0.783, 0.1,   2.0,
            "Damping ratio. Below 1 the motion arrives by overshooting slightly rather than creeping");
        out->add({"dock.magnify-tau", Type::Double, 0.055, 0.0, 1.0,
                  "Removed: the dock eases with a spring", 1, "dock.spring-omega"});
        out->add({"dock.release-tau", Type::Double, 0.135, 0.0, 2.0,
                  "Removed: the dock eases with a spring", 1, "dock.spring-omega"});
        num("dock.indicator-size",      4.0,   0.0,  24.0, "Running-app dot size, logical px");
        // Icons arrive from whichever theme happens to answer, and they are not
        // drawn to a common grid: an application shipping its own icon under
        // hicolor may fill its canvas edge to edge while a theme's own icons
        // leave a margin, so the two read as different sizes side by side. This
        // is the fraction of the icon box the artwork is scaled to occupy.
        // 0 disables it and leaves every icon exactly as its theme drew it.
        num("dock.icon-ink-ratio",      0.9,   0.0,   1.0,
            "Fraction of the icon box the artwork fills; 0 leaves icons untouched");
        out->add({"dock.enabled", Type::Bool, true, {}, {}, "Show the dock", 1, {}});

        // Off is a real requirement, not a preference. Until this key there was
        // no way to stop the animation at all -- only to slow it down, which is
        // the wrong direction for anyone who asked for less motion. A desktop
        // that cannot turn its animations off has an accessibility bug, and the
        // dock still has to do the minimise either way.
        out->add({"dock.genie-enabled", Type::Bool, true, {}, {},
                  "Animate minimise and restore; off minimises with no animation", 1, {}, {},
                  "Animate minimising"});
        // The minimise animation. Its duration is measured, not chosen -- 451ms
        // -- so the control is a multiplier on it rather than a duration of its
        // own, and bigger is SLOWER because it multiplies a duration and not a
        // rate. The effect this copies has the same setting with the same
        // meaning and the same limits, which is where the range comes from.
        num("dock.genie-speed",         1.0,   0.25,  2.0,
            "Minimise animation duration, as a multiple of its measured 451ms; bigger is slower",
            "Minimise duration");
        // Animating a minimise the dock did not start -- a window's own titlebar
        // button -- is not free. The window's pixels are gone by the time the
        // dock is told, so a picture of the screen has to be kept from
        // beforehand and refreshed as windows move: one screen capture every
        // half second while a window is open. Off, those minimises simply
        // happen, and the capture stops.
        out->add({"dock.genie-foreign-minimise", Type::Bool, true, {}, {},
                  "Animate minimises started from a window's own titlebar; costs a periodic screen capture",
                  1, {}, {}, "Animate the window's own minimise button"});

        // The panel: the second surface, and the reason the schema is a schema
        // rather than a header in the dock. These keys are read by a different
        // process, on a different edge of the screen, from the same files --
        // which is the whole claim the layered resolver makes.
        num("panel.height",             28.0,  20.0,  64.0, "Panel height, logical px");
        num("panel.padding-x",          12.0,   0.0,  64.0, "Panel horizontal padding");
        num("panel.background-opacity",  0.4,   0.0,   1.0, "Panel background alpha");
        out->add({"panel.enabled", Type::Bool, true, {}, {}, "Show the panel", 1, {}});
        num("panel.corner-radius",      14.0,   0.0,  32.0, "Panel corner radius");
        num("panel.margin",              8.0,   0.0,  64.0,
            "Gap between the panel and the screen edges; 0 makes it flush");

        // The panel's popovers -- the notice, and the control centre -- are a
        // SEPARATE alpha from the panel's own, and the reason is the floor
        // rather than the default.
        //
        // panel.background-opacity is 0.4 and that is right for a 28-pixel
        // strip carrying a clock and four glyphs, which also compensates with
        // a text-shadow. A popover is ten times the area and carries 10.5px
        // state lines and a 5px slider track, with no compositor blur behind
        // it to lean on -- labwc has none, and a control nobody can find is
        // worse than an opaque surface. Sharing the panel's key would mean
        // somebody dragging the panel to 0.2 for a translucent desktop gets an
        // unreadable control centre as a side effect they never asked for.
        //
        // The floor was 0.6 and the default 0.68, and both were set before the
        // popover had a blurred capture behind it. At that point the veil was
        // the ONLY thing making text readable over a wallpaper, so a low alpha
        // really would have hidden the controls.
        //
        // With a backdrop the blur does that work -- it removes the
        // high-frequency detail text has to compete with -- and the veil is
        // mostly covering up the thing it was there to substitute for. At 0.68
        // only 32% of the captured picture survives, which is why the surface
        // reads as a flat pane rather than as glass picking up the colours
        // behind it. This is macOS's arrangement and it is the right way round:
        // heavy blur, light tint, not light blur and a heavy tint.
        //
        // 0.35 by eye against the aurora, comparing 0.68, 0.42 and 0.26 side
        // by side: 0.68 is a pane, 0.26 starts costing the dim state lines
        // their contrast, and this sits between them.
        //
        // 0.25 is the floor rather than none, because a popover with no tint
        // at all is a hole in the screen with text floating in it.
        num("panel.popover-opacity",     0.35,  0.25,  1.0,
            "Background alpha for the panel's popovers, including the control centre");

        // Whether a popover follows the desktop's scheme or picks its own.
        //
        // "follow" is the default and it is the coherent answer: a light panel
        // with a dark surface hanging off it reads as two desktops. But the
        // argument at the top of lucid_panel.cpp's install_css -- that a
        // surface carrying text has to be legible against a wallpaper it does
        // not choose -- applies with more force to a 330x440 popover holding
        // twenty labels than it ever did to a 28-pixel strip holding a clock.
        // On a pale wallpaper a light popover has very little to separate it.
        //
        // So it is a key rather than a decision made once in C++: somebody who
        // wants every popover dark on a light desktop can say so, and the
        // default still matches the panel above it.
        out->add({"panel.popover-scheme", Type::String, std::string("follow"),
                  {}, {}, "Popover colour scheme: follow the desktop, or force one",
                  1, {}, {"follow", "light", "dark"}});

        // Desktop-wide rather than per-surface. A font is not the dock's or the
        // panel's opinion, it is the desktop's, and a key every surface reads is
        // the strongest form of the claim this schema makes: one value, one
        // place to change it, every component follows.
        // Manrope, and this also settles a disagreement: lucid_panel.cpp's
        // hardcoded fallback was already Manrope while this said Inter, so
        // whichever was right, one of them was wrong.
        //
        // Inter is the safe answer and reads like it -- it is also the face
        // half the interfaces on the internet already use. Manrope has actual
        // letterforms, runs slightly wider so rows breathe, and keeps a clear
        // weight difference between a row's title and its state line, which is
        // what makes the list scannable.
        //
        // Compared on screen against Inter, Ubuntu and Sora. Ubuntu is
        // legible and unmistakably Ubuntu's, which is somebody else's identity
        // and the same problem the icon set has. Sora is disqualified on a
        // packaging defect rather than on looks: fonts-sora ships a Regular
        // but `fc-match "Sora:weight=regular"` still answers ExtraBold, so
        // every label in the desktop would render heavy. Karla and Cabin were
        // not compared -- the capture harness would not hold still long
        // enough -- and remain worth a look.
        out->add({"desktop.font-family", Type::String, std::string("Manrope"),
                  {}, {}, "Interface font family", 1, {}});
        num("desktop.font-size",        12.0,   6.0,  32.0, "Interface font size, pt");

        // The wallpaper is a path, so it is the one key here with no meaningful
        // range and no way to validate beyond "is it a string". It is a token
        // anyway because everything else about the desktop's appearance is:
        // splitting it out into its own file would mean two places to look for
        // "what does my desktop look like", and the provenance query -- which
        // layer set this, a theme or me -- is exactly as useful here as it is
        // for a corner radius.
        //
        // An unreadable path is not an error. The session falls back to
        // desktop.wallpaper-colour, which is why that exists and why a colour
        // rather than a second path: a colour cannot itself be missing.
        out->add({"desktop.wallpaper", Type::String,
                  std::string("/usr/share/backgrounds/lucid/lucid.png"),
                  {}, {}, "Wallpaper image path", 1, {}});
        // #052936 is the single most common colour in the default wallpaper --
        // 15.7% of lucidos-aurora.jpg, the deep water and sky it is mostly made
        // of. It was #2f6fb0, hue 210, a blue: 50 degrees off a photograph at
        // hue 192, so the colour standing in for the wallpaper was a colour
        // LucidOS does not otherwise contain. Nobody sees this often, which is
        // exactly why it was wrong for so long.
        out->add({"desktop.wallpaper-colour", Type::String, std::string("#052936"),
                  {}, {}, "Colour shown where the wallpaper cannot be loaded", 1, {}});
        // The colour a control uses to say it is ON. Until now there was none,
        // which was not an oversight so much as an absence nobody had needed
        // yet: a dock draws icons, a panel draws a clock, and neither has a
        // state to report. A control centre is the first surface that does,
        // and every filled tile in it needs this.
        //
        // #0d7759 is the aurora itself -- hue 163, sampled from the green
        // band of lucidos-aurora.jpg rather than chosen. Only its lightness
        // was moved, down to the darkest point that still clears 4.5:1
        // against white, because a filled control carries an 11.5px label and
        // that is normal-size text: the vivid version measured 3.22:1 and was
        // never available.
        //
        // Why not a blue. Windows is blue, macOS is blue, GNOME is blue, and
        // the first attempt here was #1c222e's neighbourhood at hue 220 -- a
        // slate 24 degrees off everything else on the desktop. The sky in that
        // photograph is genuinely the larger share of its saturated pixels
        // (50.1% in 200-210 degrees against about 30% across 150-180), so blue
        // was defensible. It was just also the answer everybody else already
        // gave. The aurora is the part of that image anyone would describe
        // first, and it is nobody else's accent.
        //
        // It is a token and not a constant because an accent is the single
        // thing people most want to change, and lucid-settings generates its
        // controls from this schema -- so the colour control exists the moment
        // this line does.
        out->add({"desktop.accent-colour", Type::String, std::string("#0d7759"),
                  {}, {}, "Colour a control uses to show it is on", 1, {}});
        // The one switch. Light by default because that is what LucidOS looks
        // like; the components derive their own palettes from it rather than
        // each carrying a colour that somebody has to remember to change twice.
        // How large everything is drawn.
        //
        // A laptop with a dense screen renders a 28-pixel panel 28 physical
        // pixels tall, which on a 4K 14-inch display is a strip you cannot
        // read and a dock you cannot hit. Every desktop has this problem and
        // the ones that solve it ship a display setting; LucidOS shipped none,
        // so there was no way to change it at all -- not a bad default, an
        // absent control.
        //
        // Bounded at 3: beyond that a scale is a mistake rather than a
        // preference, and clamping is what this resolver does with a number
        // outside its range.
        //
        // NOTHING APPLIES THIS YET, and the reason is worth recording. labwc
        // speaks zwlr_output_manager_v1, which is the protocol that sets an
        // output's scale -- but no client that speaks it is packaged for 26.04:
        // no wlr-randr, no kanshi, no wdisplays. And labwc takes no output
        // configuration of its own; the only scale in its manual belongs to the
        // magnifier. So the key exists here first, because a setting with
        // nowhere to live is worse than one whose applier is still being
        // written, and because lucid-settings generates its controls from this
        // schema -- the day something applies it, the control is already there.
        num("desktop.scale", 1.0, 0.5, 3.0,
            "How large everything is drawn. 1 is normal, 1.25 and 1.5 suit dense screens");

        // When the screen locks itself, and when it goes dark.
        //
        // lucid-lock has shipped since 0.1.0 and the session had nothing to
        // trigger it, so it locked only when somebody asked -- and a lock
        // screen nobody remembers to reach for protects nothing. swayidle is
        // the missing half and these are the two numbers it needs.
        //
        // Both are measured from the last input, not from each other, because
        // that is what swayidle's timeouts mean. Screen-off is therefore the
        // larger of the two by default: the screen blanks a couple of minutes
        // after the lock rather than a couple of minutes after idle began.
        //
        // 0 disables either one. That is a real thing to want -- a machine
        // giving a presentation, a kiosk -- and a setting that cannot be turned
        // off gets turned off by uninstalling the package instead.
        num("desktop.lock-idle-minutes",   10.0,  0.0, 180.0,
            "Minutes of inactivity before the screen locks. 0 never locks");
        num("desktop.screen-off-minutes",  12.0,  0.0, 240.0,
            "Minutes of inactivity before the screen turns off. 0 keeps it on");

        out->add({"desktop.color-scheme", Type::String, std::string("light"),
                  {}, {}, "light or dark. Every LucidOS surface follows it", 1, {},
                  {"light", "dark"}});
        out->add({"desktop.wallpaper-mode", Type::String, std::string("fill"),
                  {}, {}, "How the wallpaper is fitted", 1, {},
                  {"stretch", "fit", "fill", "center", "tile"}});

        // Empty means "whatever the desktop is set to", which is the right
        // default for a dock installed on KDE or sway where LucidOS's icons may
        // not exist. LucidOS's own distro layer sets it to Lucid, so the session
        // is themed without the standalone dock imposing a theme nobody asked
        // for -- which is exactly the layering this resolver is for.
        out->add({"desktop.icon-theme", Type::String, std::string(""),
                  {}, {}, "Icon theme; empty follows the desktop's own setting", 1, {}});
        // ── Where each key appears, and in what order ───────────────────
        //
        // One table rather than three more arguments on forty-one
        // declarations. It is also the only place that has to be read to know
        // what the settings window looks like, which is the point: the layout
        // of a generated window IS this list.
        //
        // Pages are named for what somebody is trying to change, not for the
        // program that reads the key. "Lock Screen" holds two keys the session
        // reads; "Control Centre" holds two the panel reads. Deriving pages
        // from namespaces gave three pages called Dock, Desktop and Panel --
        // the names of the programs.
        struct Placement { const char* key; const char* page; bool advanced; int order; };
        static const Placement kPlacement[] = {
            // Appearance -- what the desktop looks like, in the order somebody
            // changes them: the big switch first, then colour, then type.
            {"desktop.color-scheme",        "Appearance",     false,  10},
            {"desktop.accent-colour",       "Appearance",     false,  20},
            {"desktop.icon-theme",          "Appearance",     false,  30},
            {"desktop.font-family",         "Appearance",     false,  40},
            {"desktop.font-size",           "Appearance",     false,  50},

            {"desktop.wallpaper",           "Wallpaper",      false,  10},
            {"desktop.wallpaper-mode",      "Wallpaper",      false,  20},
            // The fallback colour is real and almost nobody sets it: it is
            // seen only when the image cannot be read.
            {"desktop.wallpaper-colour",    "Wallpaper",      true,   30},

            {"desktop.scale",               "Display",        false,  10},

            {"desktop.lock-idle-minutes",   "Lock Screen",    false,  10},
            {"desktop.screen-off-minutes",  "Lock Screen",    false,  20},

            // Dock. On/off, then size, then the magnification people actually
            // came for, then the parts of the animation worth exposing.
            {"dock.enabled",                "Dock",           false,  10},
            {"dock.icon-size",              "Dock",           false,  20},
            {"dock.magnify-scale",          "Dock",           false,  30},
            {"dock.magnify-range",          "Dock",           false,  40},
            {"dock.item-gap",               "Dock",           false,  50},
            {"dock.bottom-margin",          "Dock",           false,  60},
            {"dock.corner-radius",          "Dock",           false,  70},
            {"dock.background-opacity",     "Dock",           false,  80},
            {"dock.background-colour",      "Dock",           false,  90},
            {"dock.genie-enabled",          "Dock",           false, 100},
            {"dock.genie-speed",            "Dock",           false, 110},
            {"dock.bounce-height",          "Dock",           false, 120},
            {"dock.bounce-duration",        "Dock",           false, 130},
            // Behind the disclosure: geometry nobody tunes by eye, and the
            // spring constants, which are real settings with real effects and
            // are not what anybody opens a settings window to find. A list
            // whose fourth row is "Spring omega" teaches somebody that this
            // window is not for them.
            {"dock.padding-x",              "Dock",           true,  200},
            {"dock.padding-y",              "Dock",           true,  210},
            {"dock.indicator-size",         "Dock",           true,  220},
            {"dock.icon-ink-ratio",         "Dock",           true,  230},
            {"dock.spring-omega",           "Dock",           true,  240},
            {"dock.spring-zeta",            "Dock",           true,  250},
            {"dock.magnify-tau",            "Dock",           true,  260},
            {"dock.release-tau",            "Dock",           true,  270},
            {"dock.genie-foreign-minimise", "Dock",           true,  280},

            {"panel.enabled",               "Panel",          false,  10},
            {"panel.height",                "Panel",          false,  20},
            {"panel.margin",                "Panel",          false,  30},
            {"panel.corner-radius",         "Panel",          false,  40},
            {"panel.background-opacity",    "Panel",          false,  50},
            {"panel.padding-x",             "Panel",          true,   60},

            // The control centre is the panel's, and nobody looks for it
            // under "Panel" -- they look for the thing they opened.
            {"panel.popover-opacity",       "Control Centre", false,  10},
            {"panel.popover-scheme",        "Control Centre", false,  20},
        };
        for (const Placement& p : kPlacement) {
            if (KeyDef* k = out->find_mutable(p.key)) {
                k->category = p.page;
                k->advanced = p.advanced;
                k->order = p.order;
            }
        }

        return out;
    }();
    return *s;
}

Config::Config(const Schema& schema) : schema_(&schema) {}

std::string default_user_dir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        return std::string(xdg) + "/lucid/profile.d";
    }
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/lucid/profile.d";
}

std::string default_distro_dir() {
    // LUCID_DISTRO_DIR, for the same reason XDG_CONFIG_HOME already moves the
    // user layer: a layered resolver whose layers cannot be pointed anywhere is
    // a layered resolver that can only be tested as root. The distro layer is
    // the one that decides what a LucidOS session looks like as opposed to a
    // bare dock, and it was the only layer with no way to exercise it.
    if (const char* dir = std::getenv("LUCID_DISTRO_DIR")) {
        if (*dir != '\0') {
            return dir;
        }
    }
    return "/usr/share/lucid/profile.d";
}

// Every layer's value for a key is kept, not just the winning one. Keeping
// only the winner made reset fall through to the compiled default instead of
// to the layer underneath -- so "undo my change" would have silently discarded
// the distro's value too. Layers are only separable if they are all present.
void Config::put(const std::string& key, Value v, Layer layer, const std::string& file) {
    for (auto& e : entries_) {
        // Later files within the same layer overwrite: that is what the NN-
        // numeric filename prefix is for.
        if (e.first == key && e.second.layer == layer) {
            e.second = Entry{std::move(v), layer, file};
            return;
        }
    }
    entries_.push_back({key, Entry{std::move(v), layer, file}});
}

void Config::own_namespace(const std::string& prefix) {
    if (!prefix.empty()) {
        owned_.push_back(prefix);
    }
}

// Owning nothing means owning the question: report every unknown key, which is
// what a doctor wants. Owning something means reporting only what is yours.
bool Config::reports_unknown(const std::string& key) const {
    if (owned_.empty()) {
        return true;
    }
    for (const std::string& prefix : owned_) {
        // Prefix plus a dot, so owning "dock" does not silently also own
        // "dockyard.something".
        if (key.size() > prefix.size() + 1 && key.compare(0, prefix.size(), prefix) == 0 &&
            key[prefix.size()] == '.') {
            return true;
        }
    }
    return false;
}

void Config::load_file(const std::string& path, Layer layer) {
    std::ifstream in(path);
    if (!in) return;

    std::string line, section;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            diags_.push_back({{}, path, "line " + std::to_string(lineno) + " is not key = value",
                              "line ignored"});
            continue;
        }

        const std::string name = trim(line.substr(0, eq));
        const std::string raw  = trim(line.substr(eq + 1));
        const std::string key  = section.empty() ? name : section + "." + name;

        const KeyDef* def = schema_->find(key);
        if (def == nullptr) {
            // Forward compatibility: a config written by a NEWER LucidOS must
            // not break an older one. Unknown keys are carried, not fatal.
            //
            // Whether this consumer says so is a separate question from whether
            // it survives it -- see own_namespace().
            if (reports_unknown(key)) {
                diags_.push_back({key, path, "unknown key", "ignored, file left unchanged"});
            }
            continue;
        }

        Value v;
        if (!coerce(raw, def->type, &v)) {
            diags_.push_back({key, path, "value '" + raw + "' is not a " + type_name(def->type),
                              "using " + to_string(def->default_value)});
            continue;
        }

        // Clamp rather than reject. An out-of-range value should give you an
        // odd-looking dock you can fix in the UI, never a session you cannot
        // reach the UI from.
        if (def->min || def->max) {
            double n = as_number(v);
            const double before = n;
            if (def->min && n < *def->min) n = *def->min;
            if (def->max && n > *def->max) n = *def->max;
            if (n != before) {
                diags_.push_back({key, path, "value " + to_string(v) + " out of range",
                                  "clamped to " + std::to_string(n)});
                v = (def->type == Type::Int) ? Value{static_cast<std::int64_t>(n)} : Value{n};
            }
        }

        // The same idea for a closed set: a value that is not one of them is
        // replaced by the default, not refused. "colour-scheme = drak" should
        // give you a light desktop and a diagnostic, not a session that will
        // not start.
        if (!def->choices.empty() && def->type == Type::String) {
            const std::string got = std::get<std::string>(v);
            if (std::find(def->choices.begin(), def->choices.end(), got) ==
                def->choices.end()) {
                std::string legal;
                for (const auto& ch : def->choices) {
                    if (!legal.empty()) legal += ", ";
                    legal += ch;
                }
                diags_.push_back({key, path, "'" + got + "' is not one of: " + legal,
                                  "using " + to_string(def->default_value)});
                continue;
            }
        }

        put(key, std::move(v), layer, path);
    }
}

void Config::load_dir(const std::string& dir, Layer layer) {
    if (dir.empty()) return;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;

    std::vector<std::string> files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".ini") {
            files.push_back(e.path().string());
        }
    }
    std::sort(files.begin(), files.end());  // NN- prefix decides order
    for (const auto& f : files) load_file(f, layer);
}

void Config::load(const std::string& user_dir, const std::string& distro_dir,
                  const std::string& theme_dir) {
    entries_.clear();
    diags_.clear();
    load_dir(theme_dir,  Layer::Theme);
    load_dir(distro_dir, Layer::Distro);
    load_dir(user_dir,   Layer::User);
}

Resolved Config::resolve(const std::string& key) const {
    const KeyDef* def = schema_->find(key);
    if (def == nullptr) {
        // Callers must never crash on a typo, so this is a diagnostic and a
        // usable value rather than an exception.
        diags_.push_back({key, {}, "no such key in schema", "returned false/0"});
        return Resolved{Value{std::int64_t{0}}, Layer::Default, {}};
    }
    const Entry* best = nullptr;
    for (const auto& e : entries_) {
        if (e.first != key) continue;
        if (best == nullptr || static_cast<int>(e.second.layer) > static_cast<int>(best->layer)) {
            best = &e.second;
        }
    }
    if (best != nullptr) return Resolved{best->value, best->layer, best->file};
    return Resolved{def->default_value, Layer::Default, {}};
}

bool Config::get_bool(const std::string& key) const {
    const Value v = resolve(key).value;
    if (const auto* b = std::get_if<bool>(&v)) return *b;
    return as_number(v) != 0.0;
}
std::int64_t Config::get_int(const std::string& key) const {
    return static_cast<std::int64_t>(as_number(resolve(key).value));
}
double Config::get_double(const std::string& key) const {
    return as_number(resolve(key).value);
}
std::string Config::get_string(const std::string& key) const {
    return to_string(resolve(key).value);
}

void Config::set_session(const std::string& key, Value v) {
    put(key, std::move(v), Layer::Session, "<session>");
}

void Config::clear_session(const std::string& key) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->first == key && it->second.layer == Layer::Session) {
            entries_.erase(it);
            return;
        }
    }
}

// Only keys whose WINNING value comes from the user layer or above. A user
// entry that some higher layer overrides is not something the user sees, so
// reporting it as "changed" would be a lie.
std::vector<std::pair<std::string, Resolved>> Config::changed() const {
    std::vector<std::pair<std::string, Resolved>> out;
    std::vector<std::string> seen;
    for (const auto& e : entries_) {
        if (std::find(seen.begin(), seen.end(), e.first) != seen.end()) continue;
        seen.push_back(e.first);
        const Resolved r = resolve(e.first);
        if (static_cast<int>(r.layer) >= static_cast<int>(Layer::User)) {
            out.push_back({e.first, r});
        }
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
}

namespace {
// The user layer is one file we own entirely, so rewriting it is safe and
// keeps "delete this file to undo everything" true.
std::string user_file(const std::string& dir) { return dir + "/90-user.ini"; }

bool write_user_file(const std::string& dir,
                     const std::vector<std::pair<std::string, Value>>& kv) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const std::string final_path = user_file(dir);
    const std::string tmp_path = final_path + ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::trunc);
        if (!out) return false;
        out << "# Written by LucidOS. Delete this file to undo every change.\n";
        std::string section;
        for (const auto& [key, v] : kv) {
            const auto dot = key.find('.');
            const std::string sec = dot == std::string::npos ? "" : key.substr(0, dot);
            const std::string name = dot == std::string::npos ? key : key.substr(dot + 1);
            if (sec != section) {
                section = sec;
                out << "\n[" << section << "]\n";
            }
            out << name << " = " << to_string(v) << "\n";
        }
    }
    // Atomic swap: a crash mid-write leaves the previous file intact.
    std::filesystem::rename(tmp_path, final_path, ec);
    return !ec;
}
}  // namespace

bool Config::set_user(const std::string& key, const Value& v, const std::string& user_dir) {
    std::vector<std::pair<std::string, Value>> kv;
    for (const auto& [k, r] : changed()) {
        if (k != key) kv.push_back({k, r.value});
    }
    kv.push_back({key, v});
    std::sort(kv.begin(), kv.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    if (!write_user_file(user_dir, kv)) return false;
    put(key, v, Layer::User, user_file(user_dir));
    return true;
}

bool Config::reset_user(const std::string& key, const std::string& user_dir) {
    std::vector<std::pair<std::string, Value>> kv;
    for (const auto& [k, r] : changed()) {
        if (k != key) kv.push_back({k, r.value});
    }
    if (!write_user_file(user_dir, kv)) return false;
    // Remove only the user layer's entry, leaving lower layers intact so the
    // value falls back to whatever the distro or theme set.
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->first == key && it->second.layer == Layer::User) {
            entries_.erase(it);
            break;
        }
    }
    return true;
}

}  // namespace lucid
