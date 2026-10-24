# ftxui recipe

Provides upstream 7.0.3 headers and installed CMake exports (`ftxui::ftxui`).
FTXUI supplies position-independent static archives; Glaze is header-only.
Consumers add `ftxui` to `DEPENDS` and use `find_package(ftxui CONFIG REQUIRED)`.
No CMake dependency downloads, examples, tests, docs or modules are enabled.

Update `FTXUI_TAG` and `SRCREV` together in build configuration or the recipe.
The immutable revision controls fetching; unpack rejects a mismatched tag.
Keep the recipe version and upstream license checksum aligned when upgrading.

Static/header consumers add `ftxui-licenses` to runtime dependencies to retain
`/usr/share/licenses/ftxui/LICENSE` without shipping development files.
The development package retains this notice dependency; FTXUI development
metadata also requires its static archives.

Validate with `bitbake ftxui` in the consuming product's configured environment,
then configure and link a consumer against the staged CMake exports.
