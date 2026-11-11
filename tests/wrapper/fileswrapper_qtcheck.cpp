#include "testkit.h"
#include "wrapper/files/fileswrap/fileswrapper.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

TEST(qtfiles, paths_crumbs_and_parents_work_on_real_folders)
{
    QTemporaryDir dir(QStringLiteral("/tmp/chaosmaker-files-XXXXXX"));
    CHECK(dir.isValid());
    CHECK(QDir(dir.path()).mkpath("Songs/Live"));
    { QFile f(dir.path() + "/Songs/take.wav"); CHECK(f.open(QIODevice::WriteOnly)); }

    FilesWrapper files;
    const QUrl songs = files.toUrl(dir.path() + "/Songs/");
    CHECK(files.toPath(songs) == dir.path() + "/Songs");                      // cleaned
    CHECK(files.toPath(QUrl()).isEmpty() && files.toUrl(QString()).isEmpty());
    CHECK(files.child(songs, "Live") == QUrl::fromLocalFile(dir.path() + "/Songs/Live"));
    CHECK(files.parentOf(files.child(songs, "Live")) == QUrl::fromLocalFile(dir.path() + "/Songs"));
    CHECK(files.isRoot(files.toUrl("/")) && !files.isRoot(songs));
    CHECK(files.parentOf(files.toUrl("/")) == files.toUrl("/"));               // a root stays

    const auto crumbs = files.crumbs(files.child(songs, "Live"));
    CHECK(crumbs.size() >= 4);
    CHECK(crumbs.first().toMap()["name"].toString() == "/");
    CHECK(crumbs.last().toMap()["name"].toString() == "Live");
    CHECK(crumbs[crumbs.size() - 2].toMap()["url"].toUrl() == QUrl::fromLocalFile(dir.path() + "/Songs"));

    const QUrl take = files.child(songs, "take.wav");
    CHECK(files.exists(take) && !files.isFolder(take) && files.isFolder(songs));
    CHECK(!files.exists(files.child(songs, "none.wav")) && !files.exists(QUrl()));
    CHECK(files.folderOf(take) == QUrl::fromLocalFile(dir.path() + "/Songs"));
    CHECK(files.folderOf(songs) == QUrl::fromLocalFile(dir.path() + "/Songs"));
    CHECK(files.folderOf(files.child(songs, "new.wav")) == QUrl::fromLocalFile(dir.path() + "/Songs"));  // not there yet
    CHECK(files.folderOf(files.toUrl(dir.path() + "/gone/x.wav")).isEmpty());
    CHECK(files.fileName(take) == "take.wav");
    CHECK(!files.homeFolder().isEmpty());
}

TEST(qtfiles, name_filters_split_into_patterns_and_match_ignoring_case)
{
    FilesWrapper files;
    CHECK(files.filterPatterns("Audio files (*.wav *.WAV *.mp3)") == QStringList({"*.wav", "*.WAV", "*.mp3"}));
    CHECK(files.filterPatterns("*.chaos") == QStringList({"*.chaos"}));
    CHECK(files.filterPatterns("All files (*)") == QStringList({"*"}));
    CHECK(files.filterLabel("ChaosMaker projects (*.chaos)") == "ChaosMaker projects");
    CHECK(files.filterLabel("*.chaos") == "*.chaos");
    CHECK(files.matchesFilter("Song.MP3", {"*.wav", "*.mp3"}));
    CHECK(!files.matchesFilter("Song.mp3x", {"*.mp3"}));
    CHECK(files.matchesFilter("anything", {"*"}));
    CHECK(files.withSuffix("mix", "wav") == "mix.wav");
    CHECK(files.withSuffix("mix.WAV", "wav") == "mix.WAV");
    CHECK(files.withSuffix("mix", ".chaos") == "mix.chaos");
    CHECK(files.withSuffix("mix", "") == "mix" && files.withSuffix("", "wav").isEmpty());
}
