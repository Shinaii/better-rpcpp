#include <iostream>
#include <csignal>
#include <thread>
#include <unistd.h>
#include <time.h>
#include <regex>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>


// Discord RPC
#include "../discord/discord.h"

// X11 libs
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

// variables
#define VERSION "2.2.1"

//local includes

namespace
{
    volatile bool interrupted{false};
}
namespace fs = std::filesystem;
using namespace std;

int startTime;
Display *disp;
float mem = -1, cpu = -1;
string distro;
static int trapped_error_code = 0;
string wm;

vector<string> apps = {
    "blender", "chrome", "chromium", "discord", "dolphin",
    "firefox", "gimp", "hl2_linux", "hoi4", "konsole",
    "lutris", "st", "steam", "surf", "vscode",
    "worldbox", "xterm"
};

map<string, string> aliases = {
    {"vscodium", "vscode"}, {"code", "vscode"}, {"code - [a-z]+", "vscode"},
    {"stardew valley", "stardewvalley"}, {"minecraft [a-z0-9.]+", "minecraft"},
    {"lunar client [a-z0-9\\(\\)\\.\\-\\/]+", "minecraft"},
    {"telegram(desktop)?", "telegram"}, {"terraria\\.bin\\.x86_64", "terraria"},
    {"u?xterm", "xterm"}, {"vivaldi(-stable)?", "vivaldi"}
};

map<string, string> distros_lsb = {
    {"Arch|Artix", "archlinux"}, {"LinuxMint", "lmint"},
    {"Gentoo", "gentoo"}, {"Ubuntu", "ubuntu"},
    {"ManjaroLinux", "manjaro"}
};

map<string, string> distros_os = {
    {"Arch Linux", "archlinux"}, {"Linux Mint", "lmint"},
    {"Gentoo", "gentoo"}, {"Ubuntu", "ubuntu"},
    {"Manjaro Linux", "manjaro"}
};

string helpMsg = 
    "========================================\n"
    "           Better-RPC++ Help Menu       \n"
    "========================================\n"
    "\n"
    "Usage:\n"
    "  brpc [options]\n"
    "\n"
    "Options:\n"
    "  -k, --kill             Kill the currently running instance.\n"
    "  -f, --ignore-discord   Don't check for Discord on start.\n"
    "  --debug                Print debug messages.\n"
    "  --usage-sleep=5000     Sleep time in milliseconds between updating CPU and RAM usages.\n"
    "  --update-sleep=100     Sleep time in milliseconds between updating the rich presence and focused application.\n"
    "  --no-small-image       Disable small image in the rich presence (focused application).\n"
    "\n"
    "  -h, --help             Display this help menu and exit.\n"
    "  -v, --version          Output version number and exit.\n"
    "\n"
    "========================================\n"
    "       Better-RPC++ Version " + std::string(VERSION) + "       \n"
    "========================================\n";
// regular expressions

regex memavailr("MemAvailable: +(\\d+) kB");
regex memtotalr("MemTotal: +(\\d+) kB");
regex processRegex("\\/proc\\/\\d+\\/cmdline");
regex usageRegex("^usage-sleep=(\\d+)$");
regex updateRegex("^update-sleep=(\\d+)$");

vector<pair<regex, string>> aliases_regex = {};
vector<pair<regex, string>> distros_lsb_regex = {};
vector<pair<regex, string>> distros_os_regex = {};

struct DiscordState
{
    discord::User currentUser;

    unique_ptr<discord::Core> core;
};

struct DistroAsset
{
    string image;
    string text;
};

struct WindowAsset
{
    string image;
    string text;
};

struct Config
{
    bool ignoreDiscord = false;
    bool debug = false;
    int usageSleep = 5000;
    int updateSleep = 300;
    bool noSmallImage = false;
    bool printHelp = false;
    bool printVersion = false;
};

Config config;

// local imports

#include "logging.hpp"
#include "wm.hpp"

// methods

static int error_handler(Display *display, XErrorEvent *error)
{
    trapped_error_code = error->error_code;
    return 0;
}

string lower(string s)
{
    transform(s.begin(), s.end(), s.begin(),
              [](unsigned char c)
              { return tolower(c); });
    return s;
}

