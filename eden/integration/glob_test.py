#!/usr/bin/env python3
#
# Copyright (c) 2016-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from facebook.eden.ttypes import EdenError
from .lib import testcase


@testcase.eden_repo_test
class GlobTest(testcase.EdenRepoTest):
    def populate_repo(self) -> None:
        self.repo.write_file('hello', 'hola\n')
        self.repo.write_file('adir/file', 'foo!\n')
        self.repo.write_file('bdir/file', 'bar!\n')
        self.repo.symlink('slink', 'hello')
        self.repo.write_file('cdir/subdir/new.txt', 'and improved')
        self.repo.commit('Commit 1.')

    def setUp(self) -> None:
        super().setUp()
        self.client = self.get_thrift_client()
        self.client.open()
        self.addCleanup(self.client.close)

    def test_exact_path_component_match(self) -> None:
        self.assertEqual(['hello'], self.client.glob(self.mount, ['hello']))

    def test_wildcard_path_component_match(self) -> None:
        self.assertEqual(['hello'], self.client.glob(self.mount, ['hel*']))
        self.assertEqual(['adir'], self.client.glob(self.mount, ['ad*']))
        self.assertEqual(
            ['adir/file'], self.client.glob(self.mount, ['a*/file'])
        )

    def test_no_accidental_substring_match(self) -> None:
        self.assertEqual(
            [],
            self.client.glob(self.mount, ['hell']),
            msg='No accidental substring match'
        )

    def test_match_all_files_in_directory(self) -> None:
        self.assertEqual(
            ['adir/file'], self.client.glob(self.mount, ['adir/*'])
        )

    def test_overlapping_globs(self) -> None:
        self.assertCountEqual(
            ['adir/file', 'bdir/file'],
            self.client.glob(self.mount, ['adir/*', '**/file']),
            msg='De-duplicate results from multiple globs'
        )

    def test_recursive_wildcard_prefix(self) -> None:
        self.assertCountEqual(
            ['adir/file', 'bdir/file'],
            self.client.glob(self.mount, ['**/file'])
        )

    def test_recursive_wildcard_suffix(self) -> None:
        self.assertEqual(
            ['adir/file'], self.client.glob(self.mount, ['adir/**'])
        )
        self.assertEqual(
            ['adir/file'], self.client.glob(self.mount, ['adir/**/*'])
        )

    def test_malformed_query(self) -> None:
        with self.assertRaises(EdenError) as ctx:
            self.client.glob(self.mount, ['adir['])
        self.assertIn('unterminated bracket sequence', str(ctx.exception))