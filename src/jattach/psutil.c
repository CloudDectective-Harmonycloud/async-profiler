/*
 * Copyright 2021 Andrei Pangin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include "psutil.h"


// Less than MAX_PATH to leave some space for appending
char tmp_path[MAX_PATH - 100];
char agent_command[MAX_PATH];

// Called just once to fill in tmp_path buffer
void get_tmp_path(int pid) {
    // Try user-provided alternative path first
    const char* jattach_path = getenv("JATTACH_PATH");
    if (jattach_path != NULL && strlen(jattach_path) < sizeof(tmp_path)) {
        strcpy(tmp_path, jattach_path);
        return;
    }

    if (get_tmp_path_r(pid, tmp_path, sizeof(tmp_path)) != 0) {
        strcpy(tmp_path, "/tmp");
    }
}

static int check_tmpfile_exist(int pid, char* file_name) {
    char dst_path[100];
    snprintf(dst_path, sizeof(dst_path), "/proc/%d/root/tmp", pid);

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", dst_path, file_name);

    struct stat stats;
    return stat(path, &stats) == 0 ? 0 : -1;
}

static void copy_to_tmpfile(int pid, char* srcPath, char* file_name, char* newfile_name) {
    char src[MAX_PATH];
    snprintf(src, sizeof(src), "%s/%s", srcPath, file_name);

    char dst[MAX_PATH];
    snprintf(dst, sizeof(dst), "/proc/%d/root/tmp/%s", pid, newfile_name);

	int srcFd = open(src, O_RDONLY);
	if (srcFd < 0) {
		fprintf(stderr, "[x Open File] %s\n", src);
		return;
	}
	struct stat stats;
	stat(src, &stats);

	int dstFd = open(dst, O_WRONLY | O_CREAT, stats.st_mode);
	if (dstFd < 0) {
		fprintf(stderr, "[x Create File] %s\n", dst);
		return;
	}

    // copy_file_range() doesn't exist in older kernels, sendfile() no longer works in newer ones
    char buf[65536];
    ssize_t r;
    while ((r = read(srcFd, buf, sizeof(buf))) > 0) {
        ssize_t w = write(dstFd, buf, r);
        (void)w;
    }

	if (fchmod(dstFd, 0666) != 0) {
		fprintf(stderr, "[x Chmod File] %s\n", dst);
		return;
	}
}

void check_copy_agent(int pid, char* srcPath, char* agent_name, char* so_name, char* version, char* command) {
    // copy agent.jar to /tmp/agent.jar
    char agent_jar[30];
    snprintf(agent_jar, sizeof(agent_jar), "%s.jar", agent_name);

    if (check_tmpfile_exist(pid, agent_jar) != 0) {
        copy_to_tmpfile(pid, srcPath, agent_jar, agent_jar);
    }

    // copy libasyncProfiler.so to /tmp/libasyncProfiler-version.so
    char agent_so[30], replace_so[50];
    snprintf(agent_so, sizeof(agent_so), "%s.so", so_name);
    snprintf(replace_so, sizeof(replace_so), "%s-%s.so", so_name, version);

    if (check_tmpfile_exist(pid, replace_so) != 0) {
        copy_to_tmpfile(pid, srcPath, agent_so, replace_so);
    }
    snprintf(agent_command, sizeof(agent_command), "/tmp/%s.jar=%s,lib=/tmp/%s-%s.so", agent_name, command, so_name, version);
}

#ifdef __linux__

// The first line of /proc/pid/sched looks like
// java (1234, #threads: 12)
// where 1234 is the host PID (before Linux 4.1)
static int sched_get_host_pid(const char* path) {
    static char* line = NULL;
    size_t size;
    int result = -1;

    FILE* sched_file = fopen(path, "r");
    if (sched_file != NULL) {
        if (getline(&line, &size, sched_file) != -1) {
            char* c = strrchr(line, '(');
            if (c != NULL) {
                result = atoi(c + 1);
            }
        }
        fclose(sched_file);
    }

    return result;
}

// Linux kernels < 4.1 do not export NStgid field in /proc/pid/status.
// Fortunately, /proc/pid/sched in a container exposes a host PID,
// so the idea is to scan all container PIDs to find which one matches the host PID.
static int alt_lookup_nspid(int pid) {
    char path[300];
    snprintf(path, sizeof(path), "/proc/%d/ns/pid", pid);

    // Don't bother looking for container PID if we are already in the same PID namespace
    struct stat oldns_stat, newns_stat;
    if (stat("/proc/self/ns/pid", &oldns_stat) == 0 && stat(path, &newns_stat) == 0) {
        if (oldns_stat.st_ino == newns_stat.st_ino) {
            return pid;
        }
    }

    // Otherwise browse all PIDs in the namespace of the target process
    // trying to find which one corresponds to the host PID
    snprintf(path, sizeof(path), "/proc/%d/root/proc", pid);
    DIR* dir = opendir(path);
    if (dir != NULL) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] >= '1' && entry->d_name[0] <= '9') {
                // Check if /proc/<container-pid>/sched points back to <host-pid>
                snprintf(path, sizeof(path), "/proc/%d/root/proc/%s/sched", pid, entry->d_name);
                if (sched_get_host_pid(path) == pid) {
                    closedir(dir);
                    return atoi(entry->d_name);
                }
            }
        }
        closedir(dir);
    }

    // Could not find container pid; return host pid as the last resort
    return pid;
}

int get_tmp_path_r(int pid, char* buf, size_t bufsize) {
    if (snprintf(buf, bufsize, "/proc/%d/root/tmp", pid) >= bufsize) {
        return -1;
    }

    // Check if the remote /tmp can be accessed via /proc/[pid]/root
    struct stat stats;
    return stat(buf, &stats);
}

int get_process_info(int pid, uid_t* uid, gid_t* gid, int* nspid) {
    // Parse /proc/pid/status to find process credentials
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE* status_file = fopen(path, "r");
    if (status_file == NULL) {
        return -1;
    }

    char* line = NULL;
    size_t size;
    int nspid_found = 0;

    while (getline(&line, &size, status_file) != -1) {
        if (strncmp(line, "Uid:", 4) == 0) {
            // Get the effective UID, which is the second value in the line
            *uid = (uid_t)atoi(strchr(line + 5, '\t'));
        } else if (strncmp(line, "Gid:", 4) == 0) {
            // Get the effective GID, which is the second value in the line
            *gid = (gid_t)atoi(strchr(line + 5, '\t'));
        } else if (strncmp(line, "NStgid:", 7) == 0) {
            // PID namespaces can be nested; the last one is the innermost one
            *nspid = atoi(strrchr(line, '\t'));
            nspid_found = 1;
        }
    }

    free(line);
    fclose(status_file);

    if (!nspid_found) {
        *nspid = alt_lookup_nspid(pid);
    }

    return 0;
}

int enter_ns(int pid, const char* type) {
#ifdef __NR_setns
    char path[64], selfpath[64];
    snprintf(path, sizeof(path), "/proc/%d/ns/%s", pid, type);
    snprintf(selfpath, sizeof(selfpath), "/proc/self/ns/%s", type);

    struct stat oldns_stat, newns_stat;
    if (stat(selfpath, &oldns_stat) == 0 && stat(path, &newns_stat) == 0) {
        // Don't try to call setns() if we're in the same namespace already
        if (oldns_stat.st_ino != newns_stat.st_ino) {
            int newns = open(path, O_RDONLY);
            if (newns < 0) {
                return -1;
            }

            // Some ancient Linux distributions do not have setns() function
            int result = syscall(__NR_setns, newns, 0);
            close(newns);
            return result < 0 ? -1 : 1;
        }
    }
#endif // __NR_setns

    return 0;
}

#elif defined(__APPLE__)

#include <sys/sysctl.h>

// macOS has a secure per-user temporary directory
int get_tmp_path_r(int pid, char* buf, size_t bufsize) {
    size_t path_size = confstr(_CS_DARWIN_USER_TEMP_DIR, buf, bufsize);
    return path_size > 0 && path_size <= sizeof(tmp_path) ? 0 : -1;
}

int get_process_info(int pid, uid_t* uid, gid_t* gid, int* nspid) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
    struct kinfo_proc info;
    size_t len = sizeof(info);

    if (sysctl(mib, 4, &info, &len, NULL, 0) < 0 || len <= 0) {
        return -1;
    }

    *uid = info.kp_eproc.e_ucred.cr_uid;
    *gid = info.kp_eproc.e_ucred.cr_gid;
    *nspid = pid;
    return 0;
}

// This is a Linux-specific API; nothing to do on macOS and FreeBSD
int enter_ns(int pid, const char* type) {
    return 0;
}

#else // __FreeBSD__

#include <sys/sysctl.h>
#include <sys/user.h>

// Use default /tmp path on FreeBSD
int get_tmp_path_r(int pid, char* buf, size_t bufsize) {
    return -1;
}

int get_process_info(int pid, uid_t* uid, gid_t* gid, int* nspid) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
    struct kinfo_proc info;
    size_t len = sizeof(info);

    if (sysctl(mib, 4, &info, &len, NULL, 0) < 0 || len <= 0) {
        return -1;
    }

    *uid = info.ki_uid;
    *gid = info.ki_groups[0];
    *nspid = pid;
    return 0;
}

// This is a Linux-specific API; nothing to do on macOS and FreeBSD
int enter_ns(int pid, const char* type) {
    return 0;
}

#endif
