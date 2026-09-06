# Yocto workspace setup

Workspace creation is provided by the shared manifest bootstrap and each
project's manifest repository. For example:

```sh
curl -fsSL https://raw.githubusercontent.com/Monutchee/monutchee-manifest/main/common/bootstrap | sh -s -- --project zudemo --workspace /opt/monutchee/project/zudemo yocto scripts
cd /opt/monutchee/project/zudemo/yocto-build
source ./setupSDK
```

Select your project and matching workspace path in the bootstrap command.
Git credentials must permit access to the selected repositories.

`setupSDK` uses OE-Core and a separate BitBake checkout. It discovers templates
at `sources/meta-<product>/conf/templates/default` or within the shared
`sources/meta-monutchee` repository. The public demo aliases remain supported.
Ambiguous or missing templates fail before build initialization.
An explicit `--product <name>` overrides the workspace's `.mncos-product` marker.
