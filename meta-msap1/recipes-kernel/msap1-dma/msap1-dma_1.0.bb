SUMMARY = "MSAP1 PL acquisition DMAengine consumers"
DESCRIPTION = "One kernel module with a shared AXI DMA S2MM transport core \
exposing PL meter result records through /dev/msap1-meter and raw waveform \
blocks through /dev/msap1-waveform."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=53cbd03cc56142008ba1bda05f2ecea2"

inherit module

COMPATIBLE_MACHINE = "^msap1$"

SRC_URI = " \
    file://COPYING;subdir=src \
    file://Makefile;subdir=src \
    file://msap1_dma_uapi.h;subdir=src \
    file://msap1_dma.h;subdir=src \
    file://msap1_dma_core.c;subdir=src \
    file://msap1_dma_meter.c;subdir=src \
    file://msap1_dma_waveform.c;subdir=src \
"

# Sources live in their own directory, NOT at the ${WORKDIR} root:
# do_unpack's cleandirs wipes ${S} before every unpack only when
# S != WORKDIR (base.bbclass), so this is what guarantees a source edit
# rebuilds from a coherent tree.  With S = "${WORKDIR}", stale objects and
# the previous .ko survived edits, and the re-created files reached
# do_install/do_package under new inodes that no longer matched pseudo's
# per-recipe database — the "inode mismatch" warning storms on rebuild.
S = "${WORKDIR}/src"

KERNEL_MODULE_AUTOLOAD += "msap1_dma"
