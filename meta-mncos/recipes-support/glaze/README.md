# glaze recipe

Provides upstream 8.0.0 headers and installed CMake exports (`glaze::glaze`).
Glaze is header-only.
Consumers add `glaze` to `DEPENDS` and use `find_package(glaze CONFIG REQUIRED)`.
No CMake dependency downloads, examples, tests, docs or modules are enabled.

Update `GLAZE_TAG` and `SRCREV` together in the recipe. Build configuration
overrides must be recipe-scoped, for example `GLAZE_TAG:pn-glaze` and
`SRCREV:pn-glaze`.
The immutable revision controls fetching; unpack rejects a mismatched tag.
Keep the recipe version and upstream license checksum aligned when upgrading.

Static/header consumers add `glaze-licenses` to runtime dependencies to retain
`/usr/share/licenses/glaze/LICENSE` without shipping development files.
The development package retains this notice dependency.

Validate with `bitbake glaze` in the consuming product's configured environment,
then configure and link a consumer against the staged CMake exports.
