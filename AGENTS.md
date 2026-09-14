# Repository guidance

Read README.md before structural changes. This repository contains shared distro,
artifact, vendor integration and public demo layers. Keep private product names,
configuration, validation logs and documentation in their separate product repos.

Generic Station artifacts belong in meta-mnc-artifact; Xilinx boot/XSDB integration
belongs in meta-xilinx-addon; ZynqMP and Kria behavior belongs in their shared
add-on layers. Product layers own application selection and image policy.
Workspace creation belongs in monutchee-manifest and each project manifest.
setupSDK must support external product layers without naming private products.

Preserve local changes and generated outputs. Never edit generated machine
configuration as a source fix. Keep numeric service identities stable.
Run affected Python unit tests for tooling changes. Recipe changes require an
affected recipe build; image, firmware and boot policy changes also require the
relevant image build and hardware validation before release.

Follow LICENSING.md. Preserve upstream notices and recipe package licenses.
Update this file when durable ownership or workflow rules change.

The reusable system SDK and daemon belong in
`meta-mncos/recipes-support/mnc-system/files/source`: portable `mnc::system`,
native `mnc::os::system`, `mnc::logging` and `mnc::Service`. Product layers supply
immutable profiles, service identities, hardware allowlists and reset paths.
Keep arbitrary command/path/unit operations out of the privileged IPC API.
