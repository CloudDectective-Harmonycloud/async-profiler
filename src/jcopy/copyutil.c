#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "copyutil.h"

char agent_command[MAX_PATH];

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
            if (fchmod(dst_fd, 0777) < 0) {
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

int check_copy_folder(int pid, const char* src_path, const char* dst_path) {
    char write_container_path[128];
	snprintf(write_container_path, sizeof(write_container_path), "/proc/%d/root%s", pid, dst_path);
    if (prepare_write_dir(write_container_path, strlen(dst_path)) == -1) {
        return -1;
    }
    if (copy_dir(src_path, write_container_path) == -1) {
        return -1;
    }
    return 0;
}

void check_copy_file(int pid, const char* src_path, const char* dst_path) {
    char write_container_path[128];
	snprintf(write_container_path, sizeof(write_container_path), "/proc/%d/root%s", pid, dst_path);
    if (prepare_write_dir(write_container_path, strlen(dst_path)) == -1) {
        return;
    }
    copy_file(src_path, write_container_path);
}