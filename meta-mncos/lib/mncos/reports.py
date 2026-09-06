"""Validate and collect the release reports produced by OE-Core."""

import json
import shutil
import tempfile
from pathlib import Path


def collect_image(deploy_dir, image_link_name, machine, output, metadata, recipe_reports=None):
    deploy = Path(deploy_dir)
    files = {
        "image.cve.json": deploy / (image_link_name + ".json"),
        "image.spdx.tar.zst": deploy / (image_link_name + ".spdx.tar.zst"),
        "image.manifest": deploy / (image_link_name + ".manifest"),
        "kernel.config": deploy / ("mncos-kernel-" + machine + ".config"),
    }
    for source in files.values():
        if not source.is_file() or source.stat().st_size == 0:
            raise ValueError("Missing or empty required MNCOS release report: " + str(source))
    cve = json.loads(files["image.cve.json"].read_text())
    if cve.get("version") != "1" or not isinstance(cve.get("package"), list) or not cve["package"]:
        raise ValueError("Incomplete image CVE report: " + str(files["image.cve.json"]))
    metadata = dict(metadata)
    metadata["recipe_cve_coverage"] = {}
    for recipe, source in (recipe_reports or {}).items():
        source = Path(source)
        available = source.is_file() and source.stat().st_size > 0
        metadata["recipe_cve_coverage"][recipe] = "reported" if available else "unavailable; requires review"
        if available:
            json.loads(source.read_text())
            files[recipe + ".cve.json"] = source
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".mncos-reports-", dir=output.parent) as temporary:
        staging = Path(temporary) / "reports"
        staging.mkdir()
        for name, source in files.items():
            shutil.copyfile(source, staging / name)
        (staging / "build.json").write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
        if output.exists():
            shutil.rmtree(output)
        staging.rename(output)
