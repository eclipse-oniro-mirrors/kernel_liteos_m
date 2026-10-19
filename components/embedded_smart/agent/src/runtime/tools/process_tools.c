#include "../builtin_tools_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void close_child_descriptors(void) {
#ifdef SYS_close_range
    if (!syscall(SYS_close_range, 3u, ~0u, 0u))
        return;
#endif
    long limit = sysconf(_SC_OPEN_MAX);
    if (limit < 0 || limit > 65536)
        limit = 65536;
    for (int descriptor = 3; descriptor < limit; descriptor++)
        close(descriptor);
}
static void set_child_limit(int resource, rlim_t value) {
    struct rlimit limit = {.rlim_cur = value, .rlim_max = value};
    if (setrlimit(resource, &limit))
        _exit(126);
}
int CrabBuiltinExecProgram(CrabRuntime* r, const CrabToolCall* c, CrabRawResult* raw) {
    const char *command = CrabToolArgsGetString(c, "command", NULL),
               *requestedPath = CrabToolArgsGetString(c, "path", NULL);
    const CrabToolStringArray* args = CrabToolArgsGetStringArray(c, "args");
    if (!command || !*command || command[0] == '/' || !args)
        return CrabBuiltinRawError(raw, CRAB_ERROR_INVALID_ARG, "invalid command or arguments");

    char normalized[PATH_MAX], workingPath[PATH_MAX], executable[PATH_MAX], workspace[PATH_MAX];
    if (CrabBuiltinNormalizePath(r, requestedPath, normalized, sizeof normalized) ||
        !realpath(normalized, workingPath) || !realpath(CrabRuntimeGetWorkspaceRoot(r), workspace))
        return CrabBuiltinRawError(raw, CRAB_ERROR_INVALID_ARG, "invalid working path");
    size_t workspaceLen = strlen(workspace);
    if (strncmp(workingPath, workspace, workspaceLen) ||
        (workingPath[workspaceLen] && workingPath[workspaceLen] != '/'))
        return CrabBuiltinRawError(raw, CRAB_ERROR_PERMISSION, "working path is outside workspace");
    const char* commandBase = strchr(command, '/') ? workspace : workingPath;
    int written = snprintf(normalized, sizeof normalized, "%s/%s", commandBase, command);
    if (written < 0 || (size_t) written >= sizeof normalized || !realpath(normalized, executable) ||
        strncmp(executable, workspace, workspaceLen) ||
        (executable[workspaceLen] && executable[workspaceLen] != '/'))
        return CrabBuiltinRawError(raw, CRAB_ERROR_PERMISSION, "executable is outside workspace");
    struct stat st;
    if (stat(executable, &st) || !S_ISREG(st.st_mode) || access(executable, X_OK))
        return CrabBuiltinRawError(raw, CRAB_ERROR_INVALID_ARG, "command is not an executable file");

    char* argv[CRAB_TOOL_ARRAY_MAX_ITEMS + 2];
    argv[0] = executable;
    for (size_t i = 0; i < args->count; i++)
        argv[i + 1] = (char*) args->items[i];
    argv[args->count + 1] = NULL;

    char userEnv[384], passwordEnv[384];
    const char *user = getenv("LITECRAB_ALARM_USER"), *password = getenv("LITECRAB_ALARM_PASSWORD");
    char* envp[4];
    int envCount = 0;
    envp[envCount++] = "LANG=C.UTF-8";
    if (user && *user) {
        if (snprintf(userEnv, sizeof userEnv, "LITECRAB_ALARM_USER=%s", user) >=
            (int) sizeof userEnv)
            return CrabBuiltinRawError(raw, CRAB_ERROR_INVALID_ARG, "alarm user environment is too long");
        envp[envCount++] = userEnv;
    }
    if (password && *password) {
        if (snprintf(passwordEnv, sizeof passwordEnv, "LITECRAB_ALARM_PASSWORD=%s", password) >=
            (int) sizeof passwordEnv)
            return CrabBuiltinRawError(raw, CRAB_ERROR_INVALID_ARG, "alarm password environment is too long");
        envp[envCount++] = passwordEnv;
    }
    envp[envCount] = NULL;

    int fd[2];
    if (pipe(fd))
        return CrabBuiltinRawError(raw, -errno, "pipe failed");
    pid_t pid = fork();
    if (pid < 0) {
        close(fd[0]); close(fd[1]);
        return CrabBuiltinRawError(raw, -errno, "fork failed");
    }
    if (!pid) {
        setpgid(0, 0);
        set_child_limit(RLIMIT_CPU, 15);
        set_child_limit(RLIMIT_AS, 64U * 1024U * 1024U);
        set_child_limit(RLIMIT_NOFILE, 32);
        /* RLIMIT_NPROC is per real user, not per Tool process tree. Applying
         * a tiny value here can prevent legitimate children when LiteCrab
         * shares its board account with other services. Process-tree cleanup
         * is enforced through the dedicated process group and wall deadline. */
        set_child_limit(RLIMIT_FSIZE, 4U * 1024U * 1024U);
        close(fd[0]);
        int nullFd = open("/dev/null", O_RDONLY);
        if (nullFd < 0 || dup2(nullFd, STDIN_FILENO) < 0)
            _exit(126);
        if (nullFd != STDIN_FILENO)
            close(nullFd);
        dup2(fd[1], STDOUT_FILENO);
        dup2(fd[1], STDERR_FILENO);
        close(fd[1]);
        close_child_descriptors();
        if (chdir(workingPath))
            _exit(126);
        execve(executable, argv, envp);
        _exit(127);
    }
    setpgid(pid, pid);
    close(fd[1]);
    fcntl(fd[0], F_SETFL, fcntl(fd[0], F_GETFL, 0) | O_NONBLOCK);
    size_t cap = 256 * 1024, used = 0;
    char* output = malloc(cap + 1);
    if (!output) {
        kill(-pid, SIGKILL); close(fd[0]); waitpid(pid, NULL, 0);
        return CrabBuiltinRawError(raw, CRAB_ERROR_NO_MEMORY, "out of memory");
    }
    int timeout = (int) CrabToolArgsGetInt(c, "timeoutMs", 30000), status = 0, done = 0;
    if (timeout < 1 || timeout > 120000)
        timeout = 30000;
    int64_t deadline = CrabBuiltinMonotonicMs() + timeout;
    while (!done) {
        struct pollfd pollfd = {.fd = fd[0], .events = POLLIN | POLLHUP};
        int remaining = (int) (deadline - CrabBuiltinMonotonicMs());
        if (remaining <= 0) {
            kill(-pid, SIGTERM);
            poll(&pollfd, 1, 200);
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
            raw->runtimeTruncated = 1;
            raw->exitCode = 124;
            done = 1;
            continue;
        }
        poll(&pollfd, 1, remaining > 100 ? 100 : remaining);
        for (;;) {
            ssize_t got = read(fd[0], output + used, cap - used);
            if (got > 0) used += (size_t) got;
            else break;
            if (used == cap) {
                raw->runtimeTruncated = 1;
                kill(-pid, SIGKILL);
                waitpid(pid, &status, 0);
                raw->exitCode = 125;
                done = 1;
                break;
            }
        }
        if (!done && waitpid(pid, &status, WNOHANG) == pid) {
            raw->exitCode = WIFEXITED(status) ? WEXITSTATUS(status) :
                            WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
            done = 1;
        }
    }
    close(fd[0]);
    output[used] = 0;
    CrabRawResultSetStdout(raw, output);
    if (raw->exitCode)
        CrabRawResultSetStderr(raw, raw->exitCode == 124 ? "program timed out" : "program failed");
    free(output);
    return raw->exitCode;
}
int CrabBuiltinShell(CrabRuntime* r, const CrabToolCall* c, CrabRawResult* raw) {
    const char* s = CrabToolArgsGetString(c, "script", NULL);
    char path[PATH_MAX];
    if (!s || !*s)
        return CrabBuiltinRawError(raw, CRAB_ERROR_INVALID_ARG, "empty script");
    if (strlen(s) > 4096)
        return CrabBuiltinRawError(raw, CRAB_ERROR_INVALID_ARG, "script too long");
    if (CrabBuiltinNormalizePath(r, CrabToolArgsGetString(c, "path", "."), path, sizeof path))
        return CrabBuiltinRawError(raw, CRAB_ERROR_INVALID_ARG, "invalid working path");
    int fd[2];
    if (pipe(fd))
        return CrabBuiltinRawError(raw, -errno, "pipe failed");
    pid_t pid = fork();
    if (pid < 0) {
        close(fd[0]);
        close(fd[1]);
        return CrabBuiltinRawError(raw, -errno, "fork failed");
    }
    if (!pid) {
        close(fd[0]);
        dup2(fd[1], 1);
        dup2(fd[1], 2);
        close(fd[1]);
        if (chdir(path))
            _exit(126);
        execlp("sh", "sh", "-c", s, (char*) NULL);
        _exit(127);
    }
    close(fd[1]);
    size_t cap = 131072, n = 0;
    char* out = malloc(cap);
    if (!out) {
        close(fd[0]);
        waitpid(pid, NULL, 0);
        return CrabBuiltinRawError(raw, CRAB_ERROR_NO_MEMORY, "out of memory");
    }
    ssize_t got;
    while ((got = read(fd[0], out + n, cap - n - 1)) > 0) {
        n += (size_t) got;
        if (cap - n < 4096) {
            if (cap >= 1024 * 1024) {
                raw->runtimeTruncated = 1;
                break;
            }
            size_t nc = cap * 2;
            char* q = realloc(out, nc);
            if (!q)
                break;
            out = q;
            cap = nc;
        }
    }
    close(fd[0]);
    int st;
    waitpid(pid, &st, 0);
    out[n] = 0;
    raw->exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : WIFSIGNALED(st) ? 128 + WTERMSIG(st) : 1;
    CrabRawResultSetStdout(raw, out);
    if (raw->exitCode)
        CrabRawResultSetStderr(raw, "shell command failed");
    free(out);
    return raw->exitCode;
}
