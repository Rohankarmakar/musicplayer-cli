# Terminal Music Player 

A small C++17 terminal music browser for Fedora. It searches `~/Music`, lets you choose a track, plays it, and draws an animated ANSI-colour sine wave while it runs.

## Requirements

Install one playback backend (the app prefers `mpv`, then `ffplay`, then `mpg123`):

```bash
sudo dnf install cmake gcc-c++ make mpv
```

## Build

```bash
cmake -S . -B build
cmake --build build
./build/musicplayer
```

## Commands

```bash
musicplayer                         # browse all music interactively
musicplayer browse "daft punk"       # search, select, and play
musicplayer play "album title"       # alias for browse
musicplayer search "jazz"            # print matching paths
musicplayer help
```

Supported files: MP3, FLAC, OGG, Opus, WAV, M4A, AAC, and WMA. The playlist is cached at `~/.local/state/ripple/song-list.txt` and refreshed when the Music folder changes. When a track finishes, the next track plays automatically and the final track wraps to the first. The visualizer fills the area beneath its sine trace with colour dots and shows remaining/total time; install `ffmpeg` for `ffprobe` duration detection. While a track is playing, press `b` to seek back 5 seconds, `f` to seek forward 5 seconds, `s` to search, `d` to discover all tracks, `r` to toggle repeat-one, `x` to stop, or `q` to stop and quit. Choosing a new track stops the current track before playing the selection.

## Design

`MusicLibrary` is a repository that owns filesystem search. `AudioBackend` is a Strategy interface with runtime selection for `mpv`/`ffplay`/`mpg123`. The command map in `main.cpp` is a Command-pattern dispatcher, so new commands can be added without changing control flow.
