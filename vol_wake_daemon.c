#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>

#include <asm/bitsperlong.h>

#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>

#include <linux/input.h>
#include <linux/ioprio.h>
#include <linux/sched/types.h>

#include "BinderGlue.h"
#include "IsInteractive.h"

static int g_verbose   = 0;
static int g_foreground = 0;

static char g_vol_dev[PATH_MAX];
static volatile sig_atomic_t g_running = 1;
static int g_singleton_fd = -1;

#define log_msg(...) do { if (g_foreground) __log_msg(__VA_ARGS__); } while (0)
#define log_verbose(...) do { if (g_verbose && g_foreground) __log_msg(__VA_ARGS__); } while (0)

static void __log_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

static void on_signal(__unused const int sig)
{
    g_running = 0;
}

#define KEYBITS_WORDS howmany(KEY_MAX + 1, __BITS_PER_LONG)

static int count_bits_set(const unsigned long *bits, const size_t nwords)
{
    int count = 0;
    for (size_t i = 0; i < nwords; ++i)
        count += __builtin_popcountl(bits[i]);
    return count;
}

static int open_volume_key_device(const char *vol_name)
{
    const char *device_path = "/dev/input";

    char *filename;
    DIR *dir;
    struct dirent *de;

    char name[80];
    int best_fd = -1, best_count = -1;
    unsigned long keybits[KEYBITS_WORDS];
    char best_path[sizeof(g_vol_dev)];

    if (!(dir = opendir(device_path)))
        return -1;

    if (vol_name)
        name[sizeof(name) - 1] = '\0';

    strlcpy(g_vol_dev, device_path, sizeof(g_vol_dev));
    filename = g_vol_dev + strlen(device_path);
    *filename++ = '/';
    while ((de = readdir(dir))) {
        if (de->d_name[0] == '.' &&
           (de->d_name[1] == '\0' ||
            (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;
        
        if (__predict_false(strncmp(de->d_name, "event", 5)))
            continue;

        strlcpy(filename, de->d_name, sizeof(g_vol_dev) - strlen(device_path) - 1);

        const int fd = open(g_vol_dev, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            log_verbose("open(%s) for reading failed: %s", g_vol_dev, strerror(errno));
            continue;
        }

        if (vol_name) {
            if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 1 && !strcmp(name, vol_name)) {
                best_fd = fd;
                break;
            }
        } else {
            memset(keybits, 0, sizeof(keybits));
            if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0) {
                const int has_volup = (keybits[KEY_VOLUMEUP / __BITS_PER_LONG] >>
                                        (KEY_VOLUMEUP % __BITS_PER_LONG)) & 1;
                if (has_volup) {
                    const int n = count_bits_set(keybits, KEYBITS_WORDS);
                    log_verbose("%s supports KEY_VOLUMEUP, %d total keys", g_vol_dev, n);
                    if (best_count < 0 || n < best_count) {
                        best_count = n;
                        if (best_fd != -1) close(best_fd);
                        best_fd = fd;
                        strlcpy(best_path, g_vol_dev, sizeof(best_path));
                        continue;
                    }
                }
            }
        }

        close(fd);
    }

    if (!vol_name && best_fd != -1)
        strlcpy(g_vol_dev, best_path, sizeof(g_vol_dev));

    closedir(dir);
    return best_fd;
}

static void apply_low_priority(void)
{
    if (setpriority(PRIO_PROCESS, 0, 19) < 0)
        log_verbose("setpriority failed: %s", strerror(errno));

    struct sched_param sp = { .sched_priority = 0 };
    if (__predict_false(sched_setscheduler(0, SCHED_IDLE, &sp) < 0)) {
        log_verbose("sched_setscheduler(SCHED_IDLE) failed, trying SCHED_BATCH: %s", strerror(errno));
        sched_setscheduler(0, SCHED_BATCH, &sp);
    }

    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(0, &cpu_set);
    CPU_SET(1, &cpu_set);
    if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set) < 0)
        log_verbose("sched_setaffinity failed: %s", strerror(errno));

    if (syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, 0, IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0)) < 0) {
        log_verbose("ioprio_set failed, trying best effort: %s", strerror(errno));
        syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, 0, IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 7));
    }

    if (prctl(PR_SET_TIMERSLACK, 40000000UL, 0, 0, 0) < 0)
        log_verbose("prctl(PR_SET_TIMERSLACK) failed: %s", strerror(errno));

#if 0
    if (access("/proc/sys/kernel/sched_util_clamp_min", F_OK) == 0) {
        struct sched_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.size = sizeof(attr);
        attr.sched_flags = SCHED_FLAG_UTIL_CLAMP | SCHED_FLAG_KEEP_ALL;
        attr.sched_util_min = 0;   // boost = 0
        attr.sched_util_max = 307; // ~30% of 1024, matches PerfClamp
        if (syscall(SYS_sched_setattr, 0 /* self */, &attr, 0) < 0)
            log_verbose("sched_setattr(uclamp.max) unavailable: %s", strerror(errno));
    }
#endif
}

static int is_screen_on(void)
{
    const int interactive = IsInteractive();
    if (__predict_true(interactive != -1))
        return interactive;

    log_verbose("IsInteractive() unavailable; assuming screen may be off");
    return 0;
}

static int acquire_singleton_lock(const char *name)
{
    const size_t name_len = strlen(name);
    struct sockaddr_un addr;

    if (__predict_false(name_len > sizeof(addr.sun_path) - 1)) {
        log_verbose("singleton lock name too long");
        return -1;
    }

    const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        __log_msg("socket() for singleton lock failed: %s", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path + 1, name, name_len);
    const socklen_t addr_len =
        (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + name_len);

    if (bind(fd, (struct sockaddr *)&addr, addr_len) < 0) {
        if (errno == EADDRINUSE)
            __log_msg("another instance is already running");
        else
            __log_msg("bind() for singleton lock failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static void daemonise()
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        const rlim_t max_fd = (rl.rlim_max == RLIM_INFINITY) ? 1024 : rl.rlim_max;
        for (rlim_t i = STDERR_FILENO + 1; i < max_fd; ++i) {
            if ((int)i == g_singleton_fd)
                continue;
            close((int)i);
        }
    }

    for (int i = 1; i < _NSIG; ++i)
        signal(i, SIG_DFL);
    sigset_t empty_set;
    sigemptyset(&empty_set);
    sigprocmask(SIG_SETMASK, &empty_set, NULL);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
    if (pid > 0) _exit(EXIT_SUCCESS);

    if (setsid() < 0) { perror("setsid"); exit(EXIT_FAILURE); }
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) _exit(EXIT_SUCCESS);

    umask(0);

    const int devnull = open("/dev/null", O_RDWR);
    if (devnull < 0) exit(EXIT_FAILURE);
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    if (devnull > STDERR_FILENO) close(devnull);

    if (chdir("/") < 0) exit(EXIT_FAILURE);
}

static void usage(const char *argv0)
{
    __log_msg(
        "usage: %s [-f] [-v] [--vol-name NAME]\n"
        "  -f              stay in foreground, log to stderr (default: daemonise)\n"
        "  -v              verbose logging\n"
        "  --vol-name      evdev name (not path) for the volume keys (default: auto-detect by\n"
        "                  finding the KEY_VOLUMEUP-supporting device with the fewest keys)",
        argv0 ? basename(argv0) : "");
}

int main(int argc, char **argv)
{
    char *vol_name = NULL;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-f") == 0) {
            g_foreground = 1;
        } else if (strcmp(arg, "-v") == 0) {
            g_verbose = 1;
        } else if (strcmp(arg, "--vol-name") == 0) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            vol_name = argv[i];
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        } else {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if ((g_singleton_fd = acquire_singleton_lock("vol_wake_daemon")) < 0)
        return EXIT_FAILURE;

    if (!g_foreground) {
        apply_low_priority();
        daemonise();
    }

    const int vol_fd = open_volume_key_device(vol_name);
    if (vol_fd == -1) {
        if (!vol_name)
            log_msg("no evdev device advertises KEY_VOLUMEUP support; pass --vol-name explicitly");
        else
            log_msg("could not find an input device matching \"%s\"", vol_name);
        return EXIT_FAILURE;
    }

    const int binder_fd = SetupBinderOrCrash();

    log_verbose("vol=%s pid=%d", g_vol_dev, (int)getpid());

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    struct pollfd pfds[2];
    pfds[0].fd = vol_fd;    pfds[0].events = POLLIN; pfds[0].revents = 0;
    pfds[1].fd = binder_fd; pfds[1].events = POLLIN; pfds[1].revents = 0;

    int ret = EXIT_SUCCESS;
    while (g_running) {
        const int nready = poll(pfds, 2, -1);

        if (__predict_false(nready < 0)) {
            if (errno == EINTR) continue;
            log_msg("poll failed, exiting: %s", strerror(errno));
            ret = EXIT_FAILURE;
            break;
        }

        if (__predict_false(pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
            log_msg("vol_fd error (revents=0x%x), exiting", pfds[0].revents);
            ret = EXIT_FAILURE;
            break;
        }

        if (__predict_false(pfds[1].revents & POLLIN))
            OnBinderReadReady();

        if (__predict_true(pfds[0].revents & POLLIN)) {
            struct input_event ev;
            ssize_t n;
            while (__predict_true((n = read(vol_fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev))) {
                if (ev.type == EV_KEY && ev.code == KEY_VOLUMEUP && ev.value == 1) {
                    if (is_screen_on()) {
                        log_verbose("volume-up down: screen already on, skipping wake");
                    } else {
                        wakeUpScreen();
                        log_msg("volume-up down: waking screen");
                    }
                }
            }

            if (__predict_false(n == 0)) {
                log_msg("vol_fd hit EOF, exiting");
                ret = EXIT_FAILURE;
                break;
            }

            if (__predict_false(n < 0 && errno != EAGAIN)) {
                log_msg("read failed, exiting: %s", strerror(errno));
                ret = EXIT_FAILURE;
                break;
            }
        }
    }

    log_verbose("exiting");
    close(vol_fd);
    return ret;
}
