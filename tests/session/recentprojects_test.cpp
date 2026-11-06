#include "testkit.h"
#include "session/recentprojects.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

TEST(recentprojects, newest_first_without_duplicates)
{
    RecentProjects recent;
    CHECK(recent.empty());
    recent.add("/a/one.chaos");
    recent.add("/a/two.chaos");
    recent.add("");                       // ignored
    recent.add("/a/one.chaos");           // moves to the top, not listed twice
    CHECK(recent.paths() == std::vector<std::string>({"/a/one.chaos", "/a/two.chaos"}));
    CHECK(recent.remove("/a/two.chaos"));
    CHECK(!recent.remove("/a/two.chaos"));
    CHECK(recent.paths() == std::vector<std::string>({"/a/one.chaos"}));
    recent.clear();
    CHECK(recent.empty());
}

TEST(recentprojects, keeps_at_most_ten)
{
    RecentProjects recent;
    for (int i = 0; i < 12; ++i) recent.add("/p/" + std::to_string(i) + ".chaos");
    CHECK(recent.paths().size() == RecentProjects::maxEntries);
    CHECK(recent.paths().front() == "/p/11.chaos");
    CHECK(recent.paths().back() == "/p/2.chaos");
}

TEST(recentprojects, stored_values_are_cleaned_on_load)
{
    RecentProjects recent;
    std::vector<std::string> stored = {"/x/a.chaos", "", "/x/b.chaos", "/x/a.chaos"};
    for (int i = 0; i < 12; ++i) stored.push_back("/y/" + std::to_string(i) + ".chaos");
    recent.setPaths(stored);
    CHECK(recent.paths().size() == RecentProjects::maxEntries);
    CHECK(recent.paths()[0] == "/x/a.chaos");
    CHECK(recent.paths()[1] == "/x/b.chaos");
    CHECK(recent.paths()[2] == "/y/0.chaos");
}

TEST(recentprojects, labels_add_the_folder_only_when_names_clash)
{
    RecentProjects recent;
    recent.setPaths({"/music/demos/song.chaos", "/music/final/song.chaos", "/music/other.chaos"});
    const auto labels = recent.labels();
    CHECK(labels.size() == 3);
    CHECK(labels[0] == "song.chaos \xe2\x80\x94 demos");
    CHECK(labels[1] == "song.chaos \xe2\x80\x94 final");
    CHECK(labels[2] == "other.chaos");
}

TEST(recentprojects, missing_files_are_removed)
{
    const auto dir = std::filesystem::temp_directory_path() / "chaosmaker-recent-test";
    std::filesystem::create_directories(dir);
    const auto kept = (dir / "kept.chaos").string();
    { std::ofstream(kept) << "{}"; }
    const auto gone = (dir / "gone.chaos").string();
    std::filesystem::remove(gone);

    RecentProjects recent;
    recent.setPaths({gone, kept, dir.string()});   // a folder is not a project file
    CHECK(recent.removeMissing() == 2);
    CHECK(recent.paths() == std::vector<std::string>({kept}));
    std::filesystem::remove_all(dir);
}
