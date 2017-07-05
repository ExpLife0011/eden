/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/experimental/TestUtil.h>
#include <folly/experimental/logging/Init.h>
#include <folly/experimental/logging/xlog.h>
#include <folly/init/Init.h>
#include <folly/test/TestUtils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "eden/fs/model/Blob.h"
#include "eden/fs/model/Hash.h"
#include "eden/fs/model/Tree.h"
#include "eden/fs/store/LocalStore.h"
#include "eden/fs/store/hg/HgImporter.h"
#include "eden/fs/testharness/HgRepo.h"
#include "eden/fs/testharness/TestUtil.h"
#include "eden/fs/utils/PathFuncs.h"
#include "eden/fs/utils/test/TestChecks.h"

DEFINE_string(logging, "", "folly::logging configuration");

using namespace facebook::eden;
using folly::StringPiece;
using folly::test::TemporaryDirectory;
using std::vector;
using testing::ElementsAre;

namespace {
vector<PathComponent> getTreeEntryNames(const Tree* tree) {
  vector<PathComponent> results;
  for (const auto& entry : tree->getTreeEntries()) {
    results.push_back(entry.getName());
  }
  return results;
}
}

class HgImportTest : public ::testing::Test {
 public:
  HgImportTest() {
    // Create the test repository
    repo_.hgInit();
  }

 protected:
  TemporaryDirectory testDir_{"eden_test"};
  AbsolutePath testPath_{testDir_.path().string()};
  HgRepo repo_{testPath_ + PathComponentPiece{"repo"}};
  LocalStore localStore_{testPath_ + PathComponentPiece{"store"}};
};

TEST_F(HgImportTest, import) {
  // Set up the repository
  repo_.mkdir("foo");
  StringPiece barData = "this is a test file\n";
  repo_.writeFile("foo/bar.txt", barData);
  StringPiece testData = "testing\n1234\ntesting\n";
  repo_.writeFile("foo/test.txt", testData);
  repo_.mkdir("src");
  repo_.mkdir("src/eden");
  StringPiece somelinkData = "this is the link contents";
  repo_.symlink(somelinkData, RelativePathPiece{"src/somelink"});
  StringPiece mainData = "print('hello world\\n')\n";
  repo_.writeFile("src/eden/main.py", mainData, 0755);
  repo_.hg("add");
  auto commit1 = repo_.commit("Initial commit");

  // Import the root tree
  HgImporter importer(repo_.path(), &localStore_);
  auto rootTreeHash = importer.importManifest(commit1.toString());
  auto rootTree = localStore_.getTree(rootTreeHash);
  EXPECT_EQ(rootTreeHash, rootTree->getHash());
  EXPECT_EQ(rootTreeHash, rootTree->getHash());
  ASSERT_THAT(
      getTreeEntryNames(rootTree.get()),
      ElementsAre(PathComponent{"foo"}, PathComponent{"src"}));

  // Import the "foo" tree
  auto fooEntry = rootTree->getEntryAt(PathComponentPiece{"foo"});
  ASSERT_EQ(FileType::DIRECTORY, fooEntry.getFileType());
  EXPECT_EQ(0b111, fooEntry.getOwnerPermissions());
  auto fooTree = localStore_.getTree(fooEntry.getHash());
  ASSERT_THAT(
      getTreeEntryNames(fooTree.get()),
      ElementsAre(PathComponent{"bar.txt"}, PathComponent{"test.txt"}));

  auto barEntry = fooTree->getEntryAt(PathComponentPiece{"bar.txt"});
  ASSERT_EQ(FileType::REGULAR_FILE, barEntry.getFileType());
  EXPECT_EQ(0b110, barEntry.getOwnerPermissions());
  auto testEntry = fooTree->getEntryAt(PathComponentPiece{"test.txt"});
  ASSERT_EQ(FileType::REGULAR_FILE, testEntry.getFileType());
  EXPECT_EQ(0b110, testEntry.getOwnerPermissions());

  // The blobs should not have been imported yet, though
  EXPECT_FALSE(localStore_.getBlob(barEntry.getHash()));
  EXPECT_FALSE(localStore_.getBlob(testEntry.getHash()));

  // Import the "src" tree
  auto srcEntry = rootTree->getEntryAt(PathComponentPiece{"src"});
  ASSERT_EQ(FileType::DIRECTORY, srcEntry.getFileType());
  EXPECT_EQ(0b111, srcEntry.getOwnerPermissions());
  auto srcTree = localStore_.getTree(srcEntry.getHash());
  ASSERT_THAT(
      getTreeEntryNames(srcTree.get()),
      ElementsAre(PathComponent{"eden"}, PathComponent{"somelink"}));

  auto somelinkEntry = srcTree->getEntryAt(PathComponentPiece{"somelink"});
  ASSERT_EQ(FileType::SYMLINK, somelinkEntry.getFileType());
  EXPECT_EQ(0b111, somelinkEntry.getOwnerPermissions());

  // Import the "src/eden" tree
  auto edenEntry = srcTree->getEntryAt(PathComponentPiece{"eden"});
  ASSERT_EQ(FileType::DIRECTORY, edenEntry.getFileType());
  EXPECT_EQ(0b111, edenEntry.getOwnerPermissions());
  auto edenTree = localStore_.getTree(edenEntry.getHash());
  ASSERT_THAT(
      getTreeEntryNames(edenTree.get()), ElementsAre(PathComponent{"main.py"}));

  auto mainEntry = edenTree->getEntryAt(PathComponentPiece{"main.py"});
  ASSERT_EQ(FileType::REGULAR_FILE, mainEntry.getFileType());
  EXPECT_EQ(0b111, mainEntry.getOwnerPermissions());

  // Import and check the blobs
  auto barBuf = importer.importFileContents(barEntry.getHash());
  EXPECT_EQ(barData, StringPiece{barBuf.coalesce()});

  auto testBuf = importer.importFileContents(testEntry.getHash());
  EXPECT_EQ(testData, StringPiece{testBuf.coalesce()});

  auto mainBuf = importer.importFileContents(mainEntry.getHash());
  EXPECT_EQ(mainData, StringPiece{mainBuf.coalesce()});

  auto somelinkBuf = importer.importFileContents(somelinkEntry.getHash());
  EXPECT_EQ(somelinkData, StringPiece{somelinkBuf.coalesce()});

  // Test importing objects that do not exist
  Hash noSuchHash = makeTestHash("123");
  EXPECT_THROW_RE(
      importer.importManifest(noSuchHash.toString()),
      std::exception,
      "unknown revision");
  EXPECT_THROW_RE(
      importer.importFileContents(noSuchHash),
      std::exception,
      "value not present in store");

  // Test trying to import manifests using blob hashes, and vice-versa
  EXPECT_THROW_RE(
      importer.importManifest(barEntry.getHash().toString()),
      std::exception,
      "unknown revision");
  EXPECT_THROW_RE(
      importer.importFileContents(commit1),
      std::exception,
      "value not present in store");
}

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  folly::init(&argc, &argv);
  folly::initLoggingGlogStyle(FLAGS_logging, folly::LogLevel::INFO);

  return RUN_ALL_TESTS();
}