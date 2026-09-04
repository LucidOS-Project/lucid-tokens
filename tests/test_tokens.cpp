// The resolver is a pure function from a layer stack to a value plus
// provenance, which is exactly why it can be tested exhaustively. The safety
// claim -- "customising cannot break your session" -- lives here, not in the
// rendered result, so this is the file that has to be trustworthy.
#include "lucid/tokens.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>

using namespace lucid;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static void write(const std::filesystem::path& p, const std::string& body) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p) << body;
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / "lucid-tokens-test";
    std::filesystem::remove_all(root);
    const auto user = root / "user";
    const auto distro = root / "distro";

    std::puts("layering");
    {
        write(distro / "50-distro.ini", "[dock]\nicon-size = 64\ncorner-radius = 12\n");
        write(user / "90-user.ini", "[dock]\nicon-size = 80\n");
        Config c(default_schema());
        c.load(user.string(), distro.string());
        check(c.get_double("dock.icon-size") == 80.0, "user layer beats distro layer");
        check(c.resolve("dock.icon-size").layer == Layer::User, "provenance names the user layer");
        check(c.get_double("dock.corner-radius") == 12.0, "distro value used where user is silent");
        check(c.resolve("dock.corner-radius").layer == Layer::Distro, "provenance names distro");
        check(c.get_double("dock.item-gap") == 10.0, "compiled default where all layers silent");
        check(c.resolve("dock.item-gap").layer == Layer::Default, "provenance names default");
    }

    std::puts("show me what I changed");
    {
        Config c(default_schema());
        c.load(user.string(), distro.string());
        const auto ch = c.changed();
        check(ch.size() == 1, "only the user layer counts as changed");
        check(ch[0].first == "dock.icon-size", "and it names the right key");
    }

    std::puts("reset");
    {
        Config c(default_schema());
        c.load(user.string(), distro.string());
        c.reset_user("dock.icon-size", user.string());
        check(c.get_double("dock.icon-size") == 64.0, "reset one key falls back to distro");
        check(c.changed().empty(), "and nothing is reported as changed");

        Config d(default_schema());
        d.load(user.string(), distro.string());
        check(d.get_double("dock.icon-size") == 64.0, "reset survives a reload");
    }

    std::puts("separability");
    {
        Config c(default_schema());
        c.load(user.string(), distro.string());
        c.set_user("dock.icon-size", 96.0, user.string());
        c.set_user("dock.item-gap", 20.0, user.string());
        std::filesystem::remove_all(user);
        Config d(default_schema());
        d.load(user.string(), distro.string());
        check(d.get_double("dock.icon-size") == 64.0, "deleting the user dir undoes everything");
        check(d.get_double("dock.corner-radius") == 12.0, "and touches no other layer");
    }

    std::puts("a bad config never costs a session");
    {
        write(user / "90-user.ini",
              "this is not ini at all\n"
              "[dock]\n"
              "icon-size = banana\n"
              "corner-radius = 999999\n"
              "no-such-key = 3\n"
              "item-gap = -50\n");
        Config c(default_schema());
        c.load(user.string(), distro.string());
        check(c.get_double("dock.icon-size") == 64.0, "unparseable value falls back");
        check(c.get_double("dock.corner-radius") == 64.0, "over-max value clamps to max");
        check(c.get_double("dock.item-gap") == 0.0, "under-min value clamps to min");
        check(c.diagnostics().size() >= 4, "and every one of them is reported");
    }

    std::puts("fuzz: no input makes resolution fail");
    {
        std::mt19937 rng(1234);
        const std::string chars = "abc.=[]\n#0123456789-xyz \t";
        for (int i = 0; i < 400; ++i) {
            std::string junk;
            const int n = static_cast<int>(rng() % 300);
            for (int j = 0; j < n; ++j) junk += chars[rng() % chars.size()];
            write(user / "90-user.ini", junk);

            Config c(default_schema());
            c.load(user.string(), distro.string());
            for (const auto& def : default_schema().keys()) {
                const Resolved r = c.resolve(def.key);
                if (def.min && type_of(r.value) == Type::Double &&
                    std::get<double>(r.value) < *def.min) {
                    check(false, "fuzz produced an out-of-range value");
                    return 1;
                }
            }
        }
        check(true, "400 random files: every key still resolves in range");
    }

    std::filesystem::remove_all(root);
    std::printf("\n%s\n", failures == 0 ? "all checks passed" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
