#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "copyutil.h"

int main(int argc, const char** argv) {
    if (argc != 4) {
        printf("Usage: jcopy <pid> <src> <dst>\n");
        return 1;
    }

    int pid = atoi(argv[1]);
    if (pid <= 0) {
        fprintf(stderr, "%s is not a valid process ID\n", argv[1]);
        return 1;
    }

    struct stat s_buf;
    stat(argv[2], &s_buf);
    if (S_ISDIR(s_buf.st_mode)) {
        if (check_copy_folder(pid, argv[2], argv[3]) < 0) {
            return 1;
        }
        return 0;
    }
    check_copy_file(pid, argv[2], argv[3]);
    return 0;
}