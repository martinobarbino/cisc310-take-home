#include <iostream>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <sys/sysinfo.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#define WIDTH 1024
#define HEIGHT 700
#define HEADER_HEIGHT 60
#define COL_HEADER_HEIGHT 35
#define ROW_HEIGHT 30
#define REFRESH_INTERVAL_MS 1000
#define MAX_VISIBLE_ROWS ((HEIGHT - HEADER_HEIGHT - COL_HEADER_HEIGHT) / ROW_HEIGHT)

// ============================================================================
// Data structures
// ============================================================================

enum SortColumn { SORT_PID, SORT_NAME, SORT_CPU, SORT_MEM, SORT_STATE };
enum SortDirection { SORT_ASC, SORT_DESC };

typedef struct ProcessInfo {
    int pid;
    std::string name;
    char state;
    float cpu_percent;
    float mem_percent;
    unsigned long utime;
    unsigned long stime;
    unsigned long vsize;
    long rss;
} ProcessInfo;

typedef struct SystemStats {
    float cpu_usage;
    float mem_usage;
    unsigned long mem_total_kb;
    unsigned long mem_available_kb;
    unsigned long prev_total;
    unsigned long prev_idle;
    int num_procs;
} SystemStats;

typedef struct AppData {
    TTF_Font *font;
    TTF_Font *font_small;
    TTF_Font *font_bold;
    SDL_Texture *process_icon;
    SDL_Texture *cpu_icon;
    SDL_Texture *memory_icon;
    SDL_Texture *kill_icon;
    SDL_Texture *sort_icon;
    std::vector<ProcessInfo> processes;
    SystemStats sys_stats;
    SortColumn sort_col;
    SortDirection sort_dir;
    int scroll_offset;
    int hovered_row;
    int hovered_kill;
    Uint32 last_refresh;
    std::string status_message;
    Uint32 status_time;
} AppData;

// ============================================================================
// Function declarations
// ============================================================================

void initialize(SDL_Renderer *renderer, AppData *data);
void handleEvent(SDL_Event *event, SDL_Renderer *renderer, AppData *data);
void render(SDL_Renderer *renderer, AppData *data);
void refreshProcesses(AppData *data);
void readProcessList(std::vector<ProcessInfo> &procs);
void updateSystemStats(SystemStats &stats);
void sortProcesses(std::vector<ProcessInfo> &procs, SortColumn col, SortDirection dir);
SDL_Texture* renderText(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Color color);
void drawBar(SDL_Renderer *renderer, int x, int y, int w, int h, float percent, SDL_Color fg, SDL_Color bg);
void cleanup(AppData *data);

// ============================================================================
// Main
// ============================================================================

int main(int argc, char *argv[])
{
    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_PNG);
    TTF_Init();

    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_CreateWindowAndRenderer(WIDTH, HEIGHT, 0, &window, &renderer);
    SDL_SetWindowTitle(window, "Task Manager");

    AppData data;
    initialize(renderer, &data);

    bool running = true;
    SDL_Event event;
    while (running)
    {
        // Poll events (non-blocking so we can auto-refresh)
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                running = false;
            }
            else
            {
                handleEvent(&event, renderer, &data);
            }
        }

        // Auto-refresh process list at interval
        Uint32 now = SDL_GetTicks();
        if (now - data.last_refresh >= REFRESH_INTERVAL_MS)
        {
            refreshProcesses(&data);
            data.last_refresh = now;
        }

        render(renderer, &data);
        SDL_Delay(16); // ~60 FPS
    }

    cleanup(&data);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();

    return 0;
}

// ============================================================================
// Initialization
// ============================================================================

