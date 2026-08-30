from __future__ import annotations

import gzip
import importlib.util
import io
import json
import os
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parents[1] / "scripts" / "mnc-artifact.py"
SCHEMA_PATH = (
    Path(__file__).resolve().parents[1]
    / "schema"
    / "mnc-station-artifact-v2.schema.json"
)
SPEC = importlib.util.spec_from_file_location("mnc_artifact", MODULE_PATH)
assert SPEC and SPEC.loader
mnc_artifact = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = mnc_artifact
SPEC.loader.exec_module(mnc_artifact)


class ArtifactTests(unittest.TestCase):
    def metadata(self) -> dict[str, object]:
        return {
            "epoch": 1_704_067_200,
            "name": "msap1-jtag-image",
            "vendor": "xilinx",
            "operation": "jtag-boot",
            "product": "msap1",
            "machine": "msap1",
            "version": "2026.1",
            "build_id": "20240101000000",
            "created_utc": "2024-01-01T00:00:00Z",
            "executor_type": "xilinx-xsdb",
            "entrypoint": "jtag/load-jtag-image.tcl",
            "tftp_root": "tftp",
        }

    def payload(self, root: Path) -> Path:
        payload = root / "payload"
        (payload / "jtag").mkdir(parents=True)
        (payload / "tftp").mkdir()
        loader = payload / "jtag" / "load-jtag-image.tcl"
        loader.write_text("#!/usr/bin/env xsdb\nputs ok\n", encoding="utf-8")
        loader.chmod(0o755)
        (payload / "jtag" / "fsbl.elf").write_bytes(b"fsbl")
        (payload / "tftp" / "Image").write_bytes(b"kernel")
        return payload

    def test_create_is_deterministic_and_manifest_covers_payload(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            payload = self.payload(root)
            first = root / "first.tar.gz"
            second = root / "second.tar.gz"

            manifest = mnc_artifact.create_archive(
                payload_dir=payload, output=first, **self.metadata()
            )
            mnc_artifact.create_archive(
                payload_dir=payload, output=second, **self.metadata()
            )

            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(manifest, mnc_artifact.verify_archive(first))
            self.assertEqual(
                set(manifest["files"]),
                {
                    "jtag/fsbl.elf",
                    "jtag/load-jtag-image.tcl",
                    "tftp/Image",
                },
            )
            self.assertEqual(
                manifest["files"]["jtag/load-jtag-image.tcl"]["mode"],
                "0755",
            )
            self.assertEqual(
                manifest["files"]["jtag/fsbl.elf"]["mode"],
                "0644",
            )

            with tarfile.open(first, "r:gz") as archive:
                self.assertEqual(
                    archive.getnames(),
                    [
                        "manifest.json",
                        "jtag/fsbl.elf",
                        "jtag/load-jtag-image.tcl",
                        "tftp/Image",
                    ],
                )
                for member in archive.getmembers():
                    self.assertTrue(member.isfile())
                    self.assertEqual(member.uid, 0)
                    self.assertEqual(member.gid, 0)
                    self.assertEqual(member.mtime, self.metadata()["epoch"])

    def test_manifest_matches_json_schema_when_validator_is_available(self) -> None:
        try:
            import jsonschema
        except ImportError:
            self.skipTest("python jsonschema module is unavailable")

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "artifact.tar.gz"
            manifest = mnc_artifact.create_archive(
                payload_dir=self.payload(root), output=output, **self.metadata()
            )
            schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
            jsonschema.Draft202012Validator(schema).validate(manifest)

    def test_payload_symlink_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            payload = self.payload(root)
            os.symlink(payload / "tftp" / "Image", payload / "tftp" / "kernel-link")
            with self.assertRaisesRegex(mnc_artifact.ArtifactError, "symlink"):
                mnc_artifact.create_archive(
                    payload_dir=payload,
                    output=root / "artifact.tar.gz",
                    **self.metadata(),
                )

    def test_payload_directory_symlink_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            payload = self.payload(root)
            payload_link = root / "payload-link"
            os.symlink(payload, payload_link, target_is_directory=True)
            with self.assertRaisesRegex(mnc_artifact.ArtifactError, "payload directory"):
                mnc_artifact.create_archive(
                    payload_dir=payload_link,
                    output=root / "artifact.tar.gz",
                    **self.metadata(),
                )

    def test_reserved_signature_path_is_rejected_from_payload(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            payload = self.payload(root)
            (payload / "manifest.sig").write_bytes(b"not-a-release-signature")
            with self.assertRaisesRegex(mnc_artifact.ArtifactError, "reserved"):
                mnc_artifact.create_archive(
                    payload_dir=payload,
                    output=root / "artifact.tar.gz",
                    **self.metadata(),
                )

    def test_empty_payload_and_missing_entrypoint_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            empty = root / "empty"
            empty.mkdir()
            with self.assertRaisesRegex(mnc_artifact.ArtifactError, "empty"):
                mnc_artifact.create_archive(
                    payload_dir=empty,
                    output=root / "empty.tar.gz",
                    **self.metadata(),
                )

            payload = root / "payload"
            (payload / "tftp").mkdir(parents=True)
            (payload / "tftp" / "Image").write_bytes(b"kernel")
            with self.assertRaisesRegex(mnc_artifact.ArtifactError, "entrypoint"):
                mnc_artifact.create_archive(
                    payload_dir=payload,
                    output=root / "missing.tar.gz",
                    **self.metadata(),
                )

    def test_unsafe_paths_are_rejected(self) -> None:
        for path in (
            "/absolute",
            "../escape",
            "a/../escape",
            "a//b",
            "a\\b",
            "C:/escape",
            "a:b",
            "a\tb",
        ):
            with self.subTest(path=path):
                with self.assertRaises(mnc_artifact.ArtifactError):
                    mnc_artifact.safe_relative_path(path)

    def test_duplicate_archive_member_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "duplicate.tar.gz"
            with output.open("wb") as raw:
                with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
                    with tarfile.open(fileobj=compressed, mode="w") as archive:
                        for content in (b"{}", b"{}"):
                            info = tarfile.TarInfo("manifest.json")
                            info.size = len(content)
                            info.mode = 0o644
                            archive.addfile(info, io.BytesIO(content))
            with self.assertRaisesRegex(mnc_artifact.ArtifactError, "duplicate"):
                mnc_artifact.verify_archive(output)

    def test_checksum_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "mismatch.tar.gz"
            payload = self.payload(root)
            good = root / "good.tar.gz"
            manifest = mnc_artifact.create_archive(
                payload_dir=payload, output=good, **self.metadata()
            )

            with output.open("wb") as raw:
                with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
                    with tarfile.open(fileobj=compressed, mode="w") as archive:
                        manifest_data = (
                            json.dumps(manifest, indent=2, sort_keys=True) + "\n"
                        ).encode()
                        info = tarfile.TarInfo("manifest.json")
                        info.size = len(manifest_data)
                        info.mode = 0o644
                        archive.addfile(info, io.BytesIO(manifest_data))
                        for path, descriptor in sorted(manifest["files"].items()):
                            data = (payload / path).read_bytes()
                            if path == "tftp/Image":
                                data = b"tamper"
                            info = tarfile.TarInfo(path)
                            info.size = len(data)
                            info.mode = int(descriptor["mode"], 8)
                            archive.addfile(info, io.BytesIO(data))

            with self.assertRaisesRegex(mnc_artifact.ArtifactError, "size|SHA-256"):
                mnc_artifact.verify_archive(output)

    def test_optional_release_signature_is_not_a_payload_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            unsigned = root / "unsigned.tar.gz"
            signed = root / "signed.tar.gz"
            manifest = mnc_artifact.create_archive(
                payload_dir=self.payload(root),
                output=unsigned,
                **self.metadata(),
            )

            with tarfile.open(unsigned, "r:gz") as source:
                source_members = [
                    (member, source.extractfile(member).read())
                    for member in source.getmembers()
                ]

            with signed.open("wb") as raw:
                with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
                    with tarfile.open(fileobj=compressed, mode="w") as archive:
                        for member, data in source_members:
                            archive.addfile(member, io.BytesIO(data))
                            if member.name == "manifest.json":
                                signature = tarfile.TarInfo("manifest.sig")
                                signature.size = 9
                                signature.mode = 0o644
                                signature.uid = 0
                                signature.gid = 0
                                archive.addfile(signature, io.BytesIO(b"signature"))

            self.assertEqual(manifest, mnc_artifact.verify_archive(signed))


if __name__ == "__main__":
    unittest.main()
