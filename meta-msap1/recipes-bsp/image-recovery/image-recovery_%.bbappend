# The inherited KR260 machine override makes the vendor recovery recipe look
# compatible, but that recipe has download URL flags only for the vendor
# machine name, not for the generated "msap1" machine. MSAP1 uses its own
# production-flashing flow and does not consume image-recovery.
COMPATIBLE_MACHINE:msap1 = "^$"

# Fetch URI expansion happens before COMPATIBLE_MACHINE filtering, so provide
# valid aliases even though the recipe is skipped for this product.
IR_PATH[msap1] = "${@d.getVarFlag('IR_PATH', 'k26-smk-kr-sdt') or ''}"
WEB_PATH[msap1] = "${@d.getVarFlag('WEB_PATH', 'k26-smk-kr-sdt') or ''}"
SRC_URI[msap1_ir.sha256sum] = "${@d.getVarFlag('SRC_URI', 'k26-smk-kr-sdt_ir.sha256sum') or ''}"
SRC_URI[msap1_web.sha256sum] = "${@d.getVarFlag('SRC_URI', 'k26-smk-kr-sdt_web.sha256sum') or ''}"
