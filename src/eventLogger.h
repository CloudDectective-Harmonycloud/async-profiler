
#ifndef _EVENT_LOGGER_H
#define _EVENT_LOGGER_H

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

const char* KINDLING_PREFIX = "kd@";

class EventLogger {
  private:
    static FILE* _file;

  public: 
    static void open(const char* file_name) {
        if (_file != stdout && _file != stderr) {
            fclose(_file);
        }

        if (file_name == NULL || strcmp(file_name, "stdout") == 0) {
            _file = stdout;
        } else if (strcmp(file_name, "stderr") == 0) {
            _file = stderr;
        } else if ((_file = fopen(file_name, "w")) == NULL) {
            _file = stdout;
            printf("Could not open log file: %s", file_name);
        }
    }

    static void close() {
        if (_file != stdout && _file != stderr) {
            fclose(_file);
            _file = stdout;
        }
    }

    static void log(const char* msg, ...) {
        va_list args;
        va_start(args, msg);
        log(msg, args);
        va_end(args);
    }

    static void log(const char* msg, va_list args) {
        char buf[1024];
        size_t len = vsnprintf(buf, sizeof(buf), msg, args);
        if (len >= sizeof(buf)) {
            len = sizeof(buf) - 1;
            buf[len] = 0;
        }

        fprintf(_file, "%s%s\n", KINDLING_PREFIX, buf);
        fflush(_file);
    }
};

FILE* EventLogger::_file = stdout;

#endif // _EVENT_LOGGER_H
