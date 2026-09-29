#!/usr/bin/env python3
"""Verify or safely refresh a deterministic Ahoi Chromium overlay checkout."""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import tempfile

from compose_overlay import compose_overlay, isolated_git_environment
from overlay_fingerprint import fingerprint as overlay_inputs_fingerprint
from overlay_state_storage import (
    ExpectedOverlay,
    OverlayApplyResult,
    OverlayRefreshResult,
    OverlayRestoreResult,
    OverlayStateError,
    VerifiedOverlayState,
    create_overlay_state_atomic as _create_overlay_state_atomic,
    delta_fingerprint,
    load_overlay_state,
    new_overlay_state as _new_overlay_state,
    remove_exact_overlay_state as _remove_exact_overlay_state,
    require_commit as _require_commit,
    write_overlay_state_atomic as _write_overlay_state_atomic,
)


def _run_git(
    *args: str,
    checkout: pathlib.Path,
    env: dict[str, str] | None = None,
) -> str:
    effective_environment = isolated_git_environment() if env is None else env
    try:
        result = subprocess.run(
            ("git", *args),
            cwd=checkout,
            env=effective_environment,
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or error.stdout or "").strip()
        suffix = f": {detail}" if detail else ""
        raise OverlayStateError(f"git {' '.join(args)} failed{suffix}") from error
    return result.stdout.strip()


def _run_git_apply(
    delta: bytes,
    *,
    checkout: pathlib.Path,
    check_only: bool = False,
    reverse: bool = False,
) -> None:
    """Apply one complete binary tree delta without fallback or rejects."""

    command = ["git", "apply", "--whitespace=error-all"]
    if check_only:
        command.append("--check")
    if reverse:
        command.append("--reverse")
    effective_environment = isolated_git_environment()
    try:
        subprocess.run(
            command,
            cwd=checkout,
            env=effective_environment,
            input=delta,
            check=True,
            capture_output=True,
        )
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or error.stdout or b"").decode(
            "utf-8", "replace"
        ).strip()
        suffix = f": {detail}" if detail else ""
        action = "preflight" if check_only else "application"
        direction = "rollback " if reverse else ""
        raise OverlayStateError(
            f"overlay refresh {direction}{action} failed{suffix}"
        ) from error


def _compose_expected_overlay(
    repository: pathlib.Path,
    checkout: pathlib.Path,
    expected_commit: str,
    *,
    diff_base_tree: str | None = None,
) -> tuple[ExpectedOverlay, bytes]:
    """Compose source truth and optionally a transition from a verified tree."""

    repository = repository.resolve()
    checkout = checkout.resolve()
    expected_commit = _require_commit(
        expected_commit, label="expected Chromium commit"
    )
    if not (checkout / ".git").exists():
        raise OverlayStateError(f"not a Git checkout: {checkout}")
    if _run_git("rev-parse", "HEAD", checkout=checkout) != expected_commit:
        raise OverlayStateError("Chromium checkout HEAD does not match the configured pin")

    fingerprint_before = overlay_inputs_fingerprint(repository)
    try:
        transition, expected_tree = compose_overlay(
            checkout,
            repository / "overlay/chromium/src",
            repository / "patches/chromium/series",
            repository / "patches/chromium",
            base_revision=expected_commit,
            diff_base_revision=diff_base_tree,
        )
    except (OSError, subprocess.CalledProcessError, SystemExit) as error:
        raise OverlayStateError(f"could not compose deterministic overlay: {error}") from error
    fingerprint_after = overlay_inputs_fingerprint(repository)
    if fingerprint_before != fingerprint_after:
        raise OverlayStateError("overlay inputs changed while they were being composed")
    if _run_git("rev-parse", "HEAD", checkout=checkout) != expected_commit:
        raise OverlayStateError("Chromium checkout HEAD changed during overlay composition")
    return (
        ExpectedOverlay(
            chromium_commit=expected_commit,
            input_fingerprint=fingerprint_after,
            tree=expected_tree,
            delta_fingerprint=delta_fingerprint(expected_commit, expected_tree),
        ),
        transition,
    )


def derive_expected_overlay(
    repository: pathlib.Path,
    checkout: pathlib.Path,
    expected_commit: str,
) -> ExpectedOverlay:
    """Recompose the expected tree solely from the pin and current source inputs."""

    expected, _ = _compose_expected_overlay(
        repository, checkout, expected_commit
    )
    return expected


