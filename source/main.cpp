#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <jansson.h>
#include <vector>
#include <string>

// ── Constants ──────────────────────────────────────────────────────────────
#define APP_TITLE    "Homebrew Shop"
#define APP_VERSION  "1.0.0"
#define API_URL      "https://your-server.com/api/apps.json"
#define DOWNLOAD_DIR "sdmc:/homebrew/"

// ── Data structures ────────────────────────────────────────────────────────
struct AppEntry {
    std::string name;
    std::string author;
    std::string description;
    std::string version;
    std::string url;       // direct download URL
    std::string type;      // "cia" or "3dsx"
    size_t      size;      // bytes
};

enum class Screen { MENU, LIST, DETAIL, DOWNLOADING, DONE };

// ── Download buffer ────────────────────────────────────────────────────────
struct DownloadBuffer {
    char*  data;
    size_t size;
};

static size_t write_memory(void* contents, size_t sz, size_t nmemb, void* userp) {
    size_t realsize = sz * nmemb;
    DownloadBuffer* buf = (DownloadBuffer*)userp;
    char* ptr = (char*)realloc(buf->data, buf->size + realsize + 1);
    if (!ptr) return 0;
    buf->data = ptr;
    memcpy(&buf->data[buf->size], contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';
    return realsize;
}

// Progress callback so we can show % on screen
static int progress_cb(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                        curl_off_t, curl_off_t) {
    if (dltotal > 0) {
        int pct = (int)(dlnow * 100 / dltotal);
        // Store in shared int so render loop can read it
        *((int*)clientp) = pct;
    }
    return 0;
}

// ── Networking helpers ─────────────────────────────────────────────────────
bool fetch_json(const std::string& url, std::string& out) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    DownloadBuffer buf = { (char*)malloc(1), 0 };
    curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,     APP_TITLE "/" APP_VERSION);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK) { out = buf.data; }
    free(buf.data);
    return res == CURLE_OK;
}

bool download_file(const std::string& url, const std::string& path, int& progress) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    // Ensure download directory exists
    mkdir(DOWNLOAD_DIR, 0777);

    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) { curl_easy_cleanup(curl); return false; }

    curl_easy_setopt(curl, CURLOPT_URL,              url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,    NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,        fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,   1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,   0L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS,       0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA,     &progress);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,        APP_TITLE "/" APP_VERSION);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (res != CURLE_OK) remove(path.c_str());
    return res == CURLE_OK;
}

// ── JSON parser ────────────────────────────────────────────────────────────
std::vector<AppEntry> parse_catalog(const std::string& json_str) {
    std::vector<AppEntry> apps;
    json_error_t err;
    json_t* root = json_loads(json_str.c_str(), 0, &err);
    if (!root) return apps;

    size_t idx;
    json_t* item;
    json_array_foreach(root, idx, item) {
        AppEntry e;
        auto get = [&](const char* k) -> std::string {
            json_t* v = json_object_get(item, k);
            return (v && json_is_string(v)) ? json_string_value(v) : "";
        };
        e.name        = get("name");
        e.author      = get("author");
        e.description = get("description");
        e.version     = get("version");
        e.url         = get("url");
        e.type        = get("type");
        json_t* sz = json_object_get(item, "size");
        e.size = (sz && json_is_integer(sz)) ? (size_t)json_integer_value(sz) : 0;
        if (!e.name.empty() && !e.url.empty()) apps.push_back(e);
    }
    json_decref(root);
    return apps;
}

// ── UI helpers ─────────────────────────────────────────────────────────────
static void draw_header(const char* title) {
    consoleClear();
    printf("\x1b[0;0H");
    printf("╔══════════════════════════════╗\n");
    printf("║  " APP_TITLE " v" APP_VERSION "           ║\n");
    printf("╚══════════════════════════════╝\n");
    printf(" %s\n\n", title);
}

static void draw_footer(const char* hints) {
    printf("\x1b[27;0H");
    printf("──────────────────────────────\n");
    printf(" %s", hints);
}

