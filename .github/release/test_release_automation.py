#!/usr/bin/env python3
"""Focused, no-network tests for the release helper and publication decisions."""

from __future__ import annotations

import json
import os
import re
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, Mapping, Optional

sys.path.insert(0, str(Path(__file__).parent))

from release_automation import (  # noqa: E402
    ApiError,
    GhApi,
    ReleaseAutomationError,
    ReleasePublisher,
    ReleaseTitleError,
    VersionMismatchError,
    cmake_project_version,
    parse_commit_message,
    parse_subject,
    release_marker,
    require_project_version,
)


class ParsingTests(unittest.TestCase):
    def test_normal_commit_does_not_request_release(self) -> None:
        self.assertFalse(parse_subject("fix: improve query bounds").requested)

    def test_exact_release_title_requests_release(self) -> None:
        request = parse_subject("chore(release): v1.2.3")
        self.assertTrue(request.requested)
        self.assertEqual(request.version, "1.2.3")

    def test_multiline_message_uses_only_subject_line(self) -> None:
        request = parse_commit_message(
            "chore(release): v1.2.3\n\nbody with shell-looking text: $(false)"
        )
        self.assertTrue(request.requested)
        self.assertEqual(request.version, "1.2.3")
        self.assertFalse(parse_commit_message("docs: release notes\nchore(release): v9.9.9").requested)

    def test_malformed_release_title_is_rejected(self) -> None:
        for title in (
            "chore(release):",
            "chore(release): 1.2.3",
            "chore(release): v1.2",
            "chore(release) : v1.2.3",
            " chore(release): v1.2.3",
            "chore(release): v1.2.3-rc.1",
        ):
            with self.subTest(title=title):
                with self.assertRaises(ReleaseTitleError):
                    parse_subject(title)

    def test_leading_zeroes_are_rejected(self) -> None:
        for title in (
            "chore(release): v01.2.3",
            "chore(release): v1.02.3",
            "chore(release): v1.2.03",
        ):
            with self.subTest(title=title):
                with self.assertRaises(ReleaseTitleError):
                    parse_subject(title)

    def test_cmake_version_must_match_exactly(self) -> None:
        cmake = "project(qalsh-lib VERSION 1.2.3 LANGUAGES CXX)\n"
        self.assertEqual(cmake_project_version(cmake), "1.2.3")
        self.assertEqual(require_project_version(cmake, "1.2.3"), "1.2.3")
        with self.assertRaises(VersionMismatchError):
            require_project_version(cmake, "1.2.4")


class FakeApi:
    """A fixture-backed API; no GitHub requests or writes are possible."""

    def __init__(
        self,
        *,
        commit: str,
        tag_ref: Optional[Mapping[str, Any]] = None,
        tag_object: Optional[Mapping[str, Any]] = None,
        release: Optional[Mapping[str, Any]] = None,
        full_release_pages: bool = False,
    ) -> None:
        self.commit = commit
        self.tag_ref = dict(tag_ref) if tag_ref is not None else None
        self.tag_object = dict(tag_object) if tag_object is not None else None
        self.release = dict(release) if release is not None else None
        self.full_release_pages = full_release_pages
        self.calls: list[tuple[str, str, Optional[Mapping[str, Any]]]] = []
        self.next_tag_object_sha = "b" * 40

    def request(
        self, method: str, endpoint: str, payload: Optional[Mapping[str, Any]] = None
    ) -> Mapping[str, Any]:
        self.calls.append((method, endpoint, payload))
        if endpoint.endswith("/git/ref/tags/v1.2.3") and method == "GET":
            if self.tag_ref is None:
                raise ApiError(404, "not found")
            return self.tag_ref
        if endpoint.endswith("/git/tags/" + "b" * 40) and method == "GET":
            if self.tag_object is None:
                raise ApiError(404, "not found")
            return self.tag_object
        if endpoint.endswith("/git/tags") and method == "POST":
            return {"sha": self.next_tag_object_sha, "type": "tag"}
        if endpoint.endswith("/git/refs") and method == "POST":
            assert payload is not None
            self.tag_ref = {"object": {"sha": payload["sha"], "type": "tag"}}
            return self.tag_ref
        if endpoint.endswith("/releases/tags/v1.2.3") and method == "GET":
            # GitHub's tag endpoint returns published releases only; drafts
            # are found through the authenticated list endpoint.
            if self.release is not None and self.release.get("draft") is not True:
                return self.release
            raise ApiError(404, "not found")
        if "/releases?per_page=100&page=" in endpoint and method == "GET":
            if self.full_release_pages:
                page = endpoint.rsplit("=", 1)[1]
                return [
                    {"id": index, "tag_name": f"unrelated-{page}-{index}"}
                    for index in range(100)
                ]
            return [self.release] if self.release is not None else []
        if endpoint.endswith("/releases") and method == "POST":
            assert payload is not None
            self.release = {
                "id": 17,
                "tag_name": payload["tag_name"],
                "target_commitish": payload["target_commitish"],
                "body": payload["body"],
                "draft": payload["draft"],
                "prerelease": payload["prerelease"],
            }
            return self.release
        if endpoint.endswith("/releases/17") and method == "PATCH":
            assert self.release is not None
            self.release.update(payload or {})
            return self.release
        raise AssertionError(f"unexpected fake API call: {method} {endpoint}")


