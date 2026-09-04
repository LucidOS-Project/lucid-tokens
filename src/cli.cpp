// lucid-tokens: the configuration model as a command line, so the behaviour
// can be exercised and tested long before a settings GUI exists.
#include "lucid/tokens.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace lucid;

namespace {

int usage() {
    std::puts(
        "usage: lucid-tokens <command>\n"
        "\n"
        "  list                 every key, its value, and which layer set it\n"
        "  get <key>            one value, with provenance\n"
        "  set <key> <value>    write to the user layer\n"
        "  reset <key>          remove from the user layer\n"
        "  reset-all            remove the entire user layer\n"
        "  diff                 everything you have changed\n"
        "  doctor               report problems found while loading\n"
        "\n"
        "Layers, lowest to highest: default, theme, distro, user, session.\n"
        "The user layer lives in one file and deleting it undoes everything.");
    return 2;
}

Config load() {
    Config cfg(default_schema());
    cfg.load(default_user_dir(), default_distro_dir());
    return cfg;
}

// Parse against the schema's declared type so `set` refuses nonsense at the
// point of entry rather than at the point of use.
bool parse_for(const KeyDef& def, const std::string& raw, Value* out) {
    try {
        switch (def.type) {
            case Type::Bool: {
                const bool t = raw == "true" || raw == "yes" || raw == "1" || raw == "on";
                const bool f = raw == "false" || raw == "no" || raw == "0" || raw == "off";
                if (!t && !f) return false;
                *out = t;
                return true;
            }
            case Type::Int:    *out = static_cast<std::int64_t>(std::stoll(raw)); return true;
            case Type::Double: *out = std::stod(raw); return true;
            default:           *out = raw; return true;
        }
    } catch (...) { return false; }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string cmd = argv[1];

    if (cmd == "list") {
        Config cfg = load();
        for (const auto& def : default_schema().keys()) {
            const Resolved r = cfg.resolve(def.key);
            std::printf("%-28s %-10s  [%s]\n", def.key.c_str(),
                        to_string(r.value).c_str(), layer_name(r.layer));
        }
        return 0;
    }

    if (cmd == "get" && argc >= 3) {
        Config cfg = load();
        const Resolved r = cfg.resolve(argv[2]);
        std::printf("%s = %s\n", argv[2], to_string(r.value).c_str());
        std::printf("  from: %s%s%s\n", layer_name(r.layer),
                    r.source_file.empty() ? "" : " -> ", r.source_file.c_str());
        if (const KeyDef* d = default_schema().find(argv[2])) {
            std::printf("  default: %s\n", to_string(d->default_value).c_str());
            if (!d->summary.empty()) std::printf("  %s\n", d->summary.c_str());
        }
        return 0;
    }

    if (cmd == "set" && argc >= 4) {
        const KeyDef* def = default_schema().find(argv[2]);
        if (def == nullptr) {
            std::fprintf(stderr, "no such key: %s\n", argv[2]);
            return 1;
        }
        Value v;
        if (!parse_for(*def, argv[3], &v)) {
            std::fprintf(stderr, "'%s' is not a %s\n", argv[3], type_name(def->type));
            return 1;
        }
        Config cfg = load();
        if (!cfg.set_user(argv[2], v, default_user_dir())) {
            std::fprintf(stderr, "could not write the user layer\n");
            return 1;
        }
        std::printf("%s = %s  [user]\n", argv[2], to_string(v).c_str());
        return 0;
    }

    if (cmd == "reset" && argc >= 3) {
        Config cfg = load();
        if (!cfg.reset_user(argv[2], default_user_dir())) return 1;
        const Resolved r = cfg.resolve(argv[2]);
        std::printf("%s = %s  [%s]\n", argv[2], to_string(r.value).c_str(), layer_name(r.layer));
        return 0;
    }

    if (cmd == "reset-all") {
        Config cfg = load();
        const auto ch = cfg.changed();
        for (const auto& [k, r] : ch) {
            (void)r;
            cfg.reset_user(k, default_user_dir());
        }
        std::printf("reset %zu setting%s to defaults\n", ch.size(), ch.size() == 1 ? "" : "s");
        return 0;
    }

    if (cmd == "diff") {
        Config cfg = load();
        const auto ch = cfg.changed();
        if (ch.empty()) {
            std::puts("Nothing changed from defaults.");
            return 0;
        }
        for (const auto& [key, r] : ch) {
            const KeyDef* d = default_schema().find(key);
            std::printf("%-28s %-10s  (default %s)  [%s]\n", key.c_str(),
                        to_string(r.value).c_str(),
                        d ? to_string(d->default_value).c_str() : "?",
                        layer_name(r.layer));
        }
        return 0;
    }

    if (cmd == "doctor") {
        Config cfg = load();
        const auto& d = cfg.diagnostics();
        if (d.empty()) {
            std::puts("No problems found.");
            return 0;
        }
        for (const auto& x : d) {
            std::printf("%s\n  in:     %s\n  problem: %s\n  action:  %s\n\n",
                        x.key.empty() ? "(file)" : x.key.c_str(),
                        x.file.c_str(), x.problem.c_str(), x.action.c_str());
        }
        return 0;
    }

    return usage();
}
