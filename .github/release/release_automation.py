#!/usr/bin/env python3
"""Small, dependency-free helpers for the commit-title release workflow.

The workflow uses this module for two deliberately separate jobs:

* ``inspect`` reads the checked-out event commit and validates a release title
  and the CMake project version.
* ``publish`` uses the GitHub CLI through :class:`GhApi` to make an annotated
  tag and a generated-notes release repeat-safe.

All subprocesses use argument vectors.  In particular, a commit message is
never interpolated into a shell command or a GitHub expression.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Optional, Protocol, Sequence


_COMPONENT = r"(?:0|[1-9][0-9]*)"
_VERSION_RE = re.compile(rf"({_COMPONENT})\.({_COMPONENT})\.({_COMPONENT})\Z")
_TITLE_RE = re.compile(rf"chore\(release\): v({_COMPONENT})\.({_COMPONENT})\.({_COMPONENT})\Z")
# Treat common spacing/punctuation variants as release-looking too.  A typo
# must fail loudly instead of silently becoming an ordinary CI-only commit.
_RELEASE_LOOKING_RE = re.compile(r"^\s*chore\(release\)\s*:")
_SHA_RE = re.compile(r"[0-9a-fA-F]{40}\Z")
_PROJECT_COMMAND_RE = re.compile(r"\bproject\s*\((.*?)\)", re.IGNORECASE | re.DOTALL)
_PROJECT_VERSION_RE = re.compile(r"\A[0-9]+(?:\.[0-9]+){0,3}\Z")

RELEASE_MESSAGE = "chore(release): {tag}"
RELEASE_LIST_PAGE_SIZE = 100
RELEASE_LIST_PAGE_LIMIT = 10
LATEST_RELEASE_POLICY = "legacy"


class ReleaseAutomationError(RuntimeError):
    """An expected, actionable release-automation failure."""


class ReleaseTitleError(ReleaseAutomationError):
    """The commit asks for a release but does not use the exact title form."""


class VersionMismatchError(ReleaseAutomationError):
    """The release title and declared CMake project version differ."""


@dataclass(frozen=True)
class ReleaseRequest:
    """The safe result of classifying one commit message."""

    requested: bool
    version: Optional[str] = None


def _first_message_line(message: str) -> str:
    """Return Git's commit subject line without inspecting the commit body."""

    # ``git show --format=%B`` includes the complete message.  A subject is
    # the first line; retaining no body text also keeps output channels safe.
    return message.split("\n", 1)[0].rstrip("\r")


def parse_subject(subject: str) -> ReleaseRequest:
    """Classify an already-separated, single-line commit subject.

    Only ``chore(release): vX.Y.Z`` with stable SemVer numeric components and
    no leading zeroes requests publication.  Any title beginning with the
    release convention is considered a release attempt and is rejected when
    malformed.  Call :func:`parse_commit_message` for a complete message.
    """

    if "\n" in subject or "\r" in subject:
        raise ReleaseTitleError("commit subject must be a single line")

    match = _TITLE_RE.fullmatch(subject)
    if match:
        return ReleaseRequest(True, ".".join(match.groups()))

    if _RELEASE_LOOKING_RE.match(subject):
        raise ReleaseTitleError(
            "malformed release title; expected chore(release): vMAJOR.MINOR.PATCH"
        )
    return ReleaseRequest(False)


def parse_commit_message(message: str) -> ReleaseRequest:
    """Classify a complete commit message using its first line as subject."""

    return parse_subject(_first_message_line(message))


# This alias makes the intended seam discoverable to callers/tests that have
# already obtained a subject through ``git log --format=%s``.
parse_commit_subject = parse_subject


def stable_version(version: str) -> bool:
    """Return whether *version* is the supported, non-prerelease SemVer form."""

    return _VERSION_RE.fullmatch(version) is not None


def release_tag(version: str) -> str:
    """Convert a validated version to its release tag."""

    if not stable_version(version):
        raise ReleaseAutomationError("release version is not stable numeric SemVer")
    return f"v{version}"


