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
char version[32];

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

static int get_version(char* path) {
    char version_path[256];
	snprintf(version_path, sizeof(version_path), "%s/version", path);
    FILE *version_file = fopen(version_path, "r");
	if (version_file == NULL) {
		fprintf(stderr, "[x File NotExist] %s\n", version_path);
		return -1;
	}
    ssize_t len;
    size_t size = 0;
    char* new_version = NULL;
    while ((len = getline(&new_version, &size, version_file)) > 0) {
        new_version[len - 1] = 0;
        strcpy(version, new_version);
        return 0;
    }
    return -1;
}

static int mk_dir(char *write_dir_path) {
	if (access(write_dir_path, F_OK) != 0) {
		mkdir(write_dir_path, 0666);

		int dst_fd = open(write_dir_path, O_RDONLY);
		if (fchmod(dst_fd, 0766) < 0) {
			fprintf(stderr, "[x Chmod Folder] %s\n", write_dir_path);
			return -1;
		}
		fprintf(stderr, "[√ Make Directory] %s\n", write_dir_path);
	}
	return 0;
}

static int prepare_write_dir(const char *write_container_path, int to_copy_length) {
	char str[512];
	strncpy(str, write_container_path, 512);

	int full_length = strlen(str);
    int i = 0;
	for (i = full_length - to_copy_length; i < full_length; i++) {
		if (str[i] == '/') {
			str[i] = '\0';
			if (mk_dir(str) != 0) {
				return -1;
			}
			str[i] = '/';
		}
	}
	return 0;
}

static void copy_file(const char *src, const char *dst) {
    int dstFd = open(dst, O_RDONLY);
    if (dstFd >= 0) {
        fprintf(stderr, "[Exist File] %s\n", dst);
        return;
    }

	int srcFd = open(src, O_RDONLY);
	if (srcFd < 0) {
		fprintf(stderr, "[x Open File] %s\n", src);
		return;
	}
	struct stat stats;
	stat(src, &stats);

    dstFd = open(dst, O_WRONLY | O_CREAT, stats.st_mode);
	if (dstFd < 0) {
		fprintf(stderr, "[x Create File] %s\n", dst);
		return;
	}

    fprintf(stderr, "[Create File] %s, Fd: %d\n", dst, dstFd);

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

static int copy_dir(const char *read_dir_path, const char *write_dir_path) {
	DIR *p_dir = opendir(read_dir_path);
	if (p_dir == NULL) {
        fprintf(stderr, "[x Open Folder] %s\n", read_dir_path);
		return -1;
	}

    DIR* dir = opendir(write_dir_path);
    if (dir == NULL) {
        if (mkdir(write_dir_path, 0666) < 0) {
            fprintf(stderr, "[x Create Folder] %s\n", write_dir_path);
            return -1;
        } else {
            int dst_fd = open(write_dir_path, O_RDONLY);
            if (fchmod(dst_fd, 0766) < 0) {
                fprintf(stderr, "[x Chmod Folder] %s\n", write_dir_path);
                return -1;
            }
            fprintf(stderr, "[√ Create Folder] %s\n", write_dir_path);
        }
    }

	struct stat s_buf;
	struct dirent *entry;
	while ((entry = readdir(p_dir)) != NULL) {
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
			continue;
		}
		char read_buffer[512];
		char write_buffer[512];
        snprintf(read_buffer, sizeof(read_buffer), "%s/%s", read_dir_path, entry->d_name);
        snprintf(write_buffer, sizeof(write_buffer), "%s/%s", write_dir_path, entry->d_name);

		stat(read_buffer, &s_buf);
		if (S_ISDIR(s_buf.st_mode)) {
			copy_dir(read_buffer, write_buffer);
		} else if (S_ISREG(s_buf.st_mode)) {
			copy_file(read_buffer, write_buffer);
		}
	}
	closedir(p_dir);

	return 0;
}

int check_copy_agent(int pid, char* srcPath, char* agentpath, char* agentname, char* so_name, char* command) {
    if (get_version(agentpath) == -1) {
        return -1;
    }
    fprintf(stderr, "[√ Get Version] %s\n", version);

    char write_container_path[128];
	snprintf(write_container_path, sizeof(write_container_path), "/proc/%d/root/tmp/kindling", pid);
    if (prepare_write_dir(write_container_path, 13) == -1) {
        return -1;
    }
    if (copy_dir(agentpath, write_container_path) == -1) {
        return -1;
    }

    // copy libasyncProfiler.so to /tmp/kindling/{version}/libasyncProfiler.so
    char src_so_path[256], dst_so_path[256];
    snprintf(src_so_path, sizeof(src_so_path), "%s/%s", srcPath, so_name);
    snprintf(dst_so_path, sizeof(dst_so_path), "%s/%s/%s", write_container_path, version, so_name);
    copy_file(src_so_path, dst_so_path);

    snprintf(agent_command, sizeof(agent_command), "/tmp/kindling/%s=%s,version=%s", agentname, command, version);
    return 0;
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