double ms_uptime(void)
{
    FILE *in = fopen("/proc/uptime", "r");
    double retval = 0;
    char tmp[256] = {0x0};
    if (in != NULL)
    {
        fgets(tmp, sizeof(tmp), in);
        retval = atof(tmp);
        fclose(in);
    }
    return retval;
}

float getRAM()
{
    ifstream meminfo;
    meminfo.open("/proc/meminfo");

    long total = 0;
    long available = 0;

    smatch matcher;
    string line;

    while (getline(meminfo, line))
    {
        if (regex_search(line, matcher, memavailr))
        {
            available = stoi(matcher[1]);
        }
        else if (regex_search(line, matcher, memtotalr))
        {
            total = stoi(matcher[1]);
        }
    }

    meminfo.close();

    if (total == 0)
    {
        return 0;
    }
    return (float)(total - available) / total * 100;
}

void setActivity(DiscordState &state, string details, string sstate, string smallimage, string smallimagetext, string largeimage, string largeimagetext, long uptime, discord::ActivityType type)
{
    time_t now = time(nullptr);
    discord::Activity activity{};
    activity.SetDetails(details.c_str());
    activity.SetState(sstate.c_str());
    activity.GetAssets().SetSmallImage(smallimage.c_str());
    activity.GetAssets().SetSmallText(smallimagetext.c_str());
    activity.GetAssets().SetLargeImage(largeimage.c_str());
    activity.GetAssets().SetLargeText(largeimagetext.c_str());
    activity.GetTimestamps().SetStart(uptime);
    activity.SetType(type);

    state.core->ActivityManager().UpdateActivity(activity, [](discord::Result result)
                                                 { if(config.debug) log(string((result == discord::Result::Ok) ? "Succeeded" : "Failed")  + " updating activity!", LogType::DEBUG); });
}

string getHyprlandSocketPath()
{
    const char *xdgRuntimeDir = getenv("XDG_RUNTIME_DIR");
    const char *hyprlandSignature = getenv("HYPRLAND_INSTANCE_SIGNATURE");

    if (!xdgRuntimeDir || !hyprlandSignature)
    {
        log("Hyprland environment variables are not set. Ensure Hyprland is running.", LogType::ERROR);
        return "";
    }

    // Construct the socket path
    string socketPath = string(xdgRuntimeDir) + "/hypr/" + string(hyprlandSignature) + "/.socket2.sock";

    // Check if the socket exists
    if (!std::filesystem::exists(socketPath))
    {
        log("Hyprland IPC socket not found at: " + socketPath, LogType::ERROR);
        return "";
    }

    return socketPath;
}

string getActiveWindowClassName(Display *disp)
{
    // Check if Hyprland is running
    const char *xdgRuntimeDir = getenv("XDG_RUNTIME_DIR");
    const char *hyprlandSignature = getenv("HYPRLAND_INSTANCE_SIGNATURE");

    if (xdgRuntimeDir && hyprlandSignature)
    {
        // Use Hyprland-specific logic
        string socketPath = string(xdgRuntimeDir) + "/hypr/" + string(hyprlandSignature) + "/.socket2.sock";

        if (std::filesystem::exists(socketPath))
        {
            int sock = socket(AF_UNIX, SOCK_STREAM, 0);
            if (sock == -1)
            {
                log("Failed to create socket for Hyprland IPC", LogType::ERROR);
                return "";
            }

            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

            if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
            {
                log("Failed to connect to Hyprland IPC socket", LogType::ERROR);
                close(sock);
                return "";
            }

            // Send the "activewindow" command
            string command = "activewindow";
            if (send(sock, command.c_str(), command.size(), 0) == -1)
            {
                log("Failed to send command to Hyprland IPC socket", LogType::ERROR);
                close(sock);
                return "";
            }

            char buffer[8192];
            ssize_t bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (bytesRead <= 0)
            {
                log("Failed to read response from Hyprland IPC socket", LogType::ERROR);
                close(sock);
                return "";
            }

            buffer[bytesRead] = '\0';
            string response = buffer;
            log("Hyprland IPC raw response: " + response, LogType::DEBUG);
            close(sock);

            // Parse the response to extract the active window class name
            size_t startPos = response.find("activewindow>>");
            if (startPos != string::npos)
            {
                startPos += 14;
                size_t endPos = response.find(",", startPos);
                if (endPos != string::npos)
                {
                    string windowClass = response.substr(startPos, endPos - startPos);
                    log("Active window class: " + windowClass, LogType::DEBUG);
                    return windowClass;
                }
            }

            log("No active window found in Hyprland response", LogType::DEBUG);
            return "";
        }
    }

    // Fallback to X11 for non-Hyprland environments
    Window root = XDefaultRootWindow(disp);

    char prop[256];
    get_property(disp, root, XA_WINDOW, "_NET_ACTIVE_WINDOW", prop, sizeof(prop));

    if (prop[0] == '\0')
    {
        return "";
    }

    XClassHint hint;
    int hintStatus = XGetClassHint(disp, *((Window *)prop), &hint);

    if (hintStatus == 0)
    {
        return "";
    }

    XFree(hint.res_name);
    string s(hint.res_class);
    XFree(hint.res_class);

    return s;
}
static unsigned long long lastTotalUser, lastTotalUserLow, lastTotalSys, lastTotalIdle;