def current_checkout_tree(checkout: pathlib.Path, expected_commit: str) -> str:
    """Stage the complete non-ignored worktree into an isolated index and return its tree."""

    checkout = checkout.resolve()
    expected_commit = _require_commit(
        expected_commit, label="expected Chromium commit"
    )
    if not (checkout / ".git").exists():
        raise OverlayStateError(f"not a Git checkout: {checkout}")
    if _run_git("rev-parse", "HEAD", checkout=checkout) != expected_commit:
        raise OverlayStateError("Chromium checkout HEAD does not match the configured pin")
    environment = isolated_git_environment()
    head_tree = _run_git(
        "rev-parse",
        f"{expected_commit}^{{tree}}",
        checkout=checkout,
        env=environment,
    )
    index_tree = _run_git("write-tree", checkout=checkout, env=environment)
    if index_tree != head_tree:
        raise OverlayStateError(
            "Chromium index must match the pinned HEAD before overlay verification"
        )
    index_value = _run_git(
        "rev-parse", "--git-path", "index", checkout=checkout, env=environment
    )
    index_path = pathlib.Path(index_value)
    if not index_path.is_absolute():
        index_path = checkout / index_path
    if not index_path.is_file():
        raise OverlayStateError(f"Chromium Git index is missing: {index_path}")
    with tempfile.TemporaryDirectory(prefix="ahoi-checkout-index-") as temp_root:
        temporary_index = pathlib.Path(temp_root) / "index"
        shutil.copyfile(index_path, temporary_index)
        environment["GIT_INDEX_FILE"] = str(temporary_index)
        _run_git(
            "-c",
            "core.fileMode=true",
            "-c",
            "core.symlinks=true",
            "add",
            "--all",
            "--",
            checkout=checkout,
            env=environment,
        )
        tree = _run_git("write-tree", checkout=checkout, env=environment)
    if _run_git("rev-parse", "HEAD", checkout=checkout) != expected_commit:
        raise OverlayStateError("Chromium checkout HEAD changed while deriving its tree")
    return tree


def _assert_refresh_preconditions(
    repository: pathlib.Path,
    checkout: pathlib.Path,
    state_path: pathlib.Path,
    expected_commit: str,
    previous_state: dict[str, Any],
    previous_tree: str,
    input_fingerprint: str,
) -> None:
    """Recheck every authority immediately before a refresh mutation."""

    if load_overlay_state(state_path, expected_commit) != previous_state:
        raise OverlayStateError("overlay state changed during refresh")
    if overlay_inputs_fingerprint(repository.resolve()) != input_fingerprint:
        raise OverlayStateError("overlay inputs changed after refresh composition")
    if current_checkout_tree(checkout, expected_commit) != previous_tree:
        raise OverlayStateError("Chromium checkout changed during overlay refresh")


def _rollback_exact_refresh(
    checkout: pathlib.Path,
    expected_commit: str,
    transition: bytes,
    previous_tree: str,
    expected_tree: str,
) -> None:
    """Undo only our transition, and only while its complete result is present."""

    if current_checkout_tree(checkout, expected_commit) != expected_tree:
        raise OverlayStateError(
            "refusing refresh rollback because the checkout changed after application"
        )
    _run_git_apply(
        transition, checkout=checkout, check_only=True, reverse=True
    )
    _run_git_apply(transition, checkout=checkout, reverse=True)
    if current_checkout_tree(checkout, expected_commit) != previous_tree:
        raise OverlayStateError(
            "overlay refresh rollback did not restore the previously recorded tree"
        )


def _reapply_exact_overlay(
    checkout: pathlib.Path,
    expected_commit: str,
    transition: bytes,
    base_tree: str,
    overlay_tree: str,
) -> None:
    """Reapply an overlay only while the exact pinned base tree is present."""

    if current_checkout_tree(checkout, expected_commit) != base_tree:
        raise OverlayStateError(
            "refusing restore rollback because the checkout changed after restoration"
        )
    _run_git_apply(transition, checkout=checkout, check_only=True)
    _run_git_apply(transition, checkout=checkout)
    if current_checkout_tree(checkout, expected_commit) != overlay_tree:
        raise OverlayStateError(
            "overlay restore rollback did not recover the previously recorded tree"
        )