class PublicationTests(unittest.TestCase):
    repository = "example/qalsh-lib"
    commit = "a" * 40

    def publisher(self, api: FakeApi) -> ReleasePublisher:
        return ReleasePublisher(api, self.repository)

    def test_missing_tag_and_release_create_annotated_tag_and_publish_notes(self) -> None:
        api = FakeApi(commit=self.commit)
        result = self.publisher(api).publish("1.2.3", self.commit)
        self.assertIn("tag=created", result)
        self.assertIn("release=published-draft", result)

        tag_payload = next(payload for method, endpoint, payload in api.calls if method == "POST" and endpoint.endswith("/git/tags"))
        self.assertEqual(tag_payload["tag"], "v1.2.3")
        self.assertEqual(tag_payload["message"], "chore(release): v1.2.3")
        self.assertEqual(tag_payload["object"], self.commit)
        self.assertEqual(tag_payload["type"], "commit")
        release_payload = next(payload for method, endpoint, payload in api.calls if method == "POST" and endpoint.endswith("/releases"))
        self.assertTrue(release_payload["generate_release_notes"])
        self.assertEqual(release_payload["make_latest"], "legacy")
        self.assertTrue(release_payload["draft"])
        self.assertIn(release_marker("v1.2.3", self.commit), release_payload["body"])
        self.assertEqual(
            [method for method, endpoint, _ in api.calls if endpoint.endswith("/releases/17")],
            ["PATCH"],
        )

    def test_existing_matching_annotated_tag_and_published_release_are_noop(self) -> None:
        api = FakeApi(
            commit=self.commit,
            tag_ref={"object": {"sha": "b" * 40, "type": "tag"}},
            tag_object={"object": {"sha": self.commit, "type": "commit"}},
            release={
                "id": 17,
                "tag_name": "v1.2.3",
                "target_commitish": "main",
                "body": "generated notes",
                "draft": False,
                "prerelease": False,
            },
        )
        result = self.publisher(api).publish("1.2.3", self.commit)
        self.assertIn("tag=reused", result)
        self.assertIn("release=already-published", result)
        self.assertFalse(any(method == "POST" or method == "PATCH" for method, _, _ in api.calls))

    def test_conflicting_tag_is_an_error_and_release_is_not_touched(self) -> None:
        other = "c" * 40
        api = FakeApi(
            commit=self.commit,
            tag_ref={"object": {"sha": other, "type": "commit"}},
        )
        with self.assertRaises(ReleaseAutomationError):
            self.publisher(api).publish("1.2.3", self.commit)
        self.assertFalse(any("releases" in endpoint for _, endpoint, _ in api.calls))

    def test_external_draft_is_not_adopted(self) -> None:
        api = FakeApi(
            commit=self.commit,
            tag_ref={"object": {"sha": self.commit, "type": "commit"}},
            release={
                "id": 17,
                "tag_name": "v1.2.3",
                "target_commitish": self.commit,
                "body": "a human draft without the automation marker",
                "draft": True,
                "prerelease": False,
            },
        )
        with self.assertRaises(ReleaseAutomationError):
            self.publisher(api).publish("1.2.3", self.commit)
        self.assertFalse(any(method == "PATCH" for method, _, _ in api.calls))

    def test_owned_draft_is_published_on_retry(self) -> None:
        marker = release_marker("v1.2.3", self.commit)
        api = FakeApi(
            commit=self.commit,
            tag_ref={"object": {"sha": self.commit, "type": "commit"}},
            release={
                "id": 17,
                "tag_name": "v1.2.3",
                "target_commitish": self.commit,
                "body": marker + "\n\nGenerated notes",
                "draft": True,
                "prerelease": False,
            },
        )
        result = self.publisher(api).publish("1.2.3", self.commit)
        self.assertIn("tag=reused", result)
        self.assertIn("release=published-draft", result)
        self.assertEqual(
            [payload for method, endpoint, payload in api.calls if method == "PATCH"],
            [{"draft": False, "make_latest": "legacy"}],
        )

    def test_full_capped_release_list_fails_closed_before_any_release_write(self) -> None:
        api = FakeApi(
            commit=self.commit,
            tag_ref={"object": {"sha": self.commit, "type": "commit"}},
            full_release_pages=True,
        )
        with self.assertRaisesRegex(ReleaseAutomationError, "1000-entry safety bound"):
            self.publisher(api).publish("1.2.3", self.commit)
        self.assertFalse(
            any(
                method in ("POST", "PATCH") and "/releases" in endpoint
                for method, endpoint, _ in api.calls
            )
        )