void getLast()
{
    FILE *file = fopen("/proc/stat", "r");
    fscanf(file, "cpu %llu %llu %llu %llu", &lastTotalUser, &lastTotalUserLow,
           &lastTotalSys, &lastTotalIdle);
    fclose(file);
}

double getCPU()
{
    getLast();
    sleep(1);
    double percent;
    FILE *file;
    unsigned long long totalUser, totalUserLow, totalSys, totalIdle, total;

    file = fopen("/proc/stat", "r");
    fscanf(file, "cpu %llu %llu %llu %llu", &totalUser, &totalUserLow,
           &totalSys, &totalIdle);
    fclose(file);

    if (totalUser < lastTotalUser || totalUserLow < lastTotalUserLow ||
        totalSys < lastTotalSys || totalIdle < lastTotalIdle)
    {
        // Overflow detection. Just skip this value.
        percent = -1.0;
    }
    else
    {
        total = (totalUser - lastTotalUser) + (totalUserLow - lastTotalUserLow) +
                (totalSys - lastTotalSys);
        percent = total;
        total += (totalIdle - lastTotalIdle);
        percent /= total;
        percent *= 100;
    }

    lastTotalUser = totalUser;
    lastTotalUserLow = totalUserLow;
    lastTotalSys = totalSys;
    lastTotalIdle = totalIdle;

    return percent;
}

bool processRunning(string name, bool ignoreCase = true)
{
    string strReg = name; // Match the name anywhere
    regex nameRegex(ignoreCase ? regex(strReg, regex::icase) : regex(strReg));
    log("Checking for process: " + name, LogType::DEBUG);

    for (const auto &entry : fs::directory_iterator("/proc"))
    {
        if (fs::is_directory(entry.path()))
        {
            ifstream s(entry.path() / "cmdline");
            if (s.is_open())
            {
                string line((istreambuf_iterator<char>(s)), istreambuf_iterator<char>());
                replace(line.begin(), line.end(), '\0', ' '); // Replace null characters with spaces
                log("Cmdline content for PID " + entry.path().filename().string() + ": " + line, LogType::DEBUG); // Log the cmdline content
                if (regex_search(line, nameRegex))
                {
                    log("Found process: " + name + " (cmdline: " + line + ")", LogType::DEBUG);
                    return true;
                }
            }
        }
    }
    log("Process not found: " + name, LogType::DEBUG);
    return false;
}

bool in_array(const string &value, const vector<string> &array)
{
    return find(array.begin(), array.end(), value) != array.end();
}

