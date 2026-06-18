# meta-mncos

This layer contains vendor-neutral MNCOS distribution policy, shared OS
services, and reusable package definitions. Machine/product layers own concrete
image recipes and hardware-specific boot or flashing logic.

## Product Images

`meta-mncos` intentionally does not provide a buildable image target. A product
layer should create its own target, such as `zudemo-image` or a future
`kr260demo-image`, and install the shared MNCOS package set from this
layer.

## Reusable Package Set

`recipes-core/images/include/mncos-image-common.inc` defines the common MNCOS
image scaffolding and base userspace package set. A product image `require`s
it and then customises the set with standard overrides:

```bitbake
require recipes-core/images/include/mncos-image-common.inc

IMAGE_INSTALL:append = " board-firmware fwctl"   # add product-specific packages
IMAGE_INSTALL:remove = " htop"                   # trim packages from the base set
```

`:remove` is applied last and overrides `:append`, so a product can
deterministically drop a base package. Keeping the shared set in the
machine-specific image recipe (rather than an `allarch` packagegroup) also
avoids the dynamic-package-rename QA error.

## Image Identity

Product image recipes can inherit `mncos-image-identity` to identify themselves
on the serial login banner and in `/etc/mncos-image-info`. On a running target,
use:

```sh
cat /etc/mncos-image-info
```

## Reference

- https://github.com/meta-homeassistant
