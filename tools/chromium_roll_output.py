#!/usr/bin/env python3
"""Race-resistant report output handling for Chromium roll tooling."""

from __future__ import annotations

import os
import pathlib
import secrets
import stat
from dataclasses import dataclass
from typing import Sequence


class ReportOutputError(ValueError):
    """A requested report destination is unsafe or cannot be reserved."""


def _contains(root: pathlib.Path, candidate: pathlib.Path) -> bool:
    try:
        candidate.relative_to(root)
    except ValueError:
        return False
    return True


@dataclass
class PreparedReportOutput:
    """A report destination whose parent and temporary inode are pinned."""

    output: pathlib.Path | None
    directory_fd: int | None = None
    temporary_fd: int | None = None
    temporary_name: str | None = None
    final_name: str | None = None
    committed: bool = False

    @classmethod
    def prepare(
        cls,
        output: pathlib.Path | None,
        *,
        repository: pathlib.Path,
        checkout: pathlib.Path | None,
        protected_files: Sequence[pathlib.Path],
    ) -> PreparedReportOutput:
        if output is None:
            return cls(output=None)
        if output.name in {"", ".", ".."}:
            raise ReportOutputError("report output must name a regular file")
        if output.is_symlink():
            raise ReportOutputError("refusing to overwrite a symlink output")

        resolved = output.parent.resolve(strict=False) / output.name
        repository = repository.resolve()
        allowed_reports = (repository / "artifacts/build").resolve()
        protected = {path.resolve(strict=False) for path in protected_files}
        production_config = (repository / "config/chromium.json").resolve(
            strict=False
        )
        if resolved == production_config:
            raise ReportOutputError(
                "refusing to overwrite production config/chromium.json"
            )
        if resolved in protected:
            raise ReportOutputError("refusing to overwrite a protected roll input")
        if checkout is not None and _contains(checkout.resolve(), resolved):
            raise ReportOutputError("refusing report output inside the Chromium checkout")
        if _contains(repository, resolved) and not _contains(allowed_reports, resolved):
            raise ReportOutputError(
                "repository report output must stay below artifacts/build"
            )

        resolved.parent.mkdir(parents=True, exist_ok=True)
        flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0)
        flags |= getattr(os, "O_CLOEXEC", 0)
        flags |= getattr(os, "O_NOFOLLOW", 0)
        try:
            directory_fd = os.open(resolved.parent, flags)
        except OSError as error:
            raise ReportOutputError(f"cannot open report directory: {error}") from error
        try:
            try:
                existing = os.stat(
                    resolved.name, dir_fd=directory_fd, follow_symlinks=False
                )
            except FileNotFoundError:
                existing = None
            if existing is not None and not stat.S_ISREG(existing.st_mode):
                raise ReportOutputError(
                    "refusing to replace a non-regular report output"
                )
            temporary_name = f".ahoi-roll-{os.getpid()}-{secrets.token_hex(8)}.tmp"
            temporary_flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
            temporary_flags |= getattr(os, "O_CLOEXEC", 0)
            temporary_fd = os.open(
                temporary_name,
                temporary_flags,
                0o600,
                dir_fd=directory_fd,
            )
        except BaseException:
            os.close(directory_fd)
            raise
        return cls(
            output=resolved,
            directory_fd=directory_fd,
            temporary_fd=temporary_fd,
            temporary_name=temporary_name,
            final_name=resolved.name,
        )

    def write(self, rendered: str) -> None:
        if self.output is None:
            raise ReportOutputError("stdout output cannot be committed as a file")
        if self.directory_fd is None or self.temporary_fd is None:
            raise ReportOutputError("report output reservation is closed")
        payload = rendered.encode("utf-8")
        offset = 0
        while offset < len(payload):
            offset += os.write(self.temporary_fd, payload[offset:])
        os.fsync(self.temporary_fd)
        os.close(self.temporary_fd)
        self.temporary_fd = None
        try:
            existing = os.stat(
                self.final_name,
                dir_fd=self.directory_fd,
                follow_symlinks=False,
            )
        except FileNotFoundError:
            existing = None
        if existing is not None and not stat.S_ISREG(existing.st_mode):
            raise ReportOutputError("report output changed to a non-regular file")
        os.replace(
            self.temporary_name,
            self.final_name,
            src_dir_fd=self.directory_fd,
            dst_dir_fd=self.directory_fd,
        )
        self.committed = True
        os.fsync(self.directory_fd)

    def close(self) -> None:
        if self.temporary_fd is not None:
            os.close(self.temporary_fd)
            self.temporary_fd = None
        if (
            not self.committed
            and self.directory_fd is not None
            and self.temporary_name is not None
        ):
            try:
                os.unlink(self.temporary_name, dir_fd=self.directory_fd)
            except FileNotFoundError:
                pass
        if self.directory_fd is not None:
            os.close(self.directory_fd)
            self.directory_fd = None

    def __enter__(self) -> PreparedReportOutput:
        return self

    def __exit__(self, *unused: object) -> None:
        del unused
        self.close()
