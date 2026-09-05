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

// Every entry below replaces a constant that used to be hardcoded in
// lucid_dock.cpp. Ranges are what the dock can actually render sensibly, and
// they are enforced on load so a hand-edited file cannot produce a dock that
// is invisible or fills the screen.
const Schema& default_schema() {
    static const Schema* s = [] {
        auto* out = new Schema();
        out->set_version(1);
        auto num = [&](const char* k, double def, double lo, double hi, const char* doc) {
            out->add({k, Type::Double, def, lo, hi, doc, 1, {}});
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

        // Desktop-wide rather than per-surface. A font is not the dock's or the
        // panel's opinion, it is the desktop's, and a key every surface reads is
        // the strongest form of the claim this schema makes: one value, one
        // place to change it, every component follows.
        out->add({"desktop.font-family", Type::String, std::string("Inter"),
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
        out->add({"desktop.wallpaper-colour", Type::String, std::string("#2f6fb0"),
                  {}, {}, "Colour shown where the wallpaper cannot be loaded", 1, {}});
        out->add({"desktop.wallpaper-mode", Type::String, std::string("fill"),
                  {}, {}, "How the wallpaper is fitted: stretch, fit, fill, center, tile", 1, {}});

        // Empty means "whatever the desktop is set to", which is the right
        // default for a dock installed on KDE or sway where LucidOS's icons may
        // not exist. LucidOS's own distro layer sets it to Lucid, so the session
        // is themed without the standalone dock imposing a theme nobody asked
        // for -- which is exactly the layering this resolver is for.
        out->add({"desktop.icon-theme", Type::String, std::string(""),
                  {}, {}, "Icon theme; empty follows the desktop's own setting", 1, {}});
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

std::string default_distro_dir() { return "/usr/share/lucid/profile.d"; }

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