class WorkflowConfigurationTests(unittest.TestCase):
    workflow_path = Path(__file__).parents[1] / "workflows" / "release.yml"

    def test_publication_concurrency_is_per_version_not_workflow_global(self) -> None:
        workflow = self.workflow_path.read_text(encoding="utf-8")
        before_jobs = workflow.split("\njobs:\n", 1)[0]
        publish = workflow.split("\n  publish:\n", 1)[1]
        self.assertNotIn("\nconcurrency:", before_jobs)
        self.assertNotIn("github.ref_name", workflow)
        self.assertRegex(
            publish,
            r"concurrency:\n\s+group: qalsh-release-\$\{\{ github\.repository \}\}-\$\{\{ needs\.verify\.outputs\.release_version \}\}",
        )
        self.assertIn("cancel-in-progress: false", publish)
        self.assertNotIn("contents: write", workflow.split("\n  publish:\n", 1)[0])
        self.assertIn("contents: write", publish)


class GhApiSubprocessTests(unittest.TestCase):
    def test_gh_api_uses_argument_vector_and_stdin_json(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            directory_path = Path(directory)
            arguments = directory_path / "arguments.json"
            body = directory_path / "body.json"
            fake_gh = directory_path / "gh"
            fake_gh.write_text(
                "#!/usr/bin/env python3\n"
                "import json, os, sys\n"
                f"json.dump(sys.argv[1:], open({str(arguments)!r}, 'w'))\n"
                f"open({str(body)!r}, 'w').write(sys.stdin.read())\n"
                "print(json.dumps({'ok': True}))\n",
                encoding="utf-8",
            )
            fake_gh.chmod(fake_gh.stat().st_mode | stat.S_IXUSR)
            environment = dict(os.environ)
            environment["PATH"] = f"{directory}{os.pathsep}{environment['PATH']}"
            # GhApi uses subprocess.run directly, so replace PATH for this
            # child process rather than configuring a network-capable client.
            old_path = os.environ.get("PATH")
            os.environ["PATH"] = environment["PATH"]
            try:
                response = GhApi().request(
                    "POST",
                    "/repos/example/qalsh-lib/git/tags",
                    {"tag": "v1.2.3", "message": "safe"},
                )
            finally:
                if old_path is None:
                    os.environ.pop("PATH", None)
                else:
                    os.environ["PATH"] = old_path
            self.assertEqual(response, {"ok": True})
            argv = json.loads(arguments.read_text(encoding="utf-8"))
            self.assertEqual(argv[:3], ["api", "/repos/example/qalsh-lib/git/tags", "--method"])
            self.assertIn("POST", argv)
            self.assertEqual(json.loads(body.read_text(encoding="utf-8")), {"tag": "v1.2.3", "message": "safe"})


if __name__ == "__main__":
    unittest.main()