def cmake_project_version(cmake_text: str) -> Optional[str]:
    """Read the ``qalsh-lib`` project's declared CMake version.

    CMake project declarations may span lines.  Comments are ignored for this
    small, auditable parser; the repository has no generated CMake source that
    needs a full language parser here.
    """

    uncommented = re.sub(r"#[^\n]*", "", cmake_text)
    for command in _PROJECT_COMMAND_RE.finditer(uncommented):
        tokens = command.group(1).split()
        if not tokens or tokens[0].lower() != "qalsh-lib":
            continue
        lowered = [token.lower() for token in tokens]
        try:
            version_index = lowered.index("version")
        except ValueError:
            return None
        if version_index + 1 >= len(tokens):
            return None
        candidate = tokens[version_index + 1]
        if _PROJECT_VERSION_RE.fullmatch(candidate):
            return candidate
        return None
    return None


def require_project_version(cmake_text: str, requested_version: str) -> str:
    """Require an exact textual match between CMake and a requested version."""

    declared = cmake_project_version(cmake_text)
    if declared is None:
        raise VersionMismatchError("CMakeLists.txt has no readable qalsh-lib project version")
    if declared != requested_version:
        raise VersionMismatchError(
            f"CMake project version {declared} does not match requested release {requested_version}"
        )
    return declared


def _valid_sha(value: str) -> str:
    if _SHA_RE.fullmatch(value) is None:
        raise ReleaseAutomationError("GitHub commit SHA must be exactly 40 hexadecimal characters")
    return value.lower()


def _write_outputs(values: Mapping[str, str]) -> None:
    """Write fixed, validated values to GitHub's output file or stdout."""

    output_path = os.environ.get("GITHUB_OUTPUT")
    lines = [f"{key}={value}\n" for key, value in values.items()]
    if output_path:
        with open(output_path, "a", encoding="utf-8") as output:
            output.writelines(lines)
    else:
        sys.stdout.writelines(lines)


def _git_commit_message(commit_sha: str) -> str:
    commit_sha = _valid_sha(commit_sha)
    command = ["git", "show", "--no-patch", "--format=%B", commit_sha]
    completed = subprocess.run(command, check=False, text=True, capture_output=True)
    if completed.returncode != 0:
        raise ReleaseAutomationError("could not read the checked-out event commit")
    return completed.stdout


def inspect_commit(commit_sha: str, cmake_path: Path) -> ReleaseRequest:
    """Classify an exact checked-out commit and check its CMake version."""

    commit_sha = _valid_sha(commit_sha)
    request = parse_commit_message(_git_commit_message(commit_sha))
    if request.requested:
        require_project_version(cmake_path.read_text(encoding="utf-8"), request.version or "")
    return request


class ApiClient(Protocol):
    """The small REST seam used by :class:`ReleasePublisher` and its tests."""

    def request(
        self, method: str, endpoint: str, payload: Optional[Mapping[str, Any]] = None
    ) -> Any: ...


class ApiError(ReleaseAutomationError):
    """A GitHub API failure, retaining its HTTP status when available."""

    def __init__(self, status: Optional[int], message: str):
        self.status = status
        super().__init__(message)


class GhApi:
    """GitHub REST client backed by the runner-provided ``gh api`` command."""

    def request(
        self, method: str, endpoint: str, payload: Optional[Mapping[str, Any]] = None
    ) -> Any:
        command = [
            "gh",
            "api",
            endpoint,
            "--method",
            method.upper(),
            "--header",
            "Accept: application/vnd.github+json",
            "--header",
            "X-GitHub-Api-Version: 2022-11-28",
        ]
        request_body: Optional[str] = None
        if payload is not None:
            command.extend(["--input", "-"])
            request_body = json.dumps(payload, separators=(",", ":"))
        completed = subprocess.run(
            command,
            check=False,
            input=request_body,
            text=True,
            capture_output=True,
        )
        if completed.returncode != 0:
            status_match = re.search(r"HTTP\s+(\d{3})", completed.stderr, re.IGNORECASE)
            status = int(status_match.group(1)) if status_match else None
            detail = completed.stderr.strip() or "gh api failed"
            raise ApiError(status, detail)
        if not completed.stdout.strip():
            return {}
        try:
            value = json.loads(completed.stdout)
        except json.JSONDecodeError as error:
            raise ApiError(None, "gh api returned invalid JSON") from error
        return value


