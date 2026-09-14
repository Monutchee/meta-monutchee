# Xilinx SYSMON provider

`mnc::xilinx::sysmon` is a read-only Linux IIO adapter for the ZynqMP
`xlnx,zynqmp-ams` driver (`xilinx-ams`). It is independent of `mnc::system`
and has no product dependencies. The recipe and implementation belong in
`meta-xilinx-addon`; product layers select the provider and device-tree node.

Consume `find_package(MncXilinxSysmon 1 CONFIG REQUIRED)` and link
`mnc::xilinx-sysmon`. Include `mnc/xilinx/sysmon/sysmon.hpp`. `Monitor` is the
interface; `IioMonitor` implements it. The optional filesystem root supports
native fixtures. It must not be exposed as an external request parameter.

Discovery matches the driver name and OF compatible, with an optional OF node
basename selector. It enumerates every exposed voltage and temperature channel,
including duplicate labels, reference inputs and enabled auxiliary inputs.
It never opens an acquisition device or writes sysfs. A kernel/device-tree
configuration may expose only a subset of the silicon's possible channels.

Values use IIO's `_input`, or `(raw + offset) * scale`, then convert millivolts
to volts and millidegrees Celsius to degrees Celsius. Offset defaults to zero;
raw channels require scale. Missing, malformed or non-finite readings are
unavailable with an empty optional value; a measured zero remains valid.
Snapshots have `ok`, `partial`, `unavailable` or `unsupported` status and
per-channel timestamps. Sampling is sequential, not simultaneous.

IDs combine OF device identity and the source channel (`in_voltageN` or
`in_tempN`), preserving duplicate labels and surviving `iio:deviceN`
renumbering. Channel numbering may change with kernel/device-tree channel
layout changes; IDs are not guaranteed across those changes. `domain` identifies
the physical rail domain from known vendor labels (PS, PL, or unknown), not
the ADC bank that sampled it. Unknown labels remain visible. Values are
diagnostics; no nominal voltages, limits, alarms or health judgments are inferred.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Native fixtures cover conversion, duplicate/unknown labels, device renumbering,
availability, malformed attributes and exclusion of other IIO devices. Before
release, compare the selected target's readings and exposed channel count with
its Linux IIO attributes, including permission and device disappearance cases.
