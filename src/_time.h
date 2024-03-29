#ifndef COSMO_TIME_H
#define COSMO_TIME_H

#ifdef _WIN32

#    include <time.h>
#    include <windows.h>

#    if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
#        define DELTA_EPOCH_IN_MICROSECS 116444736000000000Ui64
#    else
#        define DELTA_EPOCH_IN_MICROSECS 116444736000000000ULL
#    endif

struct timezone
{
    int tz_minuteswest; /* minutes W of Greenwich */
    int tz_dsttime;     /* type of dst correction */
};

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    FILETIME ft;
    unsigned __int64 tmpres = 0;
    static int tzflag;

    if (NULL != tv) {
        GetSystemTimeAsFileTime(&ft);

        tmpres |= ft.dwHighDateTime;
        tmpres <<= 32;
        tmpres |= ft.dwLowDateTime;

        /*converting file time to unix epoch*/
        tmpres /= 10; /*convert into microseconds*/
        tmpres -= DELTA_EPOCH_IN_MICROSECS;
        tv->tv_sec = (long)(tmpres / 1000000UL);
        tv->tv_usec = (long)(tmpres % 1000000UL);
    }

    if (NULL != tz) {
        if (!tzflag) {
            _tzset();
            tzflag++;
        }
        tz->tz_minuteswest = _timezone / 60;
        tz->tz_dsttime = _daylight;
    }

    return 0;
}
#else
#    include <sys/time.h>
#endif

#endif