// ── Main ───────────────────────────────────────────────────────────────────
int main() {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    httpcInit(0);
    curl_global_init(CURL_GLOBAL_ALL);

    Screen screen  = Screen::MENU;
    int    cursor  = 0;
    int    progress = 0;
    bool   success  = false;
    std::string status_msg;
    std::vector<AppEntry> catalog;

    // ── Main loop ──────────────────────────────────────────────────────────
    while (aptMainLoop()) {
        hidScanInput();
        u32 keys = hidKeysDown();

        // ── MENU ──────────────────────────────────────────────────────────
        if (screen == Screen::MENU) {
            draw_header("Main Menu");
            printf(" [A] Browse Homebrew\n");
            printf(" [B] Exit\n");
            draw_footer("A: Select  B: Exit");

            if (keys & KEY_A) {
                draw_header("Fetching catalog...");
                std::string json;
                if (fetch_json(API_URL, json)) {
                    catalog = parse_catalog(json);
                    if (!catalog.empty()) {
                        cursor = 0;
                        screen = Screen::LIST;
                    } else {
                        status_msg = "Catalog empty or parse error.";
                    }
                } else {
                    status_msg = "Network error! Check WiFi.";
                }
            }
            if (keys & KEY_B) break;

            if (!status_msg.empty()) {
                printf("\n \x1b[31mError: %s\x1b[0m\n", status_msg.c_str());
                if (keys & KEY_A) status_msg.clear();
            }
        }

        // ── LIST ──────────────────────────────────────────────────────────
        else if (screen == Screen::LIST) {
            draw_header("Browse Homebrew");
            int total = (int)catalog.size();
            int start = std::max(0, cursor - 8);
            for (int i = start; i < std::min(start + 17, total); i++) {
                const char* mark = (i == cursor) ? ">" : " ";
                printf(" %s [%s] %s\n", mark,
                       catalog[i].type.c_str(),
                       catalog[i].name.c_str());
            }
            draw_footer("A:Details  B:Back  D-pad:Move");

            if (keys & KEY_DOWN) cursor = std::min(cursor + 1, total - 1);
            if (keys & KEY_UP)   cursor = std::max(cursor - 1, 0);
            if (keys & KEY_A)    screen = Screen::DETAIL;
            if (keys & KEY_B)    screen = Screen::MENU;
        }

        // ── DETAIL ────────────────────────────────────────────────────────
        else if (screen == Screen::DETAIL) {
            const AppEntry& app = catalog[cursor];
            draw_header("App Details");
            printf(" Name   : %s\n",   app.name.c_str());
            printf(" Author : %s\n",   app.author.c_str());
            printf(" Version: %s\n",   app.version.c_str());
            printf(" Type   : %s\n",   app.type.c_str());
            printf(" Size   : %zu KB\n", app.size / 1024);
            printf("\n %s\n", app.description.c_str());
            draw_footer("A:Download  B:Back");

            if (keys & KEY_A) {
                progress = 0;
                screen   = Screen::DOWNLOADING;
            }
            if (keys & KEY_B) screen = Screen::LIST;
        }

        // ── DOWNLOADING ───────────────────────────────────────────────────
        else if (screen == Screen::DOWNLOADING) {
            const AppEntry& app = catalog[cursor];
            draw_header("Downloading...");
            printf(" %s\n\n", app.name.c_str());

            // Progress bar (28 chars wide)
            int filled = progress * 28 / 100;
            printf(" [");
            for (int i = 0; i < 28; i++) printf("%s", i < filled ? "█" : "░");
            printf("] %d%%\n", progress);

            std::string dest = std::string(DOWNLOAD_DIR) +
                               app.name + "." + app.type;
            success = download_file(app.url, dest, progress);
            screen  = Screen::DONE;
        }

        // ── DONE ──────────────────────────────────────────────────────────
        else if (screen == Screen::DONE) {
            const AppEntry& app = catalog[cursor];
            draw_header(success ? "Download Complete!" : "Download Failed");
            if (success) {
                printf(" \x1b[32m%s\x1b[0m saved to:\n", app.name.c_str());
                printf(" " DOWNLOAD_DIR "%s.%s\n",
                       app.name.c_str(), app.type.c_str());
            } else {
                printf(" \x1b[31mFailed to download %s\x1b[0m\n",
                       app.name.c_str());
                printf(" Check your connection and try again.\n");
            }
            draw_footer("A:Back to List  B:Main Menu");

            if (keys & KEY_A) screen = Screen::LIST;
            if (keys & KEY_B) screen = Screen::MENU;
        }

        gfxFlushBuffers();
        gspWaitForVBlank();
    }

    curl_global_cleanup();
    httpcExit();
    gfxExit();
    return 0;
}