def apply_overlay_state(
    repository: pathlib.Path,
    checkout: pathlib.Path,
    state_path: pathlib.Path,
    expected_commit: str,
) -> OverlayApplyResult:
    """Atomically apply the initial overlay and publish its bound state."""

    repository = repository.resolve()
    checkout = checkout.resolve()
    state_path = state_path.absolute()
    expected_commit = _require_commit(
        expected_commit, label="expected Chromium commit"
    )
    if state_path.exists() or state_path.is_symlink():
        raise OverlayStateError("initial overlay application requires missing state")

    base_tree = _run_git(
        "rev-parse", f"{expected_commit}^{{tree}}", checkout=checkout
    )
    actual_tree = current_checkout_tree(checkout, expected_commit)
    if actual_tree != base_tree:
        raise OverlayStateError(
            "initial overlay application requires the exact clean pinned tree"
        )
    expected, transition = _compose_expected_overlay(
        repository, checkout, expected_commit
    )
    if not transition:
        raise OverlayStateError("initial overlay application produced no tree delta")
    if state_path.exists() or state_path.is_symlink():
        raise OverlayStateError("overlay state appeared during initial composition")
    if overlay_inputs_fingerprint(repository) != expected.input_fingerprint:
        raise OverlayStateError("overlay inputs changed after initial composition")
    if current_checkout_tree(checkout, expected_commit) != base_tree:
        raise OverlayStateError("Chromium checkout changed before initial application")

    next_state = _new_overlay_state(expected)
    state_published = False
    try:
        _run_git_apply(transition, checkout=checkout, check_only=True)
        _run_git_apply(transition, checkout=checkout)
        if current_checkout_tree(checkout, expected_commit) != expected.tree:
            raise OverlayStateError(
                "initial overlay application did not produce the composed tree"
            )
        if overlay_inputs_fingerprint(repository) != expected.input_fingerprint:
            raise OverlayStateError("overlay inputs changed during initial application")
        _create_overlay_state_atomic(state_path, next_state)
        state_published = True
        verify_overlay_state(repository, checkout, state_path, expected_commit)
    except BaseException as error:
        cleanup_error: BaseException | None = None
        if state_published or state_path.exists() or state_path.is_symlink():
            try:
                _remove_exact_overlay_state(
                    state_path, next_state, expected_commit
                )
            except BaseException as state_error:
                cleanup_error = state_error
        rollback_error: BaseException | None = None
        try:
            failed_tree = current_checkout_tree(checkout, expected_commit)
            if failed_tree == expected.tree:
                _rollback_exact_refresh(
                    checkout,
                    expected_commit,
                    transition,
                    base_tree,
                    expected.tree,
                )
            elif failed_tree != base_tree:
                raise OverlayStateError(
                    "refusing initial-apply rollback because the interrupted "
                    "checkout is neither the exact base nor composed tree"
                )
        except BaseException as recovery_error:
            rollback_error = recovery_error
        if rollback_error is not None:
            detail = cleanup_error or rollback_error
            raise OverlayStateError(
                f"initial overlay application failed ({error}); exact rollback "
                f"also failed: {detail}"
            ) from error
        if cleanup_error is not None:
            raise OverlayStateError(
                f"initial overlay application failed ({error}); checkout rolled "
                f"back but state cleanup failed: {cleanup_error}"
            ) from error
        raise OverlayStateError(
            "initial overlay application failed and its exact tree delta was "
            f"rolled back: {error}"
        ) from error

    return OverlayApplyResult(
        chromium_commit=expected.chromium_commit,
        input_fingerprint=expected.input_fingerprint,
        checkout_delta_fingerprint=expected.delta_fingerprint,
        previous_tree=base_tree,
        actual_tree=expected.tree,
        applied_at=next_state["appliedAt"],
    )


