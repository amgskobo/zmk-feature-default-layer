from copy import deepcopy
from pathlib import Path
import unittest
from unittest.mock import patch

import yaml

from tools.verify_review_dependencies import selected_projects, verify_remote


class ReviewDependencyContract(unittest.TestCase):
    def setUp(self):
        manifest = (
            Path(__file__).resolve().parents[1]
            / "west/west-dependency/west-dependency.yml"
        )
        self.document = yaml.safe_load(manifest.read_text())

    def test_owned_manifest_selects_complete_pinned_forks(self):
        self.assertEqual(len(selected_projects(self.document)), 2)
        missing = deepcopy(self.document)
        missing["manifest"]["projects"].pop()
        with self.assertRaises(ValueError):
            selected_projects(missing)

    def test_rejects_upstream_moving_refs_and_wrong_branches(self):
        for field, value in [
            ("url", "https://github.com/cormoran/zmk-feature-os-detection"),
            ("revision", "main"),
            ("userdata", {"review-branch": "codex/other-review"}),
        ]:
            with self.subTest(field=field):
                document = deepcopy(self.document)
                document["manifest"]["projects"][1][field] = value
                with self.assertRaises(ValueError):
                    selected_projects(document)

    @patch("tools.verify_review_dependencies.subprocess.check_output")
    def test_checks_the_named_remote_ref(self, read_ref):
        for project in selected_projects(self.document):
            ref = "refs/heads/" + project["userdata"]["review-branch"]
            read_ref.return_value = project["revision"] + "\t" + ref + "\n"
            verify_remote(project)
            self.assertEqual(read_ref.call_args.args[0][-1], ref)
            self.assertEqual(read_ref.call_args.args[0][-2], project["url"])

    @patch("tools.verify_review_dependencies.subprocess.check_output")
    def test_rejects_missing_different_and_unrelated_remote_tips(self, read_ref):
        project = selected_projects(self.document)[1]
        ref = "refs/heads/" + project["userdata"]["review-branch"]
        for response in [
            "",
            "0" * 40 + "\t" + ref,
            project["revision"] + "\trefs/heads/main",
        ]:
            with self.subTest(response=response):
                read_ref.return_value = response
                with self.assertRaises(ValueError):
                    verify_remote(project)