void initialize(SDL_Renderer *renderer, AppData *data)
{
    // Load fonts
    data->font = TTF_OpenFont("resrc/fonts/OpenSans-Regular.ttf", 16);
    data->font_small = TTF_OpenFont("resrc/fonts/OpenSans-Regular.ttf", 13);
    data->font_bold = TTF_OpenFont("resrc/fonts/OpenSans-Regular.ttf", 20);

    if (!data->font || !data->font_small || !data->font_bold)
    {
        std::cerr << "Error loading font: " << TTF_GetError() << std::endl;
        std::cerr << "Make sure resrc/fonts/OpenSans-Regular.ttf exists." << std::endl;
        exit(1);
    }
    TTF_SetFontStyle(data->font_bold, TTF_STYLE_BOLD);

    // Load images
    SDL_Surface *surf;

    surf = IMG_Load("resrc/images/process_icon.png");
    data->process_icon = surf ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
    if (surf) SDL_FreeSurface(surf);

    surf = IMG_Load("resrc/images/cpu_icon.png");
    data->cpu_icon = surf ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
    if (surf) SDL_FreeSurface(surf);

    surf = IMG_Load("resrc/images/memory_icon.png");
    data->memory_icon = surf ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
    if (surf) SDL_FreeSurface(surf);

    surf = IMG_Load("resrc/images/kill_icon.png");
    data->kill_icon = surf ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
    if (surf) SDL_FreeSurface(surf);

    surf = IMG_Load("resrc/images/sort_icon.png");
    data->sort_icon = surf ? SDL_CreateTextureFromSurface(renderer, surf) : NULL;
    if (surf) SDL_FreeSurface(surf);

    // Initialize state
    data->sort_col = SORT_CPU;
    data->sort_dir = SORT_DESC;
    data->scroll_offset = 0;
    data->hovered_row = -1;
    data->hovered_kill = -1;
    data->last_refresh = 0;
    data->status_message = "";
    data->status_time = 0;

    // Initialize system stats
    data->sys_stats.prev_total = 0;
    data->sys_stats.prev_idle = 0;
    data->sys_stats.cpu_usage = 0;
    data->sys_stats.mem_usage = 0;
    data->sys_stats.num_procs = 0;

    // Initial data load
    refreshProcesses(data);
}

// ============================================================================
// OS Data Reading (Linux /proc filesystem)
// ============================================================================

void readProcessList(std::vector<ProcessInfo> &procs)
{
    procs.clear();

    // Get total system memory
    unsigned long mem_total_kb = 1;
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    while (std::getline(meminfo, line))
    {
        if (line.find("MemTotal:") == 0)
        {
            sscanf(line.c_str(), "MemTotal: %lu", &mem_total_kb);
            break;
        }
    }
    meminfo.close();

    // Get system uptime and total CPU time for CPU% calculation
    long clk_tck = sysconf(_SC_CLK_TCK);

    // Read /proc directory for process entries
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) return;

    struct dirent *entry;
    while ((entry = readdir(proc_dir)) != NULL)
    {
        // Only process numeric directories (PIDs)
        int pid = atoi(entry->d_name);
        if (pid <= 0) continue;

        ProcessInfo proc;
        proc.pid = pid;
        proc.cpu_percent = 0;
        proc.mem_percent = 0;
        proc.state = '?';

        // Read /proc/[pid]/stat
        char stat_path[64];
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
        std::ifstream stat_file(stat_path);
        if (!stat_file.is_open()) continue;

        std::string stat_line;
        std::getline(stat_file, stat_line);
        stat_file.close();

        // Parse the stat file - name is in parentheses, may contain spaces
        size_t open_paren = stat_line.find('(');
        size_t close_paren = stat_line.rfind(')');
        if (open_paren == std::string::npos || close_paren == std::string::npos) continue;

        proc.name = stat_line.substr(open_paren + 1, close_paren - open_paren - 1);

        // Parse fields after the closing parenthesis
        std::istringstream rest(stat_line.substr(close_paren + 2));
        std::string field;

        // Field 3: state
        rest >> field;
        proc.state = field[0];

        // Skip fields 4-13
        for (int i = 0; i < 10; i++) rest >> field;

        // Field 14: utime, Field 15: stime
        rest >> proc.utime >> proc.stime;

        // Skip fields 16-22
        for (int i = 0; i < 7; i++) rest >> field;

        // Field 23: vsize (virtual memory size in bytes)
        rest >> proc.vsize;

        // Field 24: rss (resident set size in pages)
        rest >> proc.rss;

        // Calculate memory percentage
        long page_size = sysconf(_SC_PAGESIZE);
        float mem_kb = (proc.rss * page_size) / 1024.0f;
        proc.mem_percent = (mem_kb / mem_total_kb) * 100.0f;

        // Read /proc/[pid]/stat for CPU usage - we use a simple heuristic:
        // total_time / uptime gives approximate CPU usage
        float uptime = 0;
        std::ifstream uptime_file("/proc/uptime");
        if (uptime_file.is_open())
        {
            uptime_file >> uptime;
            uptime_file.close();
        }

        if (uptime > 0 && clk_tck > 0)
        {
            float total_time = (float)(proc.utime + proc.stime) / clk_tck;
            proc.cpu_percent = (total_time / uptime) * 100.0f;
        }

        procs.push_back(proc);
    }
    closedir(proc_dir);
}

