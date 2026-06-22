FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# k26-smk-kr-sdt does not carry the demo OpenAMP domain, so provide the
# KR260Demo carveouts and R5 remoteproc node from this product layer.
OPENAMP_EXTRA_DT_INCLUDE_FILES:kr260demo = "kr260demo-openamp.dtsi"