def restore_overlay_state(
    repository: pathlib.Path,
    checkout: pathlib.Path,
    state_path: pathlib.Path,
    expected_commit: str,
) -> OverlayRestoreResult:
    """Restore an exactly verified overlay checkout to its pinned base tree."""

    repository = repository.resolve()
    checkout = checkout.resolve()
    state_path = state_path.absolute()
    expected_commit = _require_commit(
        expected_commit, label="expected Chromium commit"
    )
    previous_state = load_overlay_state(state_path, expected_commit)
    expected, transition = _compose_expected_overlay(
        repository, checkout, expected_commit
    )
    previous_tree = current_checkout_tree(checkout, expected_commit)
    if previous_tree != expected.tree:
        raise OverlayStateError(
            "Chromium checkout does not match the deterministic overlay being restored"
        )
    if previous_state["fingerprint"] != expected.input_fingerprint:
        raise OverlayStateError("overlay inputs do not match the state being restored")
    if previous_state["checkoutDeltaFingerprint"] != expected.delta_fingerprint:
        raise OverlayStateError("overlay tree does not match the state being restored")
    if not transition:
        raise OverlayStateError("overlay restore produced no tree delta")
    base_tree = _run_git(
        "rev-parse", f"{expected_commit}^{{tree}}", checkout=checkout
    )

    _assert_refresh_preconditions(
        repository,
        checkout,
        state_path,
        expected_commit,
        previous_state,
        previous_tree,
        expected.input_fingerprint,
    )
    try:
        _run_git_apply(
            transition, checkout=checkout, check_only=True, reverse=True
        )
        _run_git_apply(transition, checkout=checkout, reverse=True)
        if current_checkout_tree(checkout, expected_commit) != base_tree:
            raise OverlayStateError(
                "overlay restore did not produce the exact pinned base tree"
            )
        if load_overlay_state(state_path, expected_commit) != previous_state:
            raise OverlayStateError("overlay state changed during restoration")
        if overlay_inputs_fingerprint(repository) != expected.input_fingerprint:
            raise OverlayStateError("overlay inputs changed during restoration")
        _remove_exact_overlay_state(state_path, previous_state, expected_commit)
    except BaseException as error:
        # State removal is the commit point and the final operation in the
        # transaction. An asynchronous exception immediately after unlink must
        # not reapply the overlay without recreating its state.
        failed_tree = current_checkout_tree(checkout, expected_commit)
        if (
            failed_tree == base_tree
            and not state_path.exists()
            and not state_path.is_symlink()
        ):
            return OverlayRestoreResult(
                chromium_commit=expected.chromium_commit,
                input_fingerprint=expected.input_fingerprint,
                checkout_delta_fingerprint=expected.delta_fingerprint,
                previous_tree=previous_tree,
                actual_tree=base_tree,
            )
        if failed_tree == base_tree:
            try:
                _reapply_exact_overlay(
                    checkout,
                    expected_commit,
                    transition,
                    base_tree,
                    expected.tree,
                )
            except BaseException as rollback_error:
                raise OverlayStateError(
                    f"overlay restore failed ({error}); exact rollback also failed: "
                    f"{rollback_error}"
                ) from error
        elif failed_tree != expected.tree:
            raise OverlayStateError(
                f"overlay restore failed ({error}); refusing rollback because "
                "the interrupted checkout is neither the exact overlay nor base tree"
            ) from error
        raise OverlayStateError(
            "overlay restore failed and the previously recorded tree was "
            f"reapplied: {error}"
        ) from error

    return OverlayRestoreResult(
        chromium_commit=expected.chromium_commit,
        input_fingerprint=expected.input_fingerprint,
        checkout_delta_fingerprint=expected.delta_fingerprint,
        previous_tree=previous_tree,
        actual_tree=base_tree,
    )


