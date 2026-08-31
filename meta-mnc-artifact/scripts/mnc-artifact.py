#!/usr/bin/env python3
"""Create and verify deterministic Monutchee Station artifacts."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
import re
import sys
import tarfile
import tempfile
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path, PurePosixPath
from typing import BinaryIO, Iterator


SCHEMA = "mnc-station-artifact"
FORMAT_VERSION = 2
MANIFEST_NAME = "manifest.json"
SIGNATURE_NAME = "manifest.sig"
RESERVED_NAMES = {MANIFEST_NAME, SIGNATURE_NAME}
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
IDENTIFIER_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]*$")


class ArtifactError(ValueError):
    """Artifact content or metadata violates the public contract."""


@dataclass(frozen=True)
class PayloadFile:
    path: str
    source: Path
    size: int
    sha256: str
    mode: int


def safe_relative_path(value: str) -> PurePosixPath:
    if not isinstance(value, str) or not value:
        raise ArtifactError("artifact paths must be non-empty strings")
    if (
        "\\" in value
        or ":" in value
        or any(ord(character) < 0x20 or ord(character) == 0x7F for character in value)
    ):
        raise ArtifactError(f"unsafe artifact path: {value!r}")
    parts = value.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        raise ArtifactError(f"unsafe artifact path: {value!r}")
    path = PurePosixPath(value)
    if path.is_absolute():
        raise ArtifactError(f"unsafe artifact path: {value!r}")
    return path


def sha256_stream(stream: BinaryIO) -> str:
    digest = hashlib.sha256()
    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(chunk)
    return digest.hexdigest()


def sha256_file(path: Path) -> str:
    with path.open("rb") as stream:
        return sha256_stream(stream)


def normalized_mode(mode: int) -> int:
    return 0o755 if mode & 0o111 else 0o644


def walk_payload(directory: Path) -> Iterator[Path]:
    for entry in sorted(os.scandir(directory), key=lambda item: item.name):
        path = Path(entry.path)
        if entry.is_symlink():
            raise ArtifactError(f"payload must not contain symlinks: {path}")
        if entry.is_dir(follow_symlinks=False):
            yield from walk_payload(path)
        elif entry.is_file(follow_symlinks=False):
            yield path
        else:
            raise ArtifactError(f"payload entry must be a regular file: {path}")


def collect_payload(payload_dir: Path) -> list[PayloadFile]:
    if payload_dir.is_symlink() or not payload_dir.is_dir():
        raise ArtifactError(f"payload directory does not exist: {payload_dir}")

    files: list[PayloadFile] = []
    for source in walk_payload(payload_dir):
        relative = source.relative_to(payload_dir).as_posix()
        safe_relative_path(relative)
        if relative in RESERVED_NAMES:
            raise ArtifactError(f"payload uses reserved path: {relative}")
        stat = source.stat()
        files.append(
            PayloadFile(
                path=relative,
                source=source,
                size=stat.st_size,
                sha256=sha256_file(source),
                mode=normalized_mode(stat.st_mode),
            )
        )
    if not files:
        raise ArtifactError("artifact payload is empty")
    return files


def validate_identifier(label: str, value: object) -> str:
    if not isinstance(value, str) or not IDENTIFIER_RE.fullmatch(value):
        raise ArtifactError(f"{label} is not a valid identifier: {value!r}")
    return value


def validate_text(label: str, value: object) -> str:
    if (
        not isinstance(value, str)
        or not value
        or any(ord(character) < 0x20 or ord(character) == 0x7F for character in value)
    ):
        raise ArtifactError(f"{label} must be a non-empty single-line string")
    return value


def json_object_without_duplicates(pairs: list[tuple[str, object]]) -> dict[str, object]:
    value: dict[str, object] = {}
    for key, item in pairs:
        if key in value:
            raise ArtifactError(f"manifest contains a duplicate JSON key: {key!r}")
        value[key] = item
    return value


def validate_created_utc(value: object) -> str:
    value = validate_text("artifact.createdUtc", value)
    if not value.endswith("Z"):
        raise ArtifactError("artifact.createdUtc must be an ISO-8601 UTC value ending in Z")
    try:
        parsed = datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError as error:
        raise ArtifactError(f"artifact.createdUtc is invalid: {value!r}") from error
    if parsed.utcoffset() is None or parsed.utcoffset().total_seconds() != 0:
        raise ArtifactError("artifact.createdUtc must use UTC")
    return value


def build_manifest(
    *,
    name: str,
    vendor: str,
    operation: str,
    product: str,
    machine: str,
    version: str,
    build_id: str,
    created_utc: str,
    executor_type: str,
    entrypoint: str,
    tftp_root: str | None,
    files: list[PayloadFile],
) -> dict[str, object]:
    executor: dict[str, object] = {
        "type": executor_type,
        "entrypoint": entrypoint,
    }
    if tftp_root:
        executor["tftpRoot"] = tftp_root

    manifest: dict[str, object] = {
        "schema": SCHEMA,
        "formatVersion": FORMAT_VERSION,
        "artifact": {
            "name": name,
            "vendor": vendor,
            "operation": operation,
            "product": product,
            "machine": machine,
            "version": version,
            "buildId": build_id,
            "createdUtc": created_utc,
        },
        "executor": executor,
        "files": {
            item.path: {
                "size": item.size,
                "sha256": item.sha256,
                "mode": f"{item.mode:04o}",
            }
            for item in files
        },
    }
    validate_manifest(manifest)
    return manifest


def require_exact_keys(label: str, value: dict[str, object], required: set[str], optional: set[str] | None = None) -> None:
    optional = optional or set()
    missing = required - value.keys()
    extra = value.keys() - required - optional
    if missing:
        raise ArtifactError(f"{label} is missing fields: {', '.join(sorted(missing))}")
    if extra:
        raise ArtifactError(f"{label} has unsupported fields: {', '.join(sorted(extra))}")


def validate_manifest(manifest: object) -> dict[str, object]:
    if not isinstance(manifest, dict):
        raise ArtifactError("manifest root must be an object")
    require_exact_keys(
        "manifest",
        manifest,
        {"schema", "formatVersion", "artifact", "executor", "files"},
    )
    if manifest["schema"] != SCHEMA:
        raise ArtifactError(f"unsupported manifest schema: {manifest['schema']!r}")
    if manifest["formatVersion"] != FORMAT_VERSION:
        raise ArtifactError(f"unsupported manifest formatVersion: {manifest['formatVersion']!r}")

    artifact = manifest["artifact"]
    if not isinstance(artifact, dict):
        raise ArtifactError("artifact must be an object")
    require_exact_keys(
        "artifact",
        artifact,
        {
            "name",
            "vendor",
            "operation",
            "product",
            "machine",
            "version",
            "buildId",
            "createdUtc",
        },
    )
    for field in ("name", "vendor", "operation", "product", "machine", "buildId"):
        validate_identifier(f"artifact.{field}", artifact[field])
    validate_text("artifact.version", artifact["version"])
    validate_created_utc(artifact["createdUtc"])

    executor = manifest["executor"]
    if not isinstance(executor, dict):
        raise ArtifactError("executor must be an object")
    require_exact_keys(
        "executor",
        executor,
        {"type", "entrypoint"},
        {"tftpRoot"},
    )
    validate_identifier("executor.type", executor["type"])
    entrypoint = safe_relative_path(validate_text("executor.entrypoint", executor["entrypoint"])).as_posix()
    tftp_root: str | None = None
    if "tftpRoot" in executor:
        tftp_root = safe_relative_path(validate_text("executor.tftpRoot", executor["tftpRoot"])).as_posix()

    files = manifest["files"]
    if not isinstance(files, dict) or not files:
        raise ArtifactError("files must be a non-empty object")
    for path, descriptor in files.items():
        safe_relative_path(path)
        if path in RESERVED_NAMES:
            raise ArtifactError(f"files contains reserved path: {path}")
        if not isinstance(descriptor, dict):
            raise ArtifactError(f"file descriptor must be an object: {path}")
        require_exact_keys(f"files[{path!r}]", descriptor, {"size", "sha256", "mode"})
        if not isinstance(descriptor["size"], int) or isinstance(descriptor["size"], bool) or descriptor["size"] < 0:
            raise ArtifactError(f"file size is invalid: {path}")
        if not isinstance(descriptor["sha256"], str) or not SHA256_RE.fullmatch(descriptor["sha256"]):
            raise ArtifactError(f"file SHA-256 is invalid: {path}")
        if descriptor["mode"] not in {"0644", "0755"}:
            raise ArtifactError(f"file mode is invalid: {path}")

    if entrypoint not in files:
        raise ArtifactError(f"executor entrypoint is absent from files: {entrypoint}")
    if files[entrypoint]["mode"] != "0755":
        raise ArtifactError(f"executor entrypoint is not executable: {entrypoint}")
    if tftp_root and not any(path.startswith(tftp_root + "/") for path in files):
        raise ArtifactError(f"executor tftpRoot has no files: {tftp_root}")
    return manifest


def tar_info(name: str, size: int, mode: int, epoch: int) -> tarfile.TarInfo:
    info = tarfile.TarInfo(name)
    info.size = size
    info.mode = mode
    info.mtime = epoch
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    return info


def create_archive(
    *,
    payload_dir: Path,
    output: Path,
    epoch: int,
    name: str,
    vendor: str,
    operation: str,
    product: str,
    machine: str,
    version: str,
    build_id: str,
    created_utc: str,
    executor_type: str,
    entrypoint: str,
    tftp_root: str | None,
) -> dict[str, object]:
    if epoch < 0:
        raise ArtifactError("archive epoch must not be negative")
    # Keep the final path component unresolved so collect_payload can reject a
    # staging directory that is itself a symlink.
    files = collect_payload(payload_dir.absolute())
    manifest = build_manifest(
        name=name,
        vendor=vendor,
        operation=operation,
        product=product,
        machine=machine,
        version=version,
        build_id=build_id,
        created_utc=created_utc,
        executor_type=executor_type,
        entrypoint=entrypoint,
        tftp_root=tftp_root,
        files=files,
    )
    manifest_data = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")

    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{output.name}.", dir=output.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as raw:
            with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=epoch) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as archive:
                    archive.addfile(
                        tar_info(MANIFEST_NAME, len(manifest_data), 0o644, epoch),
                        io.BytesIO(manifest_data),
                    )
                    for item in files:
                        with item.source.open("rb") as stream:
                            archive.addfile(
                                tar_info(item.path, item.size, item.mode, epoch),
                                stream,
                            )
        verify_archive(temporary)
        os.replace(temporary, output)
        output.chmod(0o644)
    finally:
        temporary.unlink(missing_ok=True)
    return manifest


def verify_archive(archive_path: Path) -> dict[str, object]:
    archive_path = archive_path.resolve()
    if not archive_path.is_file():
        raise ArtifactError(f"artifact archive does not exist: {archive_path}")
    try:
        archive = tarfile.open(archive_path, mode="r:gz")
    except (OSError, tarfile.TarError) as error:
        raise ArtifactError(f"artifact is not a readable tar.gz archive: {archive_path}") from error

    with archive:
        members: dict[str, tarfile.TarInfo] = {}
        for member in archive.getmembers():
            safe_relative_path(member.name)
            if member.name in members:
                raise ArtifactError(f"archive contains a duplicate path: {member.name}")
            if not member.isfile():
                raise ArtifactError(f"archive entry is not a regular file: {member.name}")
            if member.uid != 0 or member.gid != 0:
                raise ArtifactError(f"archive entry ownership must be 0/0: {member.name}")
            members[member.name] = member

        manifest_member = members.get(MANIFEST_NAME)
        if manifest_member is None:
            raise ArtifactError(f"archive is missing {MANIFEST_NAME}")
        if manifest_member.mode & 0o777 != 0o644:
            raise ArtifactError(f"{MANIFEST_NAME} mode must be 0644")
        stream = archive.extractfile(manifest_member)
        if stream is None:
            raise ArtifactError(f"archive cannot read {MANIFEST_NAME}")
        try:
            manifest = json.load(stream, object_pairs_hook=json_object_without_duplicates)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ArtifactError(f"archive has an invalid {MANIFEST_NAME}") from error
        validate_manifest(manifest)

        signature_member = members.get(SIGNATURE_NAME)
        if signature_member is not None and signature_member.mode & 0o777 != 0o644:
            raise ArtifactError(f"{SIGNATURE_NAME} mode must be 0644")

        expected = set(manifest["files"])
        actual = set(members) - {MANIFEST_NAME, SIGNATURE_NAME}
        if actual != expected:
            missing = sorted(expected - actual)
            extra = sorted(actual - expected)
            detail = []
            if missing:
                detail.append("missing=" + ",".join(missing))
            if extra:
                detail.append("extra=" + ",".join(extra))
            raise ArtifactError("archive payload differs from manifest: " + " ".join(detail))

        for path in sorted(expected):
            descriptor = manifest["files"][path]
            member = members[path]
            if member.size != descriptor["size"]:
                raise ArtifactError(f"archive size differs from manifest: {path}")
            actual_mode = f"{member.mode & 0o777:04o}"
            if actual_mode != descriptor["mode"]:
                raise ArtifactError(f"archive mode differs from manifest: {path}")
            stream = archive.extractfile(member)
            if stream is None or sha256_stream(stream) != descriptor["sha256"]:
                raise ArtifactError(f"archive SHA-256 differs from manifest: {path}")
    return manifest


def create_command(args: argparse.Namespace) -> None:
    create_archive(
        payload_dir=Path(args.payload_dir),
        output=Path(args.output),
        epoch=args.epoch,
        name=args.name,
        vendor=args.vendor,
        operation=args.operation,
        product=args.product,
        machine=args.machine,
        version=args.version,
        build_id=args.build_id,
        created_utc=args.created_utc,
        executor_type=args.executor_type,
        entrypoint=args.entrypoint,
        tftp_root=args.tftp_root,
    )
    print(Path(args.output).resolve())


def verify_command(args: argparse.Namespace) -> None:
    manifest = verify_archive(Path(args.archive))
    print(json.dumps(manifest, indent=2, sort_keys=True))


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create", help="create and verify an artifact")
    create.add_argument("--payload-dir", required=True)
    create.add_argument("--output", required=True)
    create.add_argument("--epoch", required=True, type=int)
    create.add_argument("--name", required=True)
    create.add_argument("--vendor", required=True)
    create.add_argument("--operation", required=True)
    create.add_argument("--product", required=True)
    create.add_argument("--machine", required=True)
    create.add_argument("--version", required=True)
    create.add_argument("--build-id", required=True)
    create.add_argument("--created-utc", required=True)
    create.add_argument("--executor-type", required=True)
    create.add_argument("--entrypoint", required=True)
    create.add_argument("--tftp-root")
    create.set_defaults(handler=create_command)

    verify = subparsers.add_parser("verify", help="verify an existing artifact")
    verify.add_argument("--archive", required=True)
    verify.set_defaults(handler=verify_command)
    return parser


def main(argv: list[str] | None = None) -> int:
    try:
        args = argument_parser().parse_args(argv)
        args.handler(args)
    except (ArtifactError, OSError) as error:
        print(f"mnc-artifact: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
