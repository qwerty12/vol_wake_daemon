#define _GNU_SOURCE 1

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
#include <sys/wait.h>

#include <linux/input.h>
#include <linux/ioprio.h>
#include <linux/uinput.h>
#include <linux/sched/types.h>

#include "binder_glue.h"
#include "is_interactive.h"

#include "cleanup.h"
typedef int fd_t;
DEFINE_AUTOVAL_CLEANUP(fd_t, close, -1)

typedef int uinput_fd_t;
static __always_inline inline void destroy_uinput(const uinput_fd_t fd) { ioctl(fd, UI_DEV_DESTROY); close(fd); }
DEFINE_AUTOVAL_CLEANUP(uinput_fd_t, destroy_uinput, -1)

#define SINGLETON_NAME "vol_wake_daemon#6CDB7CC6-4DAC-4fcf-B81B-48BCDAD85DED"

static int g_verbose   = 0;
static int g_foreground = 0;

static char g_vol_dev[PATH_MAX];
static volatile sig_atomic_t g_running = 1;

static const struct input_event g_wake_seq[] = {
    { .type = EV_KEY, .code = KEY_WAKEUP, .value = 1 },
    { .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
    { .type = EV_KEY, .code = KEY_WAKEUP, .value = 0 },
    { .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
};

#define log_msg(...) do { if (__predict_false(g_foreground)) __log_msg(__VA_ARGS__); } while (0)
#define log_verbose(...) do { if (__predict_false(g_verbose && g_foreground)) __log_msg(__VA_ARGS__); } while (0)

#ifdef HAVE_LOCAL_LIBBINDER_NDK
static
#else
__LIBC_HIDDEN__
#endif
__attribute__((noinline, cold)) __printflike(1, 2) void __log_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void on_signal(__unused const int sig)
{
    g_running = 0;
}

#define KEYBITS_WORDS howmany(KEY_MAX + 1, __BITS_PER_LONG)

static __attribute_pure__ __always_inline inline int count_bits_set(const unsigned long *restrict bits, const size_t nwords)
{
    register int count = 0;
    for (size_t i = 0; i < nwords; ++i)
        count += __builtin_popcountl(bits[i]);
    return count;
}

__attribute__((noinline)) static int open_volume_key_device(const char *restrict vol_name)
{
    const char *device_path = "/dev/input";

    char *filename;
    DIR *dir;
    struct dirent *de;

    int best_fd = -1, best_count = -1;
    char best_path[sizeof(g_vol_dev)];

    if (!(dir = opendir(device_path)))
        return -1;

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
            log_verbose("open(%s) for reading failed: %m", g_vol_dev);
            continue;
        }

        if (vol_name) {
            char name[80];
            name[sizeof(name) - 1] = '\0';
            if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 1 && strcmp(name, vol_name) == 0) {
                best_fd = fd;
                break;
            }
        } else {
            unsigned long keybits[KEYBITS_WORDS] = { 0 };
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

static void parse_cpuset_cpus(char *restrict cpus, cpu_set_t *restrict cpu_set)
{
    /* Copyright 2006, The Android Open Source Project
     * Licensed under the Apache License, Version 2.0 */
    char *saveptr;
    char *cpu_range = strtok_r(cpus, ",", &saveptr);

    while (cpu_range) {
        unsigned int start = 0, end = 0;
        const int matched = sscanf(cpu_range, "%u-%u", &start, &end);

        if (start >= CPU_SETSIZE) {
            log_verbose("parse_cpuset_cpus: ignoring CPU number %u >= %d", start, CPU_SETSIZE);
            goto advance;
        }

        if (matched == 1) {
            CPU_SET(start, cpu_set);
        } else if (matched == 2) {
            if (end >= CPU_SETSIZE)
                end = CPU_SETSIZE - 1;

            if (start > end) {
                const unsigned int tmp = start;
                start = end;
                end = tmp;
            }

            for (unsigned int i = start; i <= end; ++i)
                CPU_SET(i, cpu_set);
        } else {
            log_verbose("parse_cpuset_cpus: failed to match \"%s\"", cpu_range);
        }

    advance:
        cpu_range = strtok_r(NULL, ",", &saveptr);
    }
}

static void set_background_affinity(cpu_set_t *restrict cpu_set)
{
    CPU_ZERO(cpu_set);

    FILE *file = fopen("/dev/cpuset/background/cpus", "re");
    if (file) {
        char line[128];
        if (fgets(line, sizeof(line), file)) {
            const size_t len = strlen(line);
            if ((len > 0 && line[len - 1] == '\n') || fgetc(file) == EOF)
                parse_cpuset_cpus(line, cpu_set);
            else
                log_verbose("background cpuset line too long, ignoring");
        } else {
            log_verbose("failed to read background cpuset");
        }
        fclose(file);
    }

    if (CPU_COUNT(cpu_set) < 2) {
        CPU_ZERO(cpu_set);
        long num_cpus = sysconf(_SC_NPROCESSORS_CONF);
        if (__predict_false(num_cpus < 1))
            num_cpus = 1;

        for (long i = 0; i < num_cpus && i < 2; ++i)
            CPU_SET(i, cpu_set);
    }
}

__attribute__((noinline)) static void apply_low_priority(void)
{
    cpu_set_t cpu_set;
    set_background_affinity(&cpu_set);

    if (__predict_false(setpriority(PRIO_PROCESS, 0, 19) < 0))
        log_verbose("setpriority failed: %m");

    struct sched_param sp = { 0 };
    if (__predict_false(sched_setscheduler(0, SCHED_IDLE, &sp) < 0)) {
        log_verbose("sched_setscheduler(SCHED_IDLE) failed, trying SCHED_BATCH: %m");
        sched_setscheduler(0, SCHED_BATCH, &sp);
    }

    if (__predict_false(sched_setaffinity(0, sizeof(cpu_set), &cpu_set) < 0))
        log_verbose("sched_setaffinity failed: %m");

    if (__predict_false(syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, 0, IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0)) < 0)) {
        log_verbose("ioprio_set failed, trying best effort: %m");
        syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, 0, IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 7));
    }

    if (__predict_false(prctl(PR_SET_TIMERSLACK, 40000000UL, 0, 0, 0) < 0))
        log_verbose("prctl(PR_SET_TIMERSLACK) failed: %m");