def refresh_overlay_state(
    repository: pathlib.Path,
    checkout: pathlib.Path,
    state_path: pathlib.Path,
    expected_commit: str,
) -> OverlayRefreshResult:
    """Transactionally refresh an exactly recorded applied overlay checkout.

    The cached input fingerprint is deliberately not used as authority because
    repository inputs are expected to have changed. The recorded checkout tree
    fingerprint is: it must bind the complete current non-ignored worktree to
    the previously applied tree before composition or mutation can proceed.
    """

    repository = repository.resolve()
    checkout = checkout.resolve()
    state_path = state_path.absolute()
    expected_commit = _require_commit(
        expected_commit, label="expected Chromium commit"
    )
    previous_state = load_overlay_state(state_path, expected_commit)
    previous_tree = current_checkout_tree(checkout, expected_commit)
    recorded_tree_fingerprint = delta_fingerprint(expected_commit, previous_tree)
    if previous_state["checkoutDeltaFingerprint"] != recorded_tree_fingerprint:
        # A coordinated source update may already have been materialized while
        # its cached state still describes the prior overlay. Reconcile that
        # narrow case only when the complete checkout is byte-for-byte equal to
        # the tree freshly derived from current source inputs. Partial or
        # foreign edits still fail before any mutation.
        expected = derive_expected_overlay(
            repository, checkout, expected_commit
        )
        if previous_tree != expected.tree:
            raise OverlayStateError(
                "Chromium checkout does not match the previously recorded "
                "applied overlay tree or the freshly composed overlay tree; "
                "refusing to refresh foreign or partial edits"
            )
        _assert_refresh_preconditions(
            repository,
            checkout,
            state_path,
            expected_commit,
            previous_state,
            previous_tree,
            expected.input_fingerprint,
        )
        next_state = _new_overlay_state(expected)
        _write_overlay_state_atomic(state_path, next_state)
        verify_overlay_state(
            repository, checkout, state_path, expected_commit
        )
        return OverlayRefreshResult(
            chromium_commit=expected.chromium_commit,
            input_fingerprint=expected.input_fingerprint,
            checkout_delta_fingerprint=expected.delta_fingerprint,
            previous_tree=previous_tree,
            actual_tree=previous_tree,
            applied_at=next_state["appliedAt"],
            checkout_changed=False,
            state_changed=True,
        )

    expected, transition = _compose_expected_overlay(
        repository,
        checkout,
        expected_commit,
        diff_base_tree=previous_tree,
    )
    checkout_changed = expected.tree != previous_tree
    state_changed = (
        previous_state["fingerprint"] != expected.input_fingerprint
        or previous_state["checkoutDeltaFingerprint"]
        != expected.delta_fingerprint
    )
    if not checkout_changed and not state_changed:
        return OverlayRefreshResult(
            chromium_commit=expected.chromium_commit,
            input_fingerprint=expected.input_fingerprint,
            checkout_delta_fingerprint=expected.delta_fingerprint,
            previous_tree=previous_tree,
            actual_tree=previous_tree,
            applied_at=previous_state["appliedAt"],
            checkout_changed=False,
            state_changed=False,
        )

    _assert_refresh_preconditions(
        repository,
        checkout,
        state_path,
        expected_commit,
        previous_state,
        previous_tree,
        expected.input_fingerprint,
    )
    next_state = _new_overlay_state(expected)

    if not checkout_changed:
        _write_overlay_state_atomic(state_path, next_state)
        return OverlayRefreshResult(
            chromium_commit=expected.chromium_commit,
            input_fingerprint=expected.input_fingerprint,
            checkout_delta_fingerprint=expected.delta_fingerprint,
            previous_tree=previous_tree,
            actual_tree=previous_tree,
            applied_at=next_state["appliedAt"],
            checkout_changed=False,
            state_changed=True,
        )

    if not transition:
        raise OverlayStateError(
            "overlay refresh produced different trees but no transition delta"
        )
    _run_git_apply(transition, checkout=checkout, check_only=True)
    _run_git_apply(transition, checkout=checkout)

    try:
        actual_tree = current_checkout_tree(checkout, expected_commit)
        if actual_tree != expected.tree:
            raise OverlayStateError(
                "overlay refresh did not produce the freshly composed tree"
            )
        if load_overlay_state(state_path, expected_commit) != previous_state:
            raise OverlayStateError("overlay state changed during refresh application")
        if overlay_inputs_fingerprint(repository) != expected.input_fingerprint:
            raise OverlayStateError("overlay inputs changed during refresh application")
        _write_overlay_state_atomic(state_path, next_state)
    except (OSError, OverlayStateError) as error:
        try:
            _rollback_exact_refresh(
                checkout,
                expected_commit,
                transition,
                previous_tree,
                expected.tree,
            )
        except OverlayStateError as rollback_error:
            raise OverlayStateError(
                f"overlay refresh failed ({error}); safe rollback also failed: "
                f"{rollback_error}"
            ) from error
        raise OverlayStateError(
            f"overlay refresh failed and its tree delta was rolled back: {error}"
        ) from error

    return OverlayRefreshResult(
        chromium_commit=expected.chromium_commit,
        input_fingerprint=expected.input_fingerprint,
        checkout_delta_fingerprint=expected.delta_fingerprint,
        previous_tree=previous_tree,
        actual_tree=expected.tree,
        applied_at=next_state["appliedAt"],
        checkout_changed=True,
        state_changed=True,
    )


