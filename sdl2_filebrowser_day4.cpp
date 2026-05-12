#include <iostream>
#include <string>
#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <unistd.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#define WIDTH 800
#define HEIGHT 600

typedef struct FileEntry {
    std::string name;
    bool is_directory;
    uint64_t size;
} FileEntry;

typedef struct GFile {
    SDL_Texture *icon;
    SDL_Texture *name;
    SDL_Texture *size;
    SDL_Rect icon_pos;
    SDL_Rect name_pos;
    SDL_Rect size_pos;
} GFile;

typedef struct AppData {
    std::filesystem::path current_directory;
    TTF_Font *font;
    SDL_Texture *directory_image;
    SDL_Texture *file_image;
    std::vector<FileEntry*> entries;
    std::vector<GFile*> graphic_entries;
} AppData;

void initialize(SDL_Renderer *renderer, AppData *data_ptr);
void handleEvent(SDL_Event *event, SDL_Renderer *renderer, AppData *data_ptr);
void render(SDL_Renderer *renderer, AppData *data_ptr);
void populateDirectory(SDL_Renderer *renderer, AppData *data_ptr);
void listFiles(std::filesystem::path directory, std::vector<FileEntry*> &entries);
void clearGFiles(std::vector<GFile*>& graphic_entries);
bool compareFileEntries(const FileEntry *a, const FileEntry *b);
bool pointInRect(int x, int y, SDL_Rect& rect);
void quit(AppData *data_ptr);

int main(int argc, char *argv[])
{
    // File browser starts at user's home directory
    std::filesystem::path home(getenv("HOME"));
    std::cout << "HOME: " << home << std::endl;

    // Initialize SDL2 (including image and font loaders)
    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_PNG);
    TTF_Init();

    // Create window and renderer
    SDL_Renderer *renderer;
    SDL_Window *window;
    SDL_CreateWindowAndRenderer(WIDTH, HEIGHT, 0, &window, &renderer);

    // Initialize file browser application
    AppData data;
    data.current_directory = home;
    initialize(renderer, &data);

    // Perform render loop
    SDL_Event event;
    do
    {
        render(renderer, &data);
        SDL_WaitEvent(&event);
        handleEvent(&event, renderer, &data);
    } while (event.type != SDL_QUIT);

    // Clean up
    quit(&data);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();

    return 0;
}

void initialize(SDL_Renderer *renderer, AppData *data_ptr)
{
    // Load font
    data_ptr->font = TTF_OpenFont("resrc/fonts/OpenSans-Regular.ttf", 24);

    // Load images and create texture
    SDL_Surface *dir_surf = IMG_Load("resrc/images/directory_icon.png");
    data_ptr->directory_image = SDL_CreateTextureFromSurface(renderer, dir_surf);
    SDL_FreeSurface(dir_surf);

    SDL_Surface *file_surf = IMG_Load("resrc/images/file_icon.png");
    data_ptr->file_image = SDL_CreateTextureFromSurface(renderer, file_surf);
    SDL_FreeSurface(file_surf);

    // Populate with current directory
    populateDirectory(renderer, data_ptr);
}

void handleEvent(SDL_Event *event, SDL_Renderer *renderer, AppData *data_ptr)
{
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT)
    {
        int x = event->button.x;
        int y = event->button.y;

        for (int i = 0; i < data_ptr->graphic_entries.size(); i++)
        {
            if (pointInRect(x, y, data_ptr->graphic_entries[i]->icon_pos) ||
                pointInRect(x, y, data_ptr->graphic_entries[i]->name_pos))
            {
                // Check if folder or file
                //  - folder: make current directory and reset files/graphics
                //  - file: open with default app
                if (data_ptr->entries[i]->is_directory)
                {
                    data_ptr->current_directory /= data_ptr->entries[i]->name;
                    clearGFiles(data_ptr->graphic_entries);
                    populateDirectory(renderer, data_ptr);
                }
                else
                {
                    pid_t pid = fork();
                    if (pid == 0)
                    {
                        std::filesystem::path filepath = data_ptr->current_directory / data_ptr->entries[i]->name;
                        char* args[3];
                        args[0] = const_cast<char*>("/usr/bin/xdg-open");
                        args[1] = const_cast<char*>(filepath.c_str());
                        args[2] = NULL;
                        execvp("/usr/bin/xdg-open", args);
                    }
                }
            }
        }
    }
    else if (event->type == SDL_MOUSEWHEEL)
    {
        for (int i = 0; i < data_ptr->graphic_entries.size(); i++)
        {
            data_ptr->graphic_entries[i]->icon_pos.y += 3 * event->wheel.y;
            data_ptr->graphic_entries[i]->name_pos.y += 3 * event->wheel.y;
            data_ptr->graphic_entries[i]->size_pos.y += 3 * event->wheel.y;
        }
    }
}

