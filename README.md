# Task Manager - SDL2 OS Visualization

An interactive graphical task manager built with SDL2 that reads from the Linux `/proc` filesystem to display and manage running processes in real-time.

## Features

### Basic (Visual Interface)
- **Text rendering**: Process names, PIDs, CPU/memory percentages, state labels
- **Images**: Process, CPU, memory, kill, and sort arrow icons (PNG)
- **Colored rectangles/bars**: CPU and memory usage bars with color-coded thresholds (green/yellow/red)
- **Auto-refresh**: Process list updates every 1 second by reading `/proc`
- **System overview**: Overall CPU usage, memory usage, and process count in the header bar

### Advanced (User Interaction)
- **Sorting**: Click any column header (PID, Name, CPU%, Memory%, State) to sort ascending/descending
- **Scrolling**: Mouse wheel, arrow keys, Page Up/Down, Home/End to navigate the process list
- **Kill process**: Click the red "Kill" button on any row to send SIGTERM to that process
- **Hover highlighting**: Rows highlight on mouse hover for better readability

## Dependencies

Install SDL2 development libraries on your Linux VM:

```bash
sudo apt install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev
```

## Setup

1. Generate the icon images:
```bash
python3 generate_icons.py
```

2. Download the font:
```bash
mkdir -p resrc/fonts
wget -O resrc/fonts/OpenSans-Regular.ttf \
  'https://github.com/google/fonts/raw/main/ofl/opensans/OpenSans%5Bwdth%2Cwght%5D.ttf'
```

## Build & Run

```bash
make
./taskmanager
```

## Controls

| Input | Action |
|-------|--------|
| Click column header | Sort by that column (toggles asc/desc) |
| Mouse wheel | Scroll process list |
| Arrow Up/Down | Scroll one row |
| Page Up/Down | Scroll one page |
| Home / End | Jump to top / bottom |
| Click "Kill" button | Send SIGTERM to that process |

## Architecture

- Reads `/proc/[pid]/stat` for each process to get PID, name, state, CPU time, and memory
- Reads `/proc/stat` for overall CPU usage (delta between refreshes)
- Reads `/proc/meminfo` for total and available memory
- Uses SDL2 for windowing and 2D rendering
- Uses SDL2_ttf for text rendering
- Uses SDL2_image for PNG icon loading
- Non-blocking event loop with ~60 FPS rendering and 1-second data refresh interval