def verify_overlay_state(
    repository: pathlib.Path,
    checkout: pathlib.Path,
    state_path: pathlib.Path,
    expected_commit: str,
) -> VerifiedOverlayState:
    """Verify source truth first, then validate the state as a disposable cache."""

    state = load_overlay_state(state_path, expected_commit)
    expected = derive_expected_overlay(repository, checkout, expected_commit)
    actual_tree = current_checkout_tree(checkout, expected_commit)
    if actual_tree != expected.tree:
        raise OverlayStateError(
            "Chromium checkout does not match the deterministic Ahoi overlay "
            "derived from the pinned commit and current inputs"
        )
    if state["fingerprint"] != expected.input_fingerprint:
        raise OverlayStateError("overlay state fingerprint does not match current inputs")
    if state["checkoutDeltaFingerprint"] != expected.delta_fingerprint:
        raise OverlayStateError(
            "overlay state checkoutDeltaFingerprint does not match the recomputed delta"
        )
    return VerifiedOverlayState(
        chromium_commit=expected.chromium_commit,
        input_fingerprint=expected.input_fingerprint,
        checkout_delta_fingerprint=expected.delta_fingerprint,
        expected_tree=expected.tree,
        actual_tree=actual_tree,
        applied_at=state["appliedAt"],
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    expected_parser = subparsers.add_parser(
        "expected-fingerprint", help="print the freshly composed delta fingerprint"
    )
    verify_parser = subparsers.add_parser(
        "verify", help="verify checkout truth and its cached state"
    )
    refresh_parser = subparsers.add_parser(
        "refresh",
        help="safely refresh an exactly recorded applied overlay checkout",
    )
    apply_parser = subparsers.add_parser(
        "apply", help="atomically perform the initial overlay application"
    )
    restore_parser = subparsers.add_parser(
        "restore", help="restore a verified overlay checkout to its pinned base"
    )
    for command_parser in (
        expected_parser,
        verify_parser,
        refresh_parser,
        apply_parser,
        restore_parser,
    ):
        command_parser.add_argument("--repository", type=pathlib.Path, required=True)
        command_parser.add_argument("--checkout", type=pathlib.Path, required=True)
        command_parser.add_argument("--expected-commit", required=True)
    for state_parser in (
        verify_parser,
        refresh_parser,
        apply_parser,
        restore_parser,
    ):
        state_parser.add_argument("--state", type=pathlib.Path, required=True)
    args = parser.parse_args()

    try:
        if args.command == "expected-fingerprint":
            expected = derive_expected_overlay(
                args.repository, args.checkout, args.expected_commit
            )
            print(expected.delta_fingerprint)
        elif args.command == "verify":
            verified = verify_overlay_state(
                args.repository,
                args.checkout,
                args.state,
                args.expected_commit,
            )
            print(verified.checkout_delta_fingerprint)
        elif args.command == "refresh":
            refreshed = refresh_overlay_state(
                args.repository,
                args.checkout,
                args.state,
                args.expected_commit,
            )
            if refreshed.checkout_changed:
                status = "checkout-refreshed"
            elif refreshed.state_changed:
                status = "state-updated"
            else:
                status = "unchanged"
            print(f"{status} {refreshed.checkout_delta_fingerprint}")
        elif args.command == "apply":
            applied = apply_overlay_state(
                args.repository,
                args.checkout,
                args.state,
                args.expected_commit,
            )
            print(f"checkout-applied {applied.checkout_delta_fingerprint}")
        else:
            restored = restore_overlay_state(
                args.repository,
                args.checkout,
                args.state,
                args.expected_commit,
            )
            print(f"checkout-restored {restored.checkout_delta_fingerprint}")
    except (OSError, OverlayStateError, SystemExit) as error:
        print(f"error: overlay {args.command} failed: {error}", file=os.sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
