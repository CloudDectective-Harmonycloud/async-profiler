#ifndef _FRAME_EVENT_CACHE_H
#define _FRAME_EVENT_CACHE_H

#include "vmEntry.h"
#include "frameName.h"

class FrameEvent {
    private:
        u64 _timestamp;
        int _num_frames;
        ASGCT_CallFrame* _frames;
    public:
        int _thread_id;

        FrameEvent(int depth);
        ~FrameEvent();
        void setEvent(int thread_id, int num_frames, ASGCT_CallFrame* frames);
        void log(FrameName* frameName);
};

typedef FrameEvent* P_FrameEvent;

class FrameEventList {
    private:
        int _capacity;
        volatile int _count;
        P_FrameEvent* _events;
    public:
        FrameEventList(int capacity, int max_depth);
        ~FrameEventList();
        void addFrameEvent(int thread_id, int num_frames, ASGCT_CallFrame* frames);
        void log(FrameName* frameName);
};

typedef FrameEventList* P_FrameEventList;

class FrameEventCache {
    private:
        P_FrameEventList* _list;
        volatile int _write_index;
        FrameName* _frameName;
    public:
        FrameEventCache(Arguments& args, int style, int epoch, Mutex& thread_names_lock, ThreadMap& thread_names);
        ~FrameEventCache();

        void add(jint thread_id, int num_frames, ASGCT_CallFrame* frames);
        void collect();
};
#endif // _FRAME_EVENT_CACHE_H