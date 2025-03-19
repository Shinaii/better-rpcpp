#include "brpcpp.hpp"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#define PID_FILE "/tmp/brpc.pid"

void *updateRPC(void *ptr)
{
    string windowName, lastWindow;
    WindowAsset windowAsset;
    DistroAsset distroAsset;
    DiscordState *state = (struct DiscordState *)ptr;

    log("Waiting for usages to load...", LogType::DEBUG);

    // wait for usages to load
    while (cpu == -1 || mem == -1)
    {
        usleep(1000);
    }

    log("Starting RPC loop.", LogType::DEBUG);
    distroAsset = getDistroAsset(distro);

    while (true)
    {
        string cpupercent = to_string((long)cpu);
        string rampercent = to_string((long)mem);

        usleep(config.updateSleep * 1000);

        if (!config.noSmallImage)
        {
            try
            {
                windowName = getActiveWindowClassName(disp);
            }
            catch (exception ex)
            {
                log(ex.what(), LogType::ERROR);
                continue;
            }

            if (windowName != lastWindow)
            {
                windowAsset = getWindowAsset(windowName);
                lastWindow = windowName;
            }
        }

        setActivity(*state, string("CPU: " + cpupercent + "% | RAM: " + rampercent + "%"), "WM: " + wm, windowAsset.image, windowAsset.text, distroAsset.image, distroAsset.text, startTime, discord::ActivityType::Playing);
    }
}

void *updateUsage(void *ptr)
{
    distro = getDistro();
    log("Distro: " + distro, LogType::DEBUG);

    startTime = time(0) - ms_uptime();
    wm = string(wm_info(disp));
    log("WM: " + wm, LogType::DEBUG);

    while (true)
    {
        mem = getRAM();
        cpu = getCPU();
        sleep(config.usageSleep / 1000.0);
    }
}

void writePidFile()
{
    std::ofstream pidFile(PID_FILE);
    if (pidFile.is_open())
    {
        pidFile << getpid();
        pidFile.close();
    }
    else
    {
        std::cerr << "Failed to write PID file." << std::endl;
        exit(1);
    }
}

bool isProcessRunning()
{
    std::ifstream pidFile(PID_FILE);
    if (pidFile.is_open())
    {
        pid_t pid;
        pidFile >> pid;
        pidFile.close();

        if (kill(pid, 0) == 0)
        {
            return true;
        }
        else
        {
            remove(PID_FILE);
        }
    }
    return false;
}

void killRunningProcess()
{
    std::ifstream pidFile(PID_FILE);
    if (pidFile.is_open())
    {
        pid_t pid;
        pidFile >> pid;
        pidFile.close();

        if (kill(pid, SIGTERM) == 0)
        {
            std::cout << "Killed running brpc process (PID: " << pid << ")." << std::endl;
            remove(PID_FILE);
        }
        else
        {
            std::cerr << "Failed to kill process (PID: " << pid << ")." << std::endl;
        }
    }
    else
    {
        std::cout << "No running brpc process found." << std::endl;
    }
}

void daemonize()
{
    pid_t pid = fork();

    if (pid < 0)
    {
        std::cerr << "Failed to fork process." << std::endl;
        exit(1);
    }

    if (pid > 0)
    {
        // Parent process exits
        exit(0);
    }

    // Child process continues
    if (setsid() < 0)
    {
        std::cerr << "Failed to create new session." << std::endl;
        exit(1);
    }

    // Redirect standard file descriptors to /dev/null
    int devNull = open("/dev/null", O_RDWR);
    dup2(devNull, STDIN_FILENO);
    dup2(devNull, STDOUT_FILENO);
    dup2(devNull, STDERR_FILENO);
    close(devNull);
}

int main(int argc, char **argv)
{
    parseConfigs();
    parseArgs(argc, argv);

    if (argc > 1)
    {
        std::string arg = argv[1];
        if (arg == "-k" || arg == "--kill")
        {
            killRunningProcess();
            return 0;
        }
    }
    else
    {
        if (isProcessRunning())
        {
            std::cerr << "An instance of brpc is already running. Use `brpc -k` to kill it before starting a new one." << std::endl;
            return 1;
        }

        daemonize();
        writePidFile();
    }

    if (config.printHelp)
    {
        std::cout << helpMsg << std::endl;
        exit(0);
    }
    if (config.printVersion)
    {
        std::cout << "bRPC++ version " << VERSION << std::endl;
        exit(0);
    }

    int waitedTime = 0;
    while (!config.ignoreDiscord && !processRunning("vesktop") && !processRunning("discord"))
    {
        log("Checking processes: discord=" + std::to_string(processRunning("discord")) +
            ", vesktop=" + std::to_string(processRunning("vesktop")) +
            ", ignoreDiscord=" + std::to_string(config.ignoreDiscord), LogType::DEBUG);

        if (waitedTime > 20)
        {
            log(std::string("Neither Discord nor Vesktop is running. Maybe ignore Discord check with --ignore-discord or -f?"), LogType::INFO);
        }
        log("Waiting for Discord or Vesktop...", LogType::INFO);
        waitedTime += 5;
        sleep(5);
    }

    disp = XOpenDisplay(NULL);

    if (!disp)
    {
        std::cout << "Can't open display" << std::endl;
        return -1;
    }

    static int (*old_error_handler)(Display *, XErrorEvent *);
    trapped_error_code = 0;
    old_error_handler = XSetErrorHandler(error_handler);

    // Compile all regexes
    compileAllRegexes();

    pthread_t updateThread;
    pthread_t usageThread;
    pthread_create(&usageThread, 0, updateUsage, 0);
    log("Created usage thread", LogType::DEBUG);

    DiscordState state{};

    discord::Core *core{};
    auto result = discord::Core::Create(934099338374824007, DiscordCreateFlags_Default, &core); // change with your own app's id if you made one
    state.core.reset(core);
    if (!state.core)
    {
        std::cout << "Failed to instantiate discord core! (err " << static_cast<int>(result) << ")\n";
        exit(-1);
    }

    if (config.debug)
    {
        state.core->SetLogHook(
            discord::LogLevel::Debug, [](discord::LogLevel level, const char *message)
            { std::cerr << "Log(" << static_cast<uint32_t>(level) << "): " << message << "\n"; });
    }

    pthread_create(&updateThread, 0, updateRPC, ((void *)&state));
    log("Threads started.", LogType::DEBUG);
    log("Xorg version " + std::to_string(XProtocolVersion(disp)), LogType::DEBUG); // this is kinda dumb to do since it shouldn't be anything else other than 11, but whatever
    log("Connected to Discord.", LogType::INFO);

    signal(SIGINT, [](int)
           { interrupted = true; });

    do
    {
        state.core->RunCallbacks();

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    } while (!interrupted);

    std::cout << "Exiting..." << std::endl;

    XCloseDisplay(disp);

    pthread_kill(updateThread, 9);
    pthread_kill(usageThread, 9);

    remove(PID_FILE);

    return 0;
}