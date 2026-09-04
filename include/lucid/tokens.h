// LucidOS configuration model.
//
// One idea carries the whole product: a resolved setting remembers WHERE it
// came from. Reset-this-key, reset-everything, and show-me-what-I-changed are
// then three queries against one mechanism rather than three features. Systems
// that store only the final value cannot implement any of them, which is why
// no desktop offers them today.
//
// Two invariants, both load-bearing:
//   1. Resolution NEVER fails. Bad input is clamped or defaulted and reported
//      as a diagnostic. A malformed config file must not cost you a session.
//   2. The user layer is always separable. Deleting it returns the system to
//      exactly what it shipped as, touching nothing else.

#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lucid {

enum class Type { Bool, Int, Double, String, Color };

// Lowest wins ties; highest layer present provides the value.
enum class Layer : int {
    Default = 0,  // compiled in, always valid, cannot fail to load
    Theme   = 1,  // /usr/share/lucid/themes/<id>/tokens.ini
    Distro  = 2,  // /usr/share/lucid/profile.d/*.ini
    User    = 3,  // ~/.config/lucid/profile.d/*.ini      <- always separable
    Session = 4,  // runtime only, never written to disk
};

const char* layer_name(Layer l);

using Value = std::variant<bool, std::int64_t, double, std::string>;

std::string to_string(const Value& v);
Type type_of(const Value& v);
const char* type_name(Type t);

// A key's contract. Range is advisory for the GUI and enforced on load: a value
// outside it is clamped, never rejected, because rejecting is how a config file
// takes down a session.
struct KeyDef {
    std::string key;              // "dock.icon-size"
    Type type = Type::Double;
    Value default_value;
    std::optional<double> min;
    std::optional<double> max;
    std::string summary;
    int since = 1;                // schema version that introduced it
    std::string replaced_by;      // set when deprecated; migration target
};

// What happened during load that the user may need to know about. Never fatal.
struct Diagnostic {
    std::string key;
    std::string file;
    std::string problem;
    std::string action;           // what was done instead
};

struct Resolved {
    Value value;
    Layer layer = Layer::Default;
    std::string source_file;      // empty for Default
};

class Schema {
  public:
    void add(KeyDef def);
    const KeyDef* find(const std::string& key) const;
    const std::vector<KeyDef>& keys() const { return keys_; }
    int version() const { return version_; }
    void set_version(int v) { version_ = v; }

  private:
    std::vector<KeyDef> keys_;
    int version_ = 1;
};

// The shipped schema. Every key here is one that used to be a hardcoded
// constant in the dock.
const Schema& default_schema();

class Config {
  public:
    explicit Config(const Schema& schema);

    // Load layers 1..3 from disk. Missing files are not errors -- a system with
    // no config at all is a valid system that renders defaults.
    void load(const std::string& user_dir,
              const std::string& distro_dir = {},
              const std::string& theme_dir = {});

    // Never fails. An unknown key returns a defaulted Resolved and records a
    // diagnostic rather than throwing, so a caller can never crash on a typo.
    Resolved resolve(const std::string& key) const;

    bool   get_bool  (const std::string& key) const;
    std::int64_t get_int(const std::string& key) const;
    double get_double(const std::string& key) const;
    std::string get_string(const std::string& key) const;

    // Session overrides: highest layer, memory only.
    void set_session(const std::string& key, Value v);
    void clear_session(const std::string& key);

    // Every key whose value comes from the user layer or above -- the
    // "show me everything I have changed" query.
    std::vector<std::pair<std::string, Resolved>> changed() const;

    // Write/remove in the user layer. Returns false only on I/O failure.
    bool set_user(const std::string& key, const Value& v, const std::string& user_dir);
    bool reset_user(const std::string& key, const std::string& user_dir);

    const std::vector<Diagnostic>& diagnostics() const { return diags_; }
    const Schema& schema() const { return *schema_; }

  private:
    struct Entry { Value value; Layer layer; std::string file; };

    void load_dir(const std::string& dir, Layer layer);
    void load_file(const std::string& path, Layer layer);
    void put(const std::string& key, Value v, Layer layer, const std::string& file);

    const Schema* schema_;
    std::vector<std::pair<std::string, Entry>> entries_;
    mutable std::vector<Diagnostic> diags_;
};

// Default search paths.
std::string default_user_dir();
std::string default_distro_dir();

}  // namespace lucid