def _repo_is_safe(repo: str) -> bool:
    return re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo) is not None


def release_marker(tag: str, commit_sha: str) -> str:
    """Marker used only to identify a draft made by this operation."""

    return f"<!-- qalsh-release-automation:tag={tag}:commit={commit_sha.lower()} -->"


class ReleasePublisher:
    """Ensure one version's tag and generated-notes release exists safely."""

    def __init__(self, api: ApiClient, repository: str):
        if not _repo_is_safe(repository):
            raise ReleaseAutomationError("repository must be owner/name")
        self.api = api
        self.repository = repository

    def _endpoint(self, suffix: str) -> str:
        return f"/repos/{self.repository}/{suffix}"

    def _tag_ref_endpoint(self, tag: str) -> str:
        return self._endpoint(f"git/ref/tags/{tag}")

    def _get_tag_ref(self, tag: str) -> Mapping[str, Any]:
        ref = self.api.request("GET", self._tag_ref_endpoint(tag))
        if not isinstance(ref, Mapping):
            raise ReleaseAutomationError("GitHub returned an invalid tag reference")
        return ref

    def _tag_target(self, ref: Mapping[str, Any], tag: str) -> str:
        """Resolve lightweight or annotated refs to their final commit."""

        current: Any = ref.get("object")
        for _ in range(8):
            if not isinstance(current, Mapping):
                break
            object_sha = current.get("sha")
            object_type = current.get("type")
            if not isinstance(object_sha, str) or _SHA_RE.fullmatch(object_sha) is None:
                break
            if object_type == "commit":
                return object_sha.lower()
            if object_type != "tag":
                break
            tag_object = self.api.request(
                "GET", self._endpoint(f"git/tags/{object_sha.lower()}")
            )
            if not isinstance(tag_object, Mapping):
                raise ReleaseAutomationError("GitHub returned an invalid annotated tag object")
            current = tag_object.get("object")
        raise ReleaseAutomationError(f"tag {tag} does not resolve to a commit")

    def _check_tag_target(self, tag: str, ref: Mapping[str, Any], commit_sha: str) -> str:
        target = self._tag_target(ref, tag)
        if target != commit_sha:
            raise ReleaseAutomationError(
                f"tag {tag} already resolves to a different commit; refusing to overwrite it"
            )
        return "reused"

    def ensure_tag(self, tag: str, commit_sha: str) -> str:
        """Create an annotated tag or safely reuse a matching existing ref."""

        try:
            existing = self._get_tag_ref(tag)
        except ApiError as error:
            if error.status != 404:
                raise
            existing = None

        if existing is not None:
            return self._check_tag_target(tag, existing, commit_sha)

        tag_object = {
            "tag": tag,
            "message": RELEASE_MESSAGE.format(tag=tag),
            # GitHub's create-tag-object endpoint takes the target SHA as a
            # string; the returned tag-object SHA is used for the ref below.
            "object": commit_sha,
            "type": "commit",
        }
        try:
            created_object = self.api.request(
                "POST", self._endpoint("git/tags"), tag_object
            )
            if not isinstance(created_object, Mapping):
                raise ReleaseAutomationError("GitHub returned an invalid annotated tag object")
            object_sha = created_object.get("sha")
            if not isinstance(object_sha, str) or _SHA_RE.fullmatch(object_sha) is None:
                raise ReleaseAutomationError("GitHub did not return the annotated tag object SHA")
            self.api.request(
                "POST",
                self._endpoint("git/refs"),
                {"ref": f"refs/tags/{tag}", "sha": object_sha.lower()},
            )
            return "created"
        except ApiError as error:
            # Another serialized/retried invocation may have won the ref race.
            # Re-read and verify; never replace the winner.
            if error.status not in (409, 422):
                raise
            raced = self._get_tag_ref(tag)
            return self._check_tag_target(tag, raced, commit_sha)

    def _get_release(self, tag: str) -> Optional[Mapping[str, Any]]:
        try:
            # This endpoint is the cheapest path for a published release.
            # GitHub deliberately omits drafts from a tag lookup, however, so
            # fall through to the authenticated release list below.
            published = self.api.request(
                "GET", self._endpoint(f"releases/tags/{tag}")
            )
            if not isinstance(published, Mapping):
                raise ReleaseAutomationError("GitHub returned an invalid release object")
            return published
        except ApiError as error:
            if error.status != 404:
                raise

        # Drafts are visible to the write-capable workflow token through the
        # list endpoint.  Paginate explicitly so an old external draft cannot
        # be mistaken for a missing release.
        for page in range(1, RELEASE_LIST_PAGE_LIMIT + 1):
            releases = self.api.request(
                "GET",
                self._endpoint(
                    f"releases?per_page={RELEASE_LIST_PAGE_SIZE}&page={page}"
                ),
            )
            if not isinstance(releases, list):
                raise ReleaseAutomationError("GitHub returned an invalid release list")
            for release in releases:
                if isinstance(release, Mapping) and release.get("tag_name") == tag:
                    return release
            if len(releases) < RELEASE_LIST_PAGE_SIZE:
                return None

        # A full final page means the bounded search did not establish that
        # this tag is absent.  Fail closed before any release write so an old
        # draft cannot be mistaken for a missing release.
        raise ReleaseAutomationError(
            f"release list reached the {RELEASE_LIST_PAGE_LIMIT * RELEASE_LIST_PAGE_SIZE}-entry "
            f"safety bound while looking for {tag}; refusing to create a release"
        )

    @staticmethod
    def _release_id(release: Mapping[str, Any]) -> int:
        value = release.get("id")
        if isinstance(value, bool):
            raise ReleaseAutomationError("GitHub release has an invalid ID")
        try:
            release_id = int(value)  # type: ignore[arg-type]
        except (TypeError, ValueError) as error:
            raise ReleaseAutomationError("GitHub release has an invalid ID") from error
        if release_id <= 0:
            raise ReleaseAutomationError("GitHub release has an invalid ID")
        return release_id

    def _owned_draft(
        self, release: Mapping[str, Any], tag: str, commit_sha: str, marker: str
    ) -> bool:
        body = release.get("body")
        target = release.get("target_commitish")
        return (
            marker in body if isinstance(body, str) else False
        ) and isinstance(target, str) and target.lower() == commit_sha

    def _publish_owned_draft(
        self, release: Mapping[str, Any], tag: str, commit_sha: str, marker: str
    ) -> str:
        if release.get("prerelease") is True:
            raise ReleaseAutomationError(
                f"draft release {tag} is marked prerelease; refusing to modify it"
            )
        if not self._owned_draft(release, tag, commit_sha, marker):
            raise ReleaseAutomationError(
                f"draft release {tag} is not marked as created by this operation; refusing to modify it"
            )
        release_id = self._release_id(release)
        published = self.api.request(
            "PATCH",
            self._endpoint(f"releases/{release_id}"),
            {"draft": False, "make_latest": LATEST_RELEASE_POLICY},
        )
        if not isinstance(published, Mapping):
            raise ReleaseAutomationError("GitHub returned an invalid published release")
        if published.get("draft") is True or published.get("prerelease") is True:
            raise ReleaseAutomationError(f"GitHub did not publish release {tag} as stable")
        return "published-draft"

    def _handle_existing(
        self, release: Mapping[str, Any], tag: str, commit_sha: str, marker: str
    ) -> str:
        if not isinstance(release, Mapping):
            raise ReleaseAutomationError("GitHub returned an invalid release object")
        if release.get("tag_name") != tag:
            raise ReleaseAutomationError("GitHub returned a release for the wrong tag")
        if release.get("draft") is True:
            return self._publish_owned_draft(release, tag, commit_sha, marker)
        if release.get("prerelease") is True:
            raise ReleaseAutomationError(
                f"stable tag {tag} already has a prerelease; refusing to overwrite it"
            )
        # A published release is immutable from this workflow's perspective.
        return "already-published"

    def publish(self, version: str, commit_sha: str) -> str:
        """Publish a version, returning a machine-readable action summary."""

        tag = release_tag(version)
        commit_sha = _valid_sha(commit_sha)
        marker = release_marker(tag, commit_sha)
        tag_action = self.ensure_tag(tag, commit_sha)

        release = self._get_release(tag)
        if release is not None:
            release_action = self._handle_existing(release, tag, commit_sha, marker)
            return f"tag={tag_action} release={release_action} tag_name={tag}"

        payload = {
            "tag_name": tag,
            "target_commitish": commit_sha,
            "name": tag,
            "body": marker,
            "draft": True,
            "prerelease": False,
            "generate_release_notes": True,
            "make_latest": LATEST_RELEASE_POLICY,
        }
        try:
            created = self.api.request(
                "POST", self._endpoint("releases"), payload
            )
        except ApiError as error:
            # A retry can race an already-created draft.  Re-read it and apply
            # the same ownership checks rather than creating or editing another.
            if error.status not in (409, 422):
                raise
            raced = self._get_release(tag)
            if raced is None:
                raise
            release_action = self._handle_existing(raced, tag, commit_sha, marker)
            return f"tag={tag_action} release={release_action} tag_name={tag}"

        release_action = self._handle_existing(created, tag, commit_sha, marker)
        return f"tag={tag_action} release={release_action} tag_name={tag}"


