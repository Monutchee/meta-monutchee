# glaze recipe

Provides upstream 8.0.0 headers and installed CMake exports (`glaze::glaze`).
FTXUI supplies position-independent static archives; Glaze is header-only.
Consumers add `glaze` to `DEPENDS` and use `find_package(glaze CONFIG REQUIRED)`.
No CMake dependency downloads, examples, tests, docs or modules are enabled.

Update `GLAZE_TAG` and `SRCREV` together in build configuration or the recipe.
The immutable revision controls fetching; unpack rejects a mismatched tag.
Keep the recipe version and upstream license checksum aligned when upgrading.

Static/header consumers add `glaze-licenses` to runtime dependencies to retain
`/usr/share/licenses/glaze/LICENSE` without shipping development files.
The development package retains this notice dependency; FTXUI development
metadata also requires its static archives.

Validate with `bitbake glaze` in the consuming product's configured environment,
then configure and link a consumer against the staged CMake exports.