#if 0
    if (access("/proc/sys/kernel/sched_util_clamp_min", F_OK) == 0) {
        struct sched_attr attr = { 0 };
        attr.size = sizeof(attr);
        attr.sched_flags = SCHED_FLAG_UTIL_CLAMP | SCHED_FLAG_KEEP_ALL;
        attr.sched_util_min = 0;   // boost = 0
        attr.sched_util_max = 307; // ~30% of 1024, matches PerfClamp
        if (__predict_false(syscall(SYS_sched_setattr, 0, &attr, 0) < 0))
            log_verbose("sched_setattr(uclamp.max) unavailable: %m");
    }
#endif
}

__attribute__((noinline)) static int acquire_singleton_lock(void)
{
    const size_t name_len = sizeof(SINGLETON_NAME) - 1;

    _Static_assert(name_len <= sizeof(((struct sockaddr_un *)0)->sun_path) - 1, "singleton lock name too long");

    const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (__predict_false(fd < 0)) {
        __log_msg("socket() for singleton lock failed: %m");
        return -1;
    }

    struct sockaddr_un addr = { 0 };
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path + 1, SINGLETON_NAME, name_len);
    const socklen_t addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + name_len);

    if (bind(fd, (struct sockaddr *)&addr, addr_len) < 0) {
        __log_msg(errno == EADDRINUSE ? "another instance is already running" : "bind() for singleton lock failed: %m");
        close(fd);
        return -1;
    }

    return fd;
}

__attribute__((noinline)) static void daemonise(const int keep_fd)
{
    DIR *dir = opendir("/proc/self/fd");
    if (__predict_true(dir)) {
        const int dfd = dirfd(dir);
        for (struct dirent *ent; (ent = readdir(dir));) {
            if (ent->d_name[0] == '.')
                continue;
            const int fd = atoi(ent->d_name);
            if (fd > STDERR_FILENO && fd != keep_fd && fd != dfd)
                close(fd);
        }
        closedir(dir);
    } else {
        struct rlimit rl;
        rlim_t max_fd = 1024;
        if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY)
            max_fd = rl.rlim_cur;
        if (max_fd > INT_MAX)
            max_fd = INT_MAX;
        for (int fd = STDERR_FILENO + 1; fd < (int)max_fd; ++fd) {
            if (__predict_false(fd == keep_fd))
                continue;
            close(fd);
        }
    }

    for (int i = 1; i < NSIG; ++i) {
        /*struct sigaction sa;
        if (sigaction(i, NULL, &sa) == 0 && sa.sa_handler == SIG_IGN)*/
            signal(i, SIG_DFL);
    }
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    pid_t pid = fork();
    if (pid < 0) { __log_msg("fork: %m"); exit(EXIT_FAILURE); }
    if (pid > 0) {
        int st;
        while (waitpid(pid, &st, 0) < 0) {
            if (errno != EINTR)
                _exit(EXIT_FAILURE);
        }
        _exit(WIFEXITED(st) ? WEXITSTATUS(st) : EXIT_FAILURE);
    }

    if (setsid() < 0) { __log_msg("setsid: %m"); _exit(EXIT_FAILURE); }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    pid = fork();
    if (pid < 0) _exit(EXIT_FAILURE);
    if (pid > 0) _exit(EXIT_SUCCESS);

    umask(077);

    const int devnull = open("/dev/null", O_RDWR);
    if (devnull < 0) exit(EXIT_FAILURE);
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    if (__predict_false(devnull > STDERR_FILENO)) close(devnull);

    if (__predict_false(chdir("/") < 0)) exit(EXIT_FAILURE);

    sigset_t empty_set;
    sigemptyset(&empty_set);
    sigprocmask(SIG_SETMASK, &empty_set, NULL);
}

