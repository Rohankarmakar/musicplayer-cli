#include "music_library.hpp"
#include "player.hpp"
#include "terminal.hpp"

#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {
using Args = std::vector<std::string>;
using Command = std::function<int(const Args&)>;

void usage() {
    print_banner();
    std::cout << "Usage: musicplayer <command> [query]\n\n"
              << "Commands:\n  browse [query]  Search and select a track (default)\n"
              << "  search <query>  List matching music\n  play <query>    Select and play a match\n"
              << "  help            Show this help\n";
}

std::vector<Track> find(const MusicLibrary& library, const Args& args) {
    std::ostringstream query;
    for (std::size_t i = 0; i < args.size(); ++i) query << (i ? " " : "") << args[i];
    return library.search(query.str());
}

int choose_and_play(const Args& args) {
    const MusicLibrary library(default_music_directory());
    const auto tracks = find(library, args);
    if (tracks.empty()) { print_info("No tracks found in " + default_music_directory().string()); return 0; }
    print_banner();
    std::vector<std::string> lines;
    for (std::size_t i = 0; i < tracks.size(); ++i) lines.push_back("[" + std::to_string(i + 1) + "] " + tracks[i].title);
    print_tracks(lines);
    std::cout << "\nSelect a track (1-" << tracks.size() << ", q to quit): ";
    std::string input; std::getline(std::cin, input);
    if (input == "q" || input == "Q" || input.empty()) return 0;
    try {
        const auto choice = std::stoul(input);
        if (choice == 0 || choice > tracks.size()) throw std::out_of_range("selection");
        play_with_visualizer(*make_audio_backend(), library, library.playlist(), tracks[choice - 1]);
    } catch (const std::exception&) { print_error("please choose a number from the displayed list"); return 2; }
    return 0;
}

int search(const Args& args) {
    const MusicLibrary library(default_music_directory());
    const auto tracks = find(library, args);
    std::vector<std::string> lines;
    for (const auto& track : tracks) lines.push_back(track.path.string());
    print_tracks(lines);
    return tracks.empty() ? 1 : 0;
}
}

int main(int argc, char** argv) {
    const Args arguments(argv + 1, argv + argc);
    const std::string command = arguments.empty() ? "browse" : arguments.front();
    const Args values = arguments.empty() ? Args{} : Args(arguments.begin() + 1, arguments.end());
    // Command pattern: dispatch remains open for new commands without growing main().
    const std::map<std::string, Command> commands{{"browse", choose_and_play}, {"play", choose_and_play}, {"search", search}, {"help", [](const Args&) { usage(); return 0; }}, {"--help", [](const Args&) { usage(); return 0; }}};
    try {
        const auto it = commands.find(command);
        if (it == commands.end()) { print_error("unknown command: " + command); usage(); return 2; }
        return it->second(values);
    } catch (const std::exception& error) { print_error(error.what()); return 1; }
}
