# MNC System SDK

`mnc::system` is the product-independent C++23 contract. `mnc::os::system`
implements the systemd client/backend. `mnc::logging` and `mnc::Service` provide
shared journal and daemon lifecycle support. Product code depends on interfaces;
only the native daemon performs privileged OS operations.

CMake consumers use `find_package(MncSystem CONFIG REQUIRED)` and link
`mnc::system`, `mnc::os-system`, `mnc::logging` or `mnc::service`. Public headers
are installed under `mnc/`. The optional `mnc/system/json.hpp` adapter requires
the consumer's Glaze dependency. The internal D-Bus protocol is not installed.
Host builds without libsystemd return typed unavailable errors from the client;
production builds must enable `MNC_SYSTEM_REQUIRE_SYSTEMD`.

Configure with `MNC_GLAZE_SOURCE_DIR`, `BUILD_TESTING=ON` and optionally
`MNC_SYSTEM_BUILD_DAEMON=ON` (libsystemd and OpenSSL required). Run CTest. The
backend test substitutes all native effects and uses temporary private state;
it never reconfigures the host or performs a power action.

The service owns `com.monutchee.MNCOS.System` on the local system bus, object
`/com/monutchee/MNCOS/System`, interface `com.monutchee.MNCOS.System1`.
Explicit methods carry bounded JSON typed by the SDK. Root can mutate; the
profile settings UID can change preferences/network; the control UID can
request power/reset. Other callers can query. Unknown operations are rejected.
There is no shell, arbitrary path, or arbitrary unit execution interface.

A root-owned, non-writable regular `/usr/share/mnc/system/profile.json` supplies
settings/control account names, the authoritative settings path, private state
directory, managed interfaces/hardware identifiers, sensors, factory defaults,
SSH/clock units and reset-path allowlists. Product packaging enables the units
and provides mount/startup ordering. The generic recipe leaves them disabled.

The native adapter uses timedated, hostnamed, systemd and logind D-Bus methods.
Networkd configuration-file generation is isolated in the daemon; runtime
reload/reconfigure and discovery use native D-Bus. Its description parser follows
the [systemd 255 network JSON contract](https://github.com/systemd/systemd/blob/v255/src/network/networkd-json.c)
and [manager methods](https://github.com/systemd/systemd/blob/v255/src/network/networkd-manager-bus.c).
Only DHCP or one static IPv4 configuration per allowlisted link is represented.
Products must avoid earlier competing matching network files.

Network proposals journal old/new configuration and settings hashes before OS
application, expire after 120 seconds and require preparation before the sole
settings authority commits its file. Recovery recognizes that durable hash and
otherwise restores the old network. Reset intent survives every deletion until
bootstrap finishes; product policy decides the exact deletion scope. Journal
write uncertainty stops mutations and causes daemon recovery.

New system code is GPL-3.0-only. Extracted logging/service code retains Apache-2.0
(`LICENSE.runtime`); the build recipe records Glaze's upstream MIT license.
