# Optional SELinux foundation

MNCOS keeps SELinux off by default. A product enables it through
`DISTRO_FEATURES` and adds a compatible `meta-selinux` layer. The shared layer
reports a configuration error if that feature is enabled without the layer;
its refpolicy integration and Xilinx kernel append are discovered dynamically.
Products inherit `mncos-selinux-image`, select targeted refpolicy, set
`DEFAULT_ENFORCING = "permissive"` and `FIRST_BOOT_RELABEL = "0"`, and install
`mnc-selinux`. Rootfs labeling errors fail the image build. Read-only roots must
never depend on a first-boot root relabel.

Before the storage bind mounts, preboot captures the image's read-only
`/var/lib/selinux` under the root-only runtime directory. After storage setup,
it binds that captured store read-only over the persistent `/var/lib/selinux`.
The current image therefore always supplies policy, including after upgrades;
runtime policy edits are unsupported. Symlink components are rejected. The
minimal RAM-root init remains policy-free, and SquashFS explicitly retains
xattrs. Systemd loads policy on the final root, then the early restore unit
runs after tmpfiles and before `sysinit.target`. Bootstrap helpers required by
tmpfiles run before restoration. Products order their application and settings
units after, and require, this restoration unit. This is a permissive
foundation; it does not claim confinement of those bootstrap helpers or apps.

The helper restores writable state, data, runtime and persistent aliases, using
shared file-context substitutions to give backing inodes the same labels as
their logical paths. Products can install `/etc/mnc/selinux-paths.json` with
`writable_paths` and `factory_paths` arrays of canonical absolute paths.
Factory context mismatches are reported at startup without remounting the
read-only partition. Acceptance requires correcting them while explicitly
writable, using `/usr/libexec/mnc-selinux maintenance /factory`. Storage restore
relabels restored factory identity and state files before unmounting, including
an interrupted restore whose content is already present. Backup archives need
not carry SELinux xattrs: restoration applies the current image's policy.
Provisioning tools must run the same explicit labeling step before making the
factory filesystem read-only.

There is no audit daemon in this foundation. Kernel audit records use the
existing journal path and retention policy. Image and recipe builds, effective
kernel configuration, actual rootfs/SquashFS labels, systemd dependency ordering,
factory maintenance and reboot/upgrade behavior still require product and
hardware validation before release. Enforcing mode and service policy remain
separate product milestones.

The optional `uutils-coreutils` alternative in meta-oe needs `clang-native` for
its SELinux Rust bindings. The shared dynamic append skips that recipe when
those bindings select the dependency and the `clang-layer` provider collection
is absent. This avoids dependency-discovery errors for an unused alternative
while retaining GNU coreutils, its SELinux support, and feature-off products.
Adding meta-clang restores eligibility; this guard does not add that layer.
