# SDL2 Task Manager

A task manager for Linux that uses SDL2 to show running processes and lets you sort, scroll, and kill them.

It pulls data from `/proc` (process info, CPU stats, memory) and refreshes every second.

## What it does

- Shows each process with its PID, name, CPU%, memory%, and state
- CPU and memory columns have colored usage bars
- Header bar shows overall CPU/memory usage
- You can click column headers to sort by that column
- Scroll with mouse wheel or arrow keys (Page Up/Down and Home/End work too)
- Each row has a kill button that sends SIGTERM to that process
- Icons loaded from PNGs, text rendered with SDL2_ttf

## Setup

Install SDL2 on your Linux VM:
```
sudo apt install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev
```

Generate the icons and grab a font:
```
python3 generate_icons.py
mkdir -p resrc/fonts
wget -O resrc/fonts/OpenSans-Regular.ttf \
  'https://github.com/google/fonts/raw/main/ofl/opensans/OpenSans%5Bwdth%2Cwght%5D.ttf'
```

## Build and run

```
make
./taskmanager
```

## How it works

Process data comes from `/proc/[pid]/stat` — that gives the PID, name, state, CPU time, and RSS.
Memory percentages are calculated against `/proc/meminfo`.
CPU usage for the whole system comes from `/proc/stat`, using the delta between two reads.

The main loop polls for SDL events and redraws at ~60fps. Every second it re-reads `/proc` to update the process list.