void parseConfigOption(Config *config, char *option, bool arg)
{
    smatch matcher;
    string s = option;

    if (arg)
    {
        if (s == "-h" || s == "--help")
        {
            config->printHelp = true;
            return;
        }

        if (s == "-v" || s == "--version")
        {
            config->printVersion = true;
            return;
        }

        if (s == "--debug")
        {
            config->debug = true;
            return;
        }

        if (!strncmp(option, "--", 2))
        {
            s = s.substr(2, s.size() - 2);
        }
    }

    if (s == "ignore-discord")
    {
        config->ignoreDiscord = true;
        return;
    }

    if (s == "no-small-image")
    {
        config->noSmallImage = true;
        return;
    }

    if (regex_search(s, matcher, usageRegex))
    {
        config->usageSleep = stoi(matcher[1]);
        return;
    }

    if (regex_search(s, matcher, updateRegex))
    {
        config->updateSleep = stoi(matcher[1]);
        return;
    }
}

void parseConfig(string configFile, Config *config)
{
    ifstream file(configFile);
    if (file.is_open())
    {
        string line;
        while (getline(file, line))
        {
            parseConfigOption(config, (char *)line.c_str(), false);
        }
        file.close();
    }
}

/**
 * @brief Parse default configs
 * /etc/brpc/config < ~/.config/brpc/config
 */
void parseConfigs()
{
    char *home = getenv("HOME");
    if (!home)
    {
        parseConfig("/etc/brpc/config", &config);
        return;
    }

    string configFile = string(home) + "/.config/brpc/config";
    parseConfig(configFile, &config);
    if (ifstream(configFile).fail())
    {
        parseConfig("/etc/brpc/config", &config);
    }
}

void parseArgs(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        parseConfigOption(&config, argv[i], true);
    }
}

string getDistro()
{
    string distro = "";
    string line;
    ifstream release;
    regex distroreg;
    smatch distromatcher;

    if (fs::exists("/etc/lsb-release"))
    {
        distroreg = regex("DISTRIB_ID=\"?([a-zA-Z0-9 ]+)\"?");
        release.open("/etc/lsb-release");
    }
    else if (fs::exists("/etc/os-release"))
    {
        distroreg = regex("NAME=\"?([a-zA-Z0-9 ]+)\"?");
        release.open("/etc/os-release");
    }
    else
    {
        log("Warning: Neither /etc/lsb-release nor /etc/os-release was found. Please install lsb_release or ask your distribution's developer to support os-release.", LogType::DEBUG);
        return "Linux";
    }

    while (getline(release, line))
    {
        if (regex_search(line, distromatcher, distroreg))
        {
            distro = distromatcher[1];
            break;
        }
    }

    return distro;
}

WindowAsset getWindowAsset(string w)
{
    WindowAsset window{};
    window.text = w;
    if (w == "")
    {
        window.image = "";
        return window;
    }
    window.image = "file";
    w = lower(w);

    if (in_array(w, apps))
    {
        window.image = w;
    }
    else
    {
        for (const auto &kv : aliases_regex)
        {
            regex r = kv.first;
            smatch m;
            if (regex_match(w, m, r))
            {
                window.image = kv.second;
                break;
            }
        }
    }

    return window;
}

DistroAsset getDistroAsset(string d)
{
    DistroAsset dist{};
    dist.text = d + " / Better-RPC++ " + VERSION;
    dist.image = "tux";

    for (const auto &kv : distros_lsb_regex)
    {
        regex r = kv.first;
        smatch m;
        if (regex_match(d, m, r))
        {
            dist.image = kv.second;
            break;
        }
    }
    if (dist.image == "tux")
    {
        for (const auto &kv : distros_os_regex)
        {
            regex r = kv.first;
            smatch m;
            if (regex_match(d, m, r))
            {
                dist.image = kv.second;
                break;
            }
        }
    }

    return dist;
}

/**
 * @brief Compile strings to regular expressions
 */
void compileRegexes(map<string, string> *from, vector<pair<regex, string>> *to, bool ignoreCase)
{

    for (const auto &kv : *from)
    {
        const regex r = regex(kv.first);
        to->push_back({r, kv.second});
    }
}

/**
 * @brief Compile all strings to regular expressions
 */
void compileAllRegexes()
{
    compileRegexes(&aliases, &aliases_regex, false);
    compileRegexes(&distros_lsb, &distros_lsb_regex, true);
    compileRegexes(&distros_os, &distros_os_regex, true);
}