def _inspect_command(args: argparse.Namespace) -> int:
    request = inspect_commit(args.commit, Path(args.cmake))
    values = {
        "release_requested": "true" if request.requested else "false",
        "release_version": request.version or "",
        "tested_commit": _valid_sha(args.commit),
    }
    _write_outputs(values)
    return 0


def _publish_command(args: argparse.Namespace) -> int:
    repository = args.repository or os.environ.get("GITHUB_REPOSITORY", "")
    if not os.environ.get("GH_TOKEN") and not os.environ.get("GITHUB_TOKEN"):
        raise ReleaseAutomationError("GH_TOKEN/GITHUB_TOKEN is required for publication")
    result = ReleasePublisher(GhApi(), repository).publish(args.version, args.commit)
    print(result)
    return 0


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    inspect = commands.add_parser("inspect", help="classify the exact event commit")
    inspect.add_argument("--commit", default=os.environ.get("GITHUB_SHA"), required=False)
    inspect.add_argument("--cmake", default="CMakeLists.txt")
    inspect.set_defaults(handler=_inspect_command)

    classify = commands.add_parser("classify", help="classify a complete commit message")
    classify.add_argument("--message-file", type=Path, required=True)

    publish = commands.add_parser("publish", help="publish through gh api")
    publish.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY"))
    publish.add_argument("--version", required=True)
    publish.add_argument("--commit", default=os.environ.get("GITHUB_SHA"), required=True)
    publish.set_defaults(handler=_publish_command)
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = _parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "classify":
            request = parse_commit_message(args.message_file.read_text(encoding="utf-8"))
            print(
                json.dumps(
                    {"release_requested": request.requested, "version": request.version},
                    sort_keys=True,
                )
            )
            return 0
        if not args.commit:
            parser.error("--commit or GITHUB_SHA is required")
        return args.handler(args)
    except (OSError, ReleaseAutomationError) as error:
        print(f"release automation error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