void updateSystemStats(SystemStats &stats)
{
    // Read overall CPU usage from /proc/stat
    std::ifstream stat_file("/proc/stat");
    std::string line;
    if (std::getline(stat_file, line))
    {
        unsigned long user, nice, system, idle, iowait, irq, softirq, steal;
        sscanf(line.c_str(), "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);

        unsigned long total = user + nice + system + idle + iowait + irq + softirq + steal;
        unsigned long total_idle = idle + iowait;

        if (stats.prev_total > 0)
        {
            unsigned long d_total = total - stats.prev_total;
            unsigned long d_idle = total_idle - stats.prev_idle;
            if (d_total > 0)
            {
                stats.cpu_usage = ((float)(d_total - d_idle) / d_total) * 100.0f;
            }
        }
        stats.prev_total = total;
        stats.prev_idle = total_idle;
    }
    stat_file.close();

    // Read memory info
    std::ifstream meminfo("/proc/meminfo");
    stats.mem_total_kb = 0;
    stats.mem_available_kb = 0;
    while (std::getline(meminfo, line))
    {
        if (line.find("MemTotal:") == 0)
            sscanf(line.c_str(), "MemTotal: %lu", &stats.mem_total_kb);
        else if (line.find("MemAvailable:") == 0)
            sscanf(line.c_str(), "MemAvailable: %lu", &stats.mem_available_kb);
    }
    meminfo.close();

    if (stats.mem_total_kb > 0)
    {
        stats.mem_usage = ((float)(stats.mem_total_kb - stats.mem_available_kb) / stats.mem_total_kb) * 100.0f;
    }
}

void refreshProcesses(AppData *data)
{
    readProcessList(data->processes);
    updateSystemStats(data->sys_stats);
    data->sys_stats.num_procs = data->processes.size();
    sortProcesses(data->processes, data->sort_col, data->sort_dir);

    // Clamp scroll offset
    int max_scroll = (int)data->processes.size() - MAX_VISIBLE_ROWS;
    if (max_scroll < 0) max_scroll = 0;
    if (data->scroll_offset > max_scroll) data->scroll_offset = max_scroll;
    if (data->scroll_offset < 0) data->scroll_offset = 0;
}

// ============================================================================
// Sorting
// ============================================================================

bool comparePidAsc(const ProcessInfo &a, const ProcessInfo &b) { return a.pid < b.pid; }
bool comparePidDesc(const ProcessInfo &a, const ProcessInfo &b) { return a.pid > b.pid; }
bool compareNameAsc(const ProcessInfo &a, const ProcessInfo &b) { return a.name < b.name; }
bool compareNameDesc(const ProcessInfo &a, const ProcessInfo &b) { return a.name > b.name; }
bool compareCpuAsc(const ProcessInfo &a, const ProcessInfo &b) { return a.cpu_percent < b.cpu_percent; }
bool compareCpuDesc(const ProcessInfo &a, const ProcessInfo &b) { return a.cpu_percent > b.cpu_percent; }
bool compareMemAsc(const ProcessInfo &a, const ProcessInfo &b) { return a.mem_percent < b.mem_percent; }
bool compareMemDesc(const ProcessInfo &a, const ProcessInfo &b) { return a.mem_percent > b.mem_percent; }
bool compareStateAsc(const ProcessInfo &a, const ProcessInfo &b) { return a.state < b.state; }
bool compareStateDesc(const ProcessInfo &a, const ProcessInfo &b) { return a.state > b.state; }

void sortProcesses(std::vector<ProcessInfo> &procs, SortColumn col, SortDirection dir)
{
    switch (col)
    {
        case SORT_PID:
            std::sort(procs.begin(), procs.end(), dir == SORT_ASC ? comparePidAsc : comparePidDesc);
            break;
        case SORT_NAME:
            std::sort(procs.begin(), procs.end(), dir == SORT_ASC ? compareNameAsc : compareNameDesc);
            break;
        case SORT_CPU:
            std::sort(procs.begin(), procs.end(), dir == SORT_ASC ? compareCpuAsc : compareCpuDesc);
            break;
        case SORT_MEM:
            std::sort(procs.begin(), procs.end(), dir == SORT_ASC ? compareMemAsc : compareMemDesc);
            break;
        case SORT_STATE:
            std::sort(procs.begin(), procs.end(), dir == SORT_ASC ? compareStateAsc : compareStateDesc);
            break;
    }
}

// ============================================================================
// Event Handling
// ============================================================================

// Column header click regions
static const int COL_PID_X = 40;
static const int COL_NAME_X = 120;
static const int COL_CPU_X = 420;
static const int COL_MEM_X = 600;
static const int COL_STATE_X = 780;
static const int COL_KILL_X = 870;
static const int COL_HEADER_Y = HEADER_HEIGHT;

void handleEvent(SDL_Event *event, SDL_Renderer *renderer, AppData *data)
{
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT)
    {
        int x = event->button.x;
        int y = event->button.y;

        // Check if click is on column headers (sorting)
        if (y >= COL_HEADER_Y && y < COL_HEADER_Y + COL_HEADER_HEIGHT)
        {
            SortColumn clicked = data->sort_col;
            if (x >= COL_PID_X && x < COL_NAME_X)
                clicked = SORT_PID;
            else if (x >= COL_NAME_X && x < COL_CPU_X)
                clicked = SORT_NAME;
            else if (x >= COL_CPU_X && x < COL_MEM_X)
                clicked = SORT_CPU;
            else if (x >= COL_MEM_X && x < COL_STATE_X)
                clicked = SORT_MEM;
            else if (x >= COL_STATE_X && x < COL_KILL_X)
                clicked = SORT_STATE;

            if (clicked == data->sort_col)
            {
                // Toggle direction
                data->sort_dir = (data->sort_dir == SORT_ASC) ? SORT_DESC : SORT_ASC;
            }
            else
            {
                data->sort_col = clicked;
                data->sort_dir = SORT_DESC;
            }
            sortProcesses(data->processes, data->sort_col, data->sort_dir);
        }
        // Check if click is on a kill button
        else if (y > COL_HEADER_Y + COL_HEADER_HEIGHT)
        {
            int row = (y - COL_HEADER_Y - COL_HEADER_HEIGHT) / ROW_HEIGHT;
            int proc_index = row + data->scroll_offset;

            if (x >= COL_KILL_X && x < COL_KILL_X + 100 &&
                proc_index >= 0 && proc_index < (int)data->processes.size())
            {
                int pid = data->processes[proc_index].pid;
                std::string name = data->processes[proc_index].name;
                int result = kill(pid, SIGTERM);
                if (result == 0)
                {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Sent SIGTERM to %s (PID %d)", name.c_str(), pid);
                    data->status_message = msg;
                }
                else
                {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Failed to kill %s (PID %d) - permission denied", name.c_str(), pid);
                    data->status_message = msg;
                }
                data->status_time = SDL_GetTicks();
                refreshProcesses(data);
            }
        }
    }
    else if (event->type == SDL_MOUSEWHEEL)
    {
        data->scroll_offset -= event->wheel.y * 3;
        int max_scroll = (int)data->processes.size() - MAX_VISIBLE_ROWS;
        if (max_scroll < 0) max_scroll = 0;
        if (data->scroll_offset > max_scroll) data->scroll_offset = max_scroll;
        if (data->scroll_offset < 0) data->scroll_offset = 0;
    }
    else if (event->type == SDL_MOUSEMOTION)
    {
        int x = event->motion.x;
        int y = event->motion.y;
        if (y > COL_HEADER_Y + COL_HEADER_HEIGHT)
        {
            int row = (y - COL_HEADER_Y - COL_HEADER_HEIGHT) / ROW_HEIGHT;
            data->hovered_row = row + data->scroll_offset;
            data->hovered_kill = (x >= COL_KILL_X && x < COL_KILL_X + 100) ? data->hovered_row : -1;
        }
        else
        {
            data->hovered_row = -1;
            data->hovered_kill = -1;
        }
    }
    else if (event->type == SDL_KEYDOWN)
    {
        if (event->key.keysym.sym == SDLK_UP)
        {
            data->scroll_offset--;
            if (data->scroll_offset < 0) data->scroll_offset = 0;
        }
        else if (event->key.keysym.sym == SDLK_DOWN)
        {
            data->scroll_offset++;
            int max_scroll = (int)data->processes.size() - MAX_VISIBLE_ROWS;
            if (max_scroll < 0) max_scroll = 0;
            if (data->scroll_offset > max_scroll) data->scroll_offset = max_scroll;
        }
        else if (event->key.keysym.sym == SDLK_PAGEUP)
        {
            data->scroll_offset -= MAX_VISIBLE_ROWS;
            if (data->scroll_offset < 0) data->scroll_offset = 0;
        }
        else if (event->key.keysym.sym == SDLK_PAGEDOWN)
        {
            data->scroll_offset += MAX_VISIBLE_ROWS;
            int max_scroll = (int)data->processes.size() - MAX_VISIBLE_ROWS;
            if (max_scroll < 0) max_scroll = 0;
            if (data->scroll_offset > max_scroll) data->scroll_offset = max_scroll;
        }
        else if (event->key.keysym.sym == SDLK_HOME)
        {
            data->scroll_offset = 0;
        }
        else if (event->key.keysym.sym == SDLK_END)
        {
            int max_scroll = (int)data->processes.size() - MAX_VISIBLE_ROWS;
            if (max_scroll < 0) max_scroll = 0;
            data->scroll_offset = max_scroll;
        }
    }
}

