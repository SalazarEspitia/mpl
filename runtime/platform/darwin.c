#include "platform.h"

#include <dlfcn.h>
#include <stdio.h>
#include <errno.h>
#include <mach/mach.h>
#include <sys/resource.h>

#include "platform/diskBack.unix.c"
#include "platform/mmap-protect.c"
#include "platform/nonwin.c"
#include "platform/sysctl.c"
#include "platform/use-mmap.c"

void GC_displayMem (void) {
        static char buffer[256];

        snprintf (buffer, cardof(buffer), "/usr/bin/vmmap -w -interleaved %d\n", (int)getpid ());
        (void)system (buffer);
}

/* The calling thread's port is looked up once per thread and kept (one
 * port right per thread, until the process exits): mach_thread_self and
 * mach_port_deallocate on every call made a reading about twice as slow. */
static _Thread_local mach_port_t getrusage_thread_port = MACH_PORT_NULL;

int getrusage_thread(struct rusage *rusage) {
        thread_basic_info_data_t info;
        mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;

        if (getrusage_thread_port == MACH_PORT_NULL)
                getrusage_thread_port = mach_thread_self();
        kern_return_t kr = thread_info(getrusage_thread_port, THREAD_BASIC_INFO, (thread_info_t)&info, &count);

        if (kr == KERN_SUCCESS && (info.flags & TH_FLAGS_IDLE) == 0) {
        memset(rusage, 0, sizeof(struct rusage));
        rusage->ru_utime.tv_sec = info.user_time.seconds;
        rusage->ru_utime.tv_usec = info.user_time.microseconds;
        rusage->ru_stime.tv_sec = info.system_time.seconds;
        rusage->ru_stime.tv_usec = info.system_time.microseconds;
        return 0;
        } else {
        errno = EINVAL;
        return -1;
        }
}
