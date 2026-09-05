# Release metadata is separate from the Station-facing archive contract.
MNCOS_RELEASE_REPORT_DIR = "${DEPLOY_DIR_IMAGE}/${IMAGE_LINK_NAME}.mncos-reports"
MNCOS_RELEASE_CVE_RECIPES ?= "${PREFERRED_PROVIDER_virtual/kernel}"

python do_mncos_release_reports() {
    import mncos.reports
    metadata = {
        "schema": "mncos-release-reports-v1",
        "image": d.getVar("PN"),
        "machine": d.getVar("MACHINE"),
        "distro": d.getVar("DISTRO"),
        "distro_version": d.getVar("DISTRO_VERSION"),
        "headless": d.getVar("MNCOS_HEADLESS"),
        "cve_policy": "report-only",
        "cve_scope": "image and listed recipes; standalone/FreeRTOS firmware needs separate review",
    }
    try:
        mncos.reports.collect_image(d.getVar("DEPLOY_DIR_IMAGE"),
            d.getVar("IMAGE_LINK_NAME"), d.getVar("MACHINE"),
            d.getVar("MNCOS_RELEASE_REPORT_DIR"), metadata,
            {recipe: os.path.join(d.getVar("CVE_CHECK_DIR"), recipe + "_cve.json")
             for recipe in d.getVar("MNCOS_RELEASE_CVE_RECIPES").split()})
    except (OSError, ValueError) as error:
        bb.fatal(str(error))
}
do_mncos_release_reports[depends] += "virtual/kernel:do_deploy"
do_mncos_release_reports[depends] += "${@' '.join(recipe + ':do_cve_check' for recipe in d.getVar('MNCOS_RELEASE_CVE_RECIPES').split())}"
do_mncos_release_reports[nostamp] = "1"
addtask mncos_release_reports after do_image_complete before do_build