void render(SDL_Renderer *renderer, AppData *data_ptr)
{
    // Erase prior frame content (to light gray)
    SDL_SetRenderDrawColor(renderer, 235, 235, 235, 255);
    SDL_RenderClear(renderer);

    // Draw file entries
    for (int i = 0; i < data_ptr->graphic_entries.size(); i++)
    {
        SDL_RenderCopy(renderer, data_ptr->graphic_entries[i]->icon, NULL,
            &(data_ptr->graphic_entries[i]->icon_pos));

        SDL_RenderCopy(renderer, data_ptr->graphic_entries[i]->name, NULL,
            &(data_ptr->graphic_entries[i]->name_pos));

        SDL_RenderCopy(renderer, data_ptr->graphic_entries[i]->size, NULL,
            &(data_ptr->graphic_entries[i]->size_pos));
    }

    // Draw solid rectangle (teal)
    //rect.x = 440;
    //rect.y = 320;
    //rect.w = 40;
    //rect.h = 30;
    //SDL_SetRenderDrawColor(renderer, 0, 128, 128, 255);
    //SDL_RenderFillRect(renderer, &rect); 

    // Display rendered frame
    SDL_RenderPresent(renderer);
}

void populateDirectory(SDL_Renderer *renderer, AppData *data_ptr)
{
    // Get entries in current directory
    listFiles(data_ptr->current_directory, data_ptr->entries);

    // Create texture for text
    SDL_Color color = { 0, 0, 0 };
    char file_size_str[32];
    for (int i = 0; i < data_ptr->entries.size(); i++)
    {
        GFile *g_entry = new GFile();

        const char *name = data_ptr->entries[i]->name.c_str();
        SDL_Surface *txt_surf = TTF_RenderText_Solid(data_ptr->font, name, color);
        g_entry->name = SDL_CreateTextureFromSurface(renderer, txt_surf);
        SDL_FreeSurface(txt_surf);

        if (data_ptr->entries[i]->is_directory)
        {
            g_entry->icon = data_ptr->directory_image;
            snprintf(file_size_str, 32, "--");
        }
        else
        {
            g_entry->icon = data_ptr->file_image;
            snprintf(file_size_str, 32, "%lu bytes", data_ptr->entries[i]->size);
        }
        SDL_Surface *size_surf = TTF_RenderText_Solid(data_ptr->font, file_size_str, color);
        g_entry->size = SDL_CreateTextureFromSurface(renderer, size_surf);
        SDL_FreeSurface(size_surf);

        g_entry->icon_pos.w = 28;
        g_entry->icon_pos.h = 28;
        g_entry->icon_pos.x = 10;
        g_entry->icon_pos.y = 50 + (30 * i);

        SDL_QueryTexture(g_entry->name, NULL, NULL, &(g_entry->name_pos.w), &(g_entry->name_pos.h));
        g_entry->name_pos.x = 40;
        g_entry->name_pos.y = 50 + (30 * i);

        SDL_QueryTexture(g_entry->size, NULL, NULL, &(g_entry->size_pos.w), &(g_entry->size_pos.h));
        g_entry->size_pos.x = 400;
        g_entry->size_pos.y = 50 + (30 * i);

        data_ptr->graphic_entries.push_back(g_entry);
    }
}

void listFiles(std::filesystem::path directory, std::vector<FileEntry*> &entries)
{
    // Clear out old data
    entries.clear();

    // TODO: get entries in directory and sort alphabetically
    std::filesystem::directory_iterator it;
    for (it = std::filesystem::directory_iterator(directory);
         it != std::filesystem::end(it);
         it++)
    {
        const std::filesystem::directory_entry& entry = *it;

        std::string entry_name = entry.path().filename().string();
        if (entry_name[0] != '.')
        {
            FileEntry *file_entry = new FileEntry();
            file_entry->name = entry_name;
            file_entry->is_directory = entry.is_directory();
            if (file_entry->is_directory)
            {
                file_entry->size = 0;
            }
            else
            {
                file_entry->size = entry.file_size();
            }
            entries.push_back(file_entry);
        }
    }
    std::sort(entries.begin(), entries.end(), compareFileEntries);
}

bool compareFileEntries(const FileEntry *a, const FileEntry *b)
{
    return a->name < b->name;
}

void clearGFiles(std::vector<GFile*>& graphic_entries)
{
    for (int i = 0; i < graphic_entries.size(); i++)
    {
        SDL_DestroyTexture(graphic_entries[i]->name);
        SDL_DestroyTexture(graphic_entries[i]->size);
    }
    graphic_entries.clear();
}

bool pointInRect(int x, int y, SDL_Rect& rect)
{
    if (x > rect.x && x < rect.x + rect.w &&
        y > rect.y && y < rect.y + rect.h)
    {
        return true;
    }
    return false;
}

void quit(AppData *data_ptr)
{
    clearGFiles(data_ptr->graphic_entries);
    SDL_DestroyTexture(data_ptr->directory_image);
    SDL_DestroyTexture(data_ptr->file_image);
    TTF_CloseFont(data_ptr->font);
}
