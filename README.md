# lucid-tokens

The LucidOS configuration model: a layered settings resolver where **every
value remembers where it came from**.

That one property is what the product is built on. *Reset this setting*,
*reset everything*, and *show me everything I've changed* are not three
features — they are three queries against one mechanism. A settings system
that stores only the final value cannot implement any of them, which is why
no desktop offers them today, and why "customisation that can't break your
system" has stayed a slogan rather than a design.

## Layers

```
  session   runtime only, never written to disk
  user      ~/.config/lucid/profile.d/*.ini      <- always separable
  distro    /usr/share/lucid/profile.d/*.ini
  theme     /usr/share/lucid/themes/<id>/tokens.ini
  default   compiled in, always valid, cannot fail to load
```

The highest layer that sets a key wins. Every layer's value is retained, so
removing one falls back to the layer beneath rather than all the way to the
default. Within a layer, files load in filename order — hence the `NN-` prefix
convention.

## Two invariants

**Resolution never fails.** A value of the wrong type falls back to the
default; a value out of range is clamped; an unknown key is ignored and
carried, so a config written by a newer LucidOS cannot break an older one.
Every one of these is reported as a diagnostic. A malformed config file must
never cost you a session, so it is not permitted to be an error.

**The user layer is always separable.** It is one file. Deleting it returns
the system to exactly what it shipped as, touching nothing else.

## Try it

```
make && make check
./lucid-tokens list                    # every key, value, and layer
./lucid-tokens set dock.icon-size 72
./lucid-tokens diff                    # everything you have changed
./lucid-tokens get dock.icon-size      # value, provenance, default, docs
./lucid-tokens reset-all
```

Hand-edit `~/.config/lucid/profile.d/90-user.ini` into nonsense and run
`./lucid-tokens doctor`. Every setting still resolves to something usable.

## Testing

The resolver is a pure function from a layer stack to a value plus provenance,
which is precisely why the safety claim is testable. `make check` runs the
layering, reset, separability and validation cases, then fuzzes the loader with
400 random files and asserts that every key still resolves within its declared
range. The combinatorial space lives in the resolver, so testing the resolver
*is* testing the safety claim — there is no need to test the rendered result
across every combination of settings.

## Schema

Every key currently in the schema replaces a constant that was hardcoded in
`lucid-dock`. That is the intended pattern: a value stops being a magic number
in one program and becomes a setting the whole desktop can see, document,
validate, and revert.

## Status

Working, tested, and in use. `lucid-dock` reads its geometry, motion and
stylesheet values from here; its settings page shows each key's layer and offers
a per-key reset, both of which are one lookup against the resolver rather than a
feature each. The token files are watched, so a `lucid-tokens set` applies to the
running dock without a restart.

Consumed by [lucid-dock](https://github.com/LucidOS-Project/lucid-dock), and
paired with `lucid-session`, which covers the half of the safety claim a resolver
cannot: a configuration can be entirely valid and still produce a desktop you
cannot use.
