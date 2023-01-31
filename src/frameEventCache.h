#ifndef _FRAME_EVENT_CACHE_H
#define _FRAME_EVENT_CACHE_H

#include "vmEntry.h"
#include "frameName.h"
#include "stoppableTask.h"

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

class CollectFrameEventTask;

class FrameEventCache {
    private:
        P_FrameEventList* _list;
        volatile int _write_index;

        CollectFrameEventTask* _collect_frame_task;
        std::thread _collect_frame_thread;

        friend class CollectFrameEventTask;
    public:
        FrameEventCache();
        ~FrameEventCache();

        void add(jint thread_id, int num_frames, ASGCT_CallFrame* frames);
        void collect(FrameName* fn);
        void startCollectThreadTask(FrameName* fn, long interval);
        void endCollectThreadTask();
};

class CollectFrameEventTask: public Stoppable {
    public:
        CollectFrameEventTask(FrameEventCache* cache, FrameName* fn, long interval) {
            this->cache = cache;
            this->fn = fn;
            this->intervalMs = interval / 1000000;
        }

        void run() {
            while (stopRequested() == false) {
                std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
                cache->collect(fn);
            }
        }
    private:
        FrameEventCache* cache;
        FrameName* fn;
        long intervalMs;
};
#endif // _FRAME_EVENT_CACHE_H