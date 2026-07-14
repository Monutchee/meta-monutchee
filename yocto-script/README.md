# Monutchee Yocto environment script

This directory provides `setupSDK`, the product-aware wrapper around Poky's
`oe-init-build-env`. Product manifests link it to `yocto-build/setupSDK`.

Workspace creation, repository synchronization, runtime directory setup, tmux
configuration, and build-stage wrapper installation are owned by
[`monutchee-manifest`](https://github.com/Monutchee/monutchee-manifest). Use the
product's manifest entry point, for example:

```bash
curl -fsSL \
  "https://raw.githubusercontent.com/Monutchee/monutchee-manifest/main/msap1/setupWorkspace" \
  | bash -s -- yocto scripts
```

After synchronization, initialize a build shell with:

```bash
source ./setupSDK --product msap1
```

Reference: [Xilinx yocto-scripts](https://github.com/Xilinx/yocto-script)