// ============================================================================
// Rendering helpers
// ============================================================================

SDL_Texture* renderText(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Color color)
{
    SDL_Surface *surf = TTF_RenderText_Blended(font, text, color);
    if (!surf) return NULL;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    return tex;
}

void drawBar(SDL_Renderer *renderer, int x, int y, int w, int h, float percent,
             SDL_Color fg, SDL_Color bg)
{
    // Background
    SDL_Rect bg_rect = { x, y, w, h };
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &bg_rect);

    // Filled portion
    int fill_w = (int)(w * (percent / 100.0f));
    if (fill_w > w) fill_w = w;
    if (fill_w > 0)
    {
        SDL_Rect fg_rect = { x, y, fill_w, h };
        SDL_SetRenderDrawColor(renderer, fg.r, fg.g, fg.b, fg.a);
        SDL_RenderFillRect(renderer, &fg_rect);
    }

    // Border
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_RenderDrawRect(renderer, &bg_rect);
}

void renderTextAt(SDL_Renderer *renderer, TTF_Font *font, const char *text,
                  SDL_Color color, int x, int y)
{
    SDL_Texture *tex = renderText(renderer, font, text, color);
    if (!tex) return;
    int w, h;
    SDL_QueryTexture(tex, NULL, NULL, &w, &h);
    SDL_Rect dst = { x, y, w, h };
    SDL_RenderCopy(renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

void renderIcon(SDL_Renderer *renderer, SDL_Texture *icon, int x, int y, int size)
{
    if (!icon) return;
    SDL_Rect dst = { x, y, size, size };
    SDL_RenderCopy(renderer, icon, NULL, &dst);
}

// ============================================================================
// Main Render
// ============================================================================

void render(SDL_Renderer *renderer, AppData *data)
{
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Color black = { 0, 0, 0, 255 };
    SDL_Color gray = { 180, 180, 180, 255 };
    SDL_Color dark_gray = { 60, 60, 60, 255 };
    SDL_Color light_gray = { 240, 240, 240, 255 };
    SDL_Color row_hover = { 220, 235, 255, 255 };
    SDL_Color header_bg = { 45, 52, 64, 255 };
    SDL_Color col_header_bg = { 70, 80, 95, 255 };
    SDL_Color green = { 76, 175, 80, 255 };
    SDL_Color green_bg = { 200, 230, 201, 255 };
    SDL_Color blue = { 33, 150, 243, 255 };
    SDL_Color blue_bg = { 187, 222, 251, 255 };
    SDL_Color red = { 244, 67, 54, 255 };
    SDL_Color red_light = { 255, 120, 110, 255 };
    SDL_Color yellow = { 255, 193, 7, 255 };

    // Clear background
    SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
    SDL_RenderClear(renderer);

    // ====== Header bar ======
    SDL_Rect header_rect = { 0, 0, WIDTH, HEADER_HEIGHT };
    SDL_SetRenderDrawColor(renderer, header_bg.r, header_bg.g, header_bg.b, 255);
    SDL_RenderFillRect(renderer, &header_rect);

    // Title with icon
    renderIcon(renderer, data->process_icon, 12, 14, 32);
    renderTextAt(renderer, data->font_bold, "Task Manager", white, 50, 8);

    // System overview stats in header
    char cpu_str[64], mem_str[64], proc_str[32];
    snprintf(cpu_str, sizeof(cpu_str), "CPU: %.1f%%", data->sys_stats.cpu_usage);
    snprintf(mem_str, sizeof(mem_str), "MEM: %.1f%% (%lu / %lu MB)",
             data->sys_stats.mem_usage,
             (data->sys_stats.mem_total_kb - data->sys_stats.mem_available_kb) / 1024,
             data->sys_stats.mem_total_kb / 1024);
    snprintf(proc_str, sizeof(proc_str), "Processes: %d", data->sys_stats.num_procs);

    // CPU icon + bar
    renderIcon(renderer, data->cpu_icon, 250, 6, 20);
    renderTextAt(renderer, data->font_small, cpu_str, white, 275, 8);
    drawBar(renderer, 275, 28, 120, 12, data->sys_stats.cpu_usage, green, {50, 60, 70, 255});

    // Memory icon + bar
    renderIcon(renderer, data->memory_icon, 430, 6, 20);
    renderTextAt(renderer, data->font_small, mem_str, white, 455, 8);
    drawBar(renderer, 455, 28, 120, 12, data->sys_stats.mem_usage, blue, {50, 60, 70, 255});

    // Process count
    renderTextAt(renderer, data->font_small, proc_str, white, 700, 8);

    // Scroll indicator
    char scroll_str[64];
    int visible_end = data->scroll_offset + MAX_VISIBLE_ROWS;
    if (visible_end > (int)data->processes.size()) visible_end = data->processes.size();
    snprintf(scroll_str, sizeof(scroll_str), "Showing %d-%d of %d",
             data->scroll_offset + 1, visible_end, (int)data->processes.size());
    renderTextAt(renderer, data->font_small, scroll_str, white, 700, 28);

    // ====== Column headers ======
    SDL_Rect col_rect = { 0, HEADER_HEIGHT, WIDTH, COL_HEADER_HEIGHT };
    SDL_SetRenderDrawColor(renderer, col_header_bg.r, col_header_bg.g, col_header_bg.b, 255);
    SDL_RenderFillRect(renderer, &col_rect);

    const char *col_labels[] = { "PID", "Name", "CPU %", "Memory %", "State", "Action" };
    int col_positions[] = { COL_PID_X, COL_NAME_X, COL_CPU_X, COL_MEM_X, COL_STATE_X, COL_KILL_X };
    SortColumn col_ids[] = { SORT_PID, SORT_NAME, SORT_CPU, SORT_MEM, SORT_STATE, SORT_STATE };

    for (int i = 0; i < 6; i++)
    {
        SDL_Color label_col = (i < 5 && data->sort_col == col_ids[i]) ? yellow : white;
        renderTextAt(renderer, data->font_small, col_labels[i], label_col,
                     col_positions[i], HEADER_HEIGHT + 8);

        // Sort indicator arrow
        if (i < 5 && data->sort_col == col_ids[i] && data->sort_icon)
        {
            int arrow_x = col_positions[i] + 50;
            SDL_Rect arrow_dst = { arrow_x, HEADER_HEIGHT + 10, 12, 12 };
            // Flip if ascending
            if (data->sort_dir == SORT_ASC)
            {
                SDL_RenderCopyEx(renderer, data->sort_icon, NULL, &arrow_dst, 180, NULL, SDL_FLIP_NONE);
            }
            else
            {
                SDL_RenderCopy(renderer, data->sort_icon, NULL, &arrow_dst);
            }
        }
    }

    // ====== Horizontal separator line ======
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
    SDL_RenderDrawLine(renderer, 0, HEADER_HEIGHT + COL_HEADER_HEIGHT,
                       WIDTH, HEADER_HEIGHT + COL_HEADER_HEIGHT);

    // ====== Process rows ======
    int start_y = HEADER_HEIGHT + COL_HEADER_HEIGHT;
    int max_rows = MAX_VISIBLE_ROWS;

    for (int i = 0; i < max_rows; i++)
    {
        int proc_idx = i + data->scroll_offset;
        if (proc_idx >= (int)data->processes.size()) break;

        ProcessInfo &proc = data->processes[proc_idx];
        int row_y = start_y + (i * ROW_HEIGHT);

        // Row background - alternating + hover
        SDL_Rect row_rect = { 0, row_y, WIDTH, ROW_HEIGHT };
        if (proc_idx == data->hovered_row)
        {
            SDL_SetRenderDrawColor(renderer, row_hover.r, row_hover.g, row_hover.b, 255);
        }
        else if (i % 2 == 0)
        {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        }
        else
        {
            SDL_SetRenderDrawColor(renderer, light_gray.r, light_gray.g, light_gray.b, 255);
        }
        SDL_RenderFillRect(renderer, &row_rect);

        // Separator line
        SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
        SDL_RenderDrawLine(renderer, 0, row_y + ROW_HEIGHT - 1, WIDTH, row_y + ROW_HEIGHT - 1);

        // Process icon
        renderIcon(renderer, data->process_icon, 10, row_y + 3, 24);

        // PID
        char pid_str[16];
        snprintf(pid_str, sizeof(pid_str), "%d", proc.pid);
        renderTextAt(renderer, data->font_small, pid_str, dark_gray, COL_PID_X, row_y + 6);

        // Name (truncate if too long)
        std::string display_name = proc.name;
        if (display_name.length() > 35) display_name = display_name.substr(0, 32) + "...";
        renderTextAt(renderer, data->font_small, display_name.c_str(), black, COL_NAME_X, row_y + 6);

        // CPU % with bar
        char cpu_pct[16];
        snprintf(cpu_pct, sizeof(cpu_pct), "%.1f%%", proc.cpu_percent);

        // Color based on usage
        SDL_Color cpu_bar_fg = green;
        if (proc.cpu_percent > 50) cpu_bar_fg = red;
        else if (proc.cpu_percent > 20) cpu_bar_fg = yellow;

        drawBar(renderer, COL_CPU_X, row_y + 4, 100, 14, proc.cpu_percent, cpu_bar_fg, green_bg);
        renderTextAt(renderer, data->font_small, cpu_pct, dark_gray, COL_CPU_X + 105, row_y + 6);

        // Memory % with bar
        char mem_pct[16];
        snprintf(mem_pct, sizeof(mem_pct), "%.1f%%", proc.mem_percent);

        SDL_Color mem_bar_fg = blue;
        if (proc.mem_percent > 30) mem_bar_fg = red;
        else if (proc.mem_percent > 10) mem_bar_fg = yellow;

        drawBar(renderer, COL_MEM_X, row_y + 4, 100, 14, proc.mem_percent, mem_bar_fg, blue_bg);
        renderTextAt(renderer, data->font_small, mem_pct, dark_gray, COL_MEM_X + 105, row_y + 6);

        // State
        const char *state_str;
        SDL_Color state_color;
        switch (proc.state)
        {
            case 'R': state_str = "Running"; state_color = green; break;
            case 'S': state_str = "Sleeping"; state_color = gray; break;
            case 'D': state_str = "Disk Wait"; state_color = yellow; break;
            case 'Z': state_str = "Zombie"; state_color = red; break;
            case 'T': state_str = "Stopped"; state_color = red; break;
            case 'I': state_str = "Idle"; state_color = gray; break;
            default: state_str = "Unknown"; state_color = gray; break;
        }
        renderTextAt(renderer, data->font_small, state_str, state_color, COL_STATE_X, row_y + 6);

        // Kill button
        SDL_Rect kill_btn = { COL_KILL_X, row_y + 3, 70, 24 };
        if (proc_idx == data->hovered_kill)
        {
            SDL_SetRenderDrawColor(renderer, red_light.r, red_light.g, red_light.b, 255);
        }
        else
        {
            SDL_SetRenderDrawColor(renderer, red.r, red.g, red.b, 255);
        }
        SDL_RenderFillRect(renderer, &kill_btn);

        // Kill button border
        SDL_SetRenderDrawColor(renderer, 200, 50, 40, 255);
        SDL_RenderDrawRect(renderer, &kill_btn);

        // Kill icon + text
        renderIcon(renderer, data->kill_icon, COL_KILL_X + 4, row_y + 5, 18);
        renderTextAt(renderer, data->font_small, "Kill", white, COL_KILL_X + 25, row_y + 6);
    }

    // ====== Scrollbar ======
    if ((int)data->processes.size() > MAX_VISIBLE_ROWS)
    {
        int total_rows = data->processes.size();
        int scrollbar_area_h = HEIGHT - start_y;
        int thumb_h = (MAX_VISIBLE_ROWS * scrollbar_area_h) / total_rows;
        if (thumb_h < 20) thumb_h = 20;
        int thumb_y = start_y + (data->scroll_offset * (scrollbar_area_h - thumb_h)) /
                      (total_rows - MAX_VISIBLE_ROWS);

        // Scrollbar track
        SDL_Rect track = { WIDTH - 10, start_y, 10, scrollbar_area_h };
        SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
        SDL_RenderFillRect(renderer, &track);

        // Scrollbar thumb
        SDL_Rect thumb = { WIDTH - 10, thumb_y, 10, thumb_h };
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        SDL_RenderFillRect(renderer, &thumb);
    }

    // ====== Status message (bottom bar) ======
    if (!data->status_message.empty())
    {
        Uint32 elapsed = SDL_GetTicks() - data->status_time;
        if (elapsed < 3000)
        {
            SDL_Rect status_rect = { 0, HEIGHT - 28, WIDTH, 28 };
            SDL_SetRenderDrawColor(renderer, 50, 50, 50, 220);
            SDL_RenderFillRect(renderer, &status_rect);
            renderTextAt(renderer, data->font_small, data->status_message.c_str(), white, 10, HEIGHT - 24);
        }
        else
        {
            data->status_message = "";
        }
    }

    // ====== Vertical column separator lines ======
    SDL_SetRenderDrawColor(renderer, 200, 200, 210, 255);
    SDL_RenderDrawLine(renderer, COL_NAME_X - 5, HEADER_HEIGHT, COL_NAME_X - 5, HEIGHT);
    SDL_RenderDrawLine(renderer, COL_CPU_X - 5, HEADER_HEIGHT, COL_CPU_X - 5, HEIGHT);
    SDL_RenderDrawLine(renderer, COL_MEM_X - 5, HEADER_HEIGHT, COL_MEM_X - 5, HEIGHT);
    SDL_RenderDrawLine(renderer, COL_STATE_X - 5, HEADER_HEIGHT, COL_STATE_X - 5, HEIGHT);
    SDL_RenderDrawLine(renderer, COL_KILL_X - 5, HEADER_HEIGHT, COL_KILL_X - 5, HEIGHT);

    SDL_RenderPresent(renderer);
}

// ============================================================================
// Cleanup
// ============================================================================

void cleanup(AppData *data)
{
    if (data->process_icon) SDL_DestroyTexture(data->process_icon);
    if (data->cpu_icon) SDL_DestroyTexture(data->cpu_icon);
    if (data->memory_icon) SDL_DestroyTexture(data->memory_icon);
    if (data->kill_icon) SDL_DestroyTexture(data->kill_icon);
    if (data->sort_icon) SDL_DestroyTexture(data->sort_icon);
    if (data->font) TTF_CloseFont(data->font);
    if (data->font_small) TTF_CloseFont(data->font_small);
    if (data->font_bold) TTF_CloseFont(data->font_bold);
}
