# uutils' SELinux Rust bindings require clang-native from meta-clang. Runtime
# provider discovery otherwise emits NoProvider errors even when GNU coreutils
# is selected. Leave this optional alternative available when it is buildable.
python () {
    if ('selinux' in (d.getVar('PACKAGECONFIG') or '').split()
            and 'clang-native' in (d.getVar('DEPENDS') or '').split()
            and 'clang-layer' not in (d.getVar('BBFILE_COLLECTIONS') or '').split()):
        raise bb.parse.SkipRecipe('uutils-coreutils SELinux bindings require clang-native from meta-clang; GNU coreutils remains available')
}
