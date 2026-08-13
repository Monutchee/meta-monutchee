# MNC service identities - the single source of truth.
#
# Every user and group that MNC software runs as, or that owns MNC state, is
# defined here and nowhere else. Add new identities to this file only.
#
# WHY THIS FILE EXISTS
#
# These identities own state on /data, a persistent partition that outlives any
# rootfs, so their NUMERIC ids are part of the on-disk format rather than an
# implementation detail. A bare "--system" lets useradd.bbclass allocate ids at
# rootfs-assembly time by counting down from 999 in package-install order, so
# adding one service user or a single RDEPENDS silently renumbers the existing
# ones and every file already on /data becomes unreadable to its owning daemon.
# That is not hypothetical: it took the whole metering chain down once, because
# the settings authority could no longer read its own active.json.
#
# Two rules follow, and both matter:
#
#   1. Every id here is pinned explicitly. Never recycle or renumber one -
#      only append. A number that has reached a device is permanent.
#   2. Any recipe that must declare these identities itself expands the
#      variables below rather than repeating the numbers, so duplicated
#      declarations cannot drift apart from each other.
#
# 780-789 is the reserved MNC block. It sits well clear of the dynamic band
# systemd's own accounts (systemd-network, -resolve, -timesync) are drawn from,
# so a distro update cannot collide with it.
#
# The names carry the vendor prefix, not a product one. These accounts outlive
# any single product generation - msap1 is only the first, msap2 follows and the
# shipping product is msa - so they share the `mnc` namespace already used by
# /data/mnc, /run/monutchee, the mnc CLI and the mnc:: libraries. The prefix is
# kept rather than dropped because /etc/passwd and /etc/group are one flat
# namespace shared with every other recipe in the image: bare names like "data"
# or "settings" are exactly what another package is liable to claim, and a
# collision either fails the build or silently merges two identities, losing
# the per-service isolation these accounts exist to provide.

MNC_GID_DATA      = "780"
MNC_GID_SETTINGS  = "781"
MNC_UID_SETTINGS  = "781"
MNC_UID_STREAM    = "782"
MNC_UID_HISTORIAN = "783"
MNC_GID_WEB       = "784"
MNC_UID_WEB       = "784"
MNC_UID_MODBUS    = "785"

# Shared shape for every MNC service account: no login, no home directory.
MNC_SYSTEM_ACCOUNT = "--system --home /nonexistent --no-create-home --shell /sbin/nologin"

# mnc-data     - shared access to acquisition IPC objects and /data subtrees
# mnc-settings - the settings authority; also gates mutation over its socket
# mnc-web      - the network-facing web/proxy identity
# mnc-modbus   - the field-protocol identity; reads acquisition/settings IPC
MNC_GROUPADD_SHARED = "--system --gid ${MNC_GID_DATA} mnc-data; \
    --system --gid ${MNC_GID_SETTINGS} mnc-settings; \
    --system --gid ${MNC_GID_WEB} mnc-web"

# mnc-settings owns /data/mnc/settings, so it is the only account whose primary
# group is the authority group; the rest sit in mnc-data and are granted the
# authority group only as a supplementary where a unit needs the socket.
MNC_USERADD_SHARED = "${MNC_SYSTEM_ACCOUNT} --uid ${MNC_UID_SETTINGS} --gid mnc-settings --groups mnc-data mnc-settings; \
    ${MNC_SYSTEM_ACCOUNT} --uid ${MNC_UID_STREAM} --gid mnc-data mnc-stream; \
    ${MNC_SYSTEM_ACCOUNT} --uid ${MNC_UID_HISTORIAN} --gid mnc-data mnc-historian; \
    ${MNC_SYSTEM_ACCOUNT} --uid ${MNC_UID_WEB} --gid mnc-web mnc-web; \
    ${MNC_SYSTEM_ACCOUNT} --uid ${MNC_UID_MODBUS} --gid mnc-data mnc-modbus"
