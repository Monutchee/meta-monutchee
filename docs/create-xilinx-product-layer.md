# Creating a Xilinx product layer

`create-xilinx-product-layer.py` creates a new Monutchee product layer from a
maintained Xilinx board profile. Use it when a new product needs its own image,
DFX firmware identity, generated machine name, and build configuration.

This tool fills a different role from the two standard Yocto mechanisms:

- `bitbake-layers create-layer` creates a generic layer skeleton with an
  example recipe. It does not know Monutchee's Xilinx, OpenAMP, image, firmware,
  or production-flashing conventions.
- `TEMPLATECONF` and `bitbake-layers save-build-conf` manage the `local.conf`
  and `bblayers.conf` used to initialize a build directory. They do not create
  a product layer.

## Prerequisites

- Python 3.8 or newer.
- A checkout of `meta-monutchee`.
- A lowercase product identifier that can also be used as the generated Yocto
  machine name.
- A component prefix used by the future `<Prefix>_APU`, `<Prefix>_RPU`, and
  `<Prefix>_PL` repositories and XSA filename.

Run the command from the `meta-monutchee` repository root.

## Usage

```text
python3 scripts/create-xilinx-product-layer.py \
    --product PRODUCT \
    --project-prefix PREFIX \
    --board kr260 \
    [--output-root DIRECTORY]
```

Options:

- `--product`: lowercase product and machine identifier. It must start with a
  letter and contain lowercase letters, digits, or single hyphen separators.
- `--project-prefix`: case-sensitive component/artifact prefix. It must start
  with a letter and contain letters, digits, or underscores.
- `--board`: board profile to use. The currently supported value is `kr260`.
- `--output-root`: parent directory in which `meta-<product>` is created. It
  defaults to the `meta-monutchee` repository root and is mainly useful for
  tests or reviewing generated output elsewhere.
- `--help`: print the command-line reference.

The tool refuses to modify or replace an existing `meta-<product>` directory.
There is deliberately no force option.

## MSAP1 example

```bash
python3 scripts/create-xilinx-product-layer.py \
    --product msap1 \
    --project-prefix MSAP1 \
    --board kr260
```

This creates `meta-msap1` with:

- `conf/layer.conf` and a `conf/templates/default` build template.
- A build-layer dependency on the vendor-neutral `meta-mnc-artifact` contract
  used by Xilinx Station artifact recipes.
- `msap1-image` and `msap1-production-flash-image` targets.
- A `msap1-dfx-firmware` recipe expecting `MSAP1_PL.bit`, `pl.dtso`,
  `R5c0.elf`, and `R5c1.elf` from the workspace-generated artifacts.
- A guarded TFTP-to-eMMC production flasher.
- A product README and the repository MIT license.

No static `conf/machine/msap1.conf` is created. Xilinx `gen-machineconf`
generates that machine from the product XSA and system device tree.

## Required follow-up work

1. Review the generated layer, particularly its image package list, DFX
   autoload policy, both R5 firmware entries, and production-flashing target.
2. Add a product profile to `monutchee-manifest/common/build/products`. For the
   KR260 profile, provide the product/machine name, component directories, XSA
   basename, `zynqmp-smk-k26-reva` board DTS, Kria machine YAML, OpenAMP domain,
   and default image target.
3. Ensure `monutchee-manifest/common/setupWorkspace` recognizes the product and
   installs the shared build scripts. Workspace initialization belongs to the
   manifest repository, not this layer repository.
4. Set up Yocto and the wrapper scripts before component repositories exist:

   ```bash
   ./msap1/setupWorkspace --workspace /path/to/msap1 yocto scripts
   ```

5. When the component repositories and bitstream-inclusive XSA exist, run the
   hardware-to-image stages:

   ```bash
   ./make_PL.sh --xsa /path/to/MSAP1_PL.xsa
   ./make_mconf.sh
   ./make_RPU.sh
   ./make_yocto.sh
   ```

The KR260 profile uses `k26-smk-kr-sdt.yaml`, the KR260 board DTS
`zynqmp-smk-k26-reva`, dual R5 FreeRTOS/OpenAMP domains, and a full PL overlay.

## Validation

After adding the manifest profile and synchronizing a workspace:

```bash
source ./setupSDK --product msap1 build-msap1
bitbake-layers show-layers
```

Confirm that `meta-msap1` appears in the layer list. A complete image build also
requires the generated `msap1` machine configuration, PL overlay/bitstream, both
R5 ELF files, and APU source checkout.

Run the generator unit tests from the `meta-monutchee` repository:

```bash
python3 -m unittest discover -s scripts/tests -v
```

## Common errors

- `destination already exists`: choose a new product name or review/remove the
  existing directory yourself. The generator never overwrites it.
- `--product must start ...`: use a lowercase BitBake-safe product identifier,
  such as `msap1` or `product-2`.
- `--project-prefix must start ...`: use a filename-safe prefix such as `MSAP1`.
- Missing `MSAP1_PL.bit`, `pl.dtso`, or R5 ELF errors: run the PL, machine-conf,
  and RPU stages after the real component repositories and XSA are available.
- Unsupported board choice: only the `kr260` profile exists currently; add and
  test another maintained template before using a different Xilinx board.

The generator does not create or clone GitHub repositories, component source
trees, XSA files, bitstreams, RPU firmware, or generated machine configuration.
