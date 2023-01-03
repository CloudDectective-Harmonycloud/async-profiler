#ifndef _COPYUTIL_H
#define _COPYUTIL_H

#define MAX_PATH 1024
extern char agent_command[];

int check_copy_folder(int pid, const char* src_path, const char* dst_path);

void check_copy_file(int pid, const char* src_path, const char* dst_path);

#endif // _COPYUTIL_H