__attribute__((noinline)) static int uinput_init(const int allowed_keycode)
{
    const int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (__predict_false(fd < 0)) {
        log_msg("open /dev/uinput: %m");
        return -1;
    }

    if (__predict_false(
        ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, allowed_keycode) < 0
    )) {
        log_verbose("UI_SET_{EV,KEY}BIT: %m");
        close(fd);
        return -1;
    }

    struct uinput_setup usetup = { 0 };
    usetup.id.bustype = BUS_VIRTUAL;
    usetup.id.vendor = 0x7239;
    usetup.id.product = 0x3666;
    strlcpy(usetup.name, "vol_wake_daemon", sizeof(usetup.name));

    if (__predict_false(ioctl(fd, UI_DEV_SETUP, &usetup) < 0)) {
        log_msg("UI_DEV_SETUP: %m");
        close(fd);
        return -1;
    }

    if (__predict_false(ioctl(fd, UI_DEV_CREATE) < 0)) {
        log_msg("UI_DEV_CREATE: %m");
        close(fd);
        return -1;
    }

    usleep(150 * 1000);

    return fd;
}

static __always_inline inline void wakeup_screen(const int uinput_fd)
{
    write(uinput_fd, g_wake_seq, sizeof(g_wake_seq));
}

static __always_inline inline int is_screen_on(void)
{
    const int interactive = IsInteractive();
    if (__predict_true(interactive != -1))
        return interactive;

    log_verbose("IsInteractive() unavailable; assuming screen may be off");
    return 0;
}

static __noreturn __attribute__((noinline, cold))
void usage(const char *argv0, const int status)
{
    const char *name = __predict_true(argv0) ? basename(argv0) : "";
    __log_msg(
        "usage: %s [-f] [-v] [--vol-name NAME]\n"
        "  -f              stay in foreground, log to stderr (default: daemonise)\n"
        "  -v              verbose logging\n"
        "  --vol-name      evdev name (not path) for the volume keys (default: auto-detect by\n"
        "                  finding the KEY_VOLUMEUP-supporting device with the fewest keys)",
        name);
    exit(status);
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
            if (++i >= argc) usage(argv[0], EXIT_FAILURE);
            vol_name = argv[i];
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(argv[0], EXIT_SUCCESS);
        } else {
            usage(argv[0], EXIT_FAILURE);
        }
    }

    const autoval(fd_t) singleton_fd = acquire_singleton_lock();
    if (singleton_fd < 0)
        return EXIT_FAILURE;

    if (__predict_true(!g_foreground)) {
        apply_low_priority();
        daemonise(singleton_fd);
    }

    const autoval(fd_t) vol_fd = open_volume_key_device(vol_name);
    if (vol_fd < 0) {
        if (!vol_name)
            log_msg("no evdev device advertises KEY_VOLUMEUP support; pass --vol-name explicitly");
        else
            log_msg("could not find an input device matching \"%s\"", vol_name);
        return EXIT_FAILURE;
    }

    const autoval(fd_t) binder_fd = SetupBinder();
    if (__predict_false(binder_fd < 0)) {
        if (__predict_true(binder_fd != -1))
            log_msg("error setting up Binder polling: %s", strerror(-binder_fd));
        else
            log_msg("invalid Binder FD (or maybe EPERM)");
        return EXIT_FAILURE;
    }

    if (__predict_false(!ConnectPowerService())) {
        log_msg("failed to connect to the power service via Binder");
        return EXIT_FAILURE;
    }

    const autoval(uinput_fd_t) uinput_fd = uinput_init(KEY_WAKEUP);
    if (__predict_false(uinput_fd < 0))
        return EXIT_FAILURE;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    log_verbose("vol=%s pid=%ld", g_vol_dev, (long)getpid());

    struct pollfd pfds[2];
    pfds[0].fd = vol_fd;    pfds[0].events = POLLIN; pfds[0].revents = 0;
    pfds[1].fd = binder_fd; pfds[1].events = POLLIN; pfds[1].revents = 0;

    while (g_running) {
        const int nready = poll(pfds, 2, -1);

        if (__predict_false(nready < 0)) {
            if (errno == EINTR) continue;
            log_msg("poll failed, exiting: %m");
            return EXIT_FAILURE;
        }

        if (__predict_false(pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
            log_msg("vol_fd error (revents=0x%x), exiting", pfds[0].revents);
            return EXIT_FAILURE;
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
                        wakeup_screen(uinput_fd);
                        log_msg("volume-up down: waking screen");
                    }
                }
            }

            if (__predict_false(n == 0)) {
                log_msg("vol_fd hit EOF, exiting");
                return EXIT_FAILURE;
            }

            if (__predict_false(n < 0 && errno != EAGAIN)) {
                log_msg("read failed, exiting: %m");
                return EXIT_FAILURE;
            }
        }
    }

    log_verbose("exiting");
    return EXIT_SUCCESS;
}
