#include <stdlib.h>
#include <stdio.h>
#include <time.h>


/*
 * Copy from nginx
 */

static int mday[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

time_t
bolt_parse_time(char *value, size_t len)
{
    char *p, *end;
    int day, month, year, hour, min, sec;
    enum {
        no = 0,
        rfc822,   /* Tue 10 Nov 2002 23:50:13    */
        rfc850,   /* Tuesday, 10-Dec-02 23:50:13 */
        isoc      /* Tue Dec 10 23:50:13 2002    */
    } fmt;

    fmt = 0;
    end = value + len;

    for (p = value; p < end; p++) {
        if (*p == ',') {
            break;
        }

        if (*p == ' ') {
            fmt = isoc;
            break;
        }
    }

    for (p++; p < end; p++)
        if (*p != ' ') {
            break;
        }

    if (end - p < 18) {
        return -1;
        }

    if (fmt != isoc) {
        if (*p < '0' || *p > '9' || *(p + 1) < '0' || *(p + 1) > '9') {
            return -1;
        }

        day = (*p - '0') * 10 + *(p + 1) - '0';
        p += 2;

        if (*p == ' ') {
            if (end - p < 18) {
                return -1;
            }
            fmt = rfc822;

        } else if (*p == '-') {
            fmt = rfc850;

        } else {
            return -1;
        }

        p++;
    }

    switch (*p) {

    case 'J':
        month = *(p + 1) == 'a' ? 0 : *(p + 2) == 'n' ? 5 : 6;
        break;

    case 'F':
        month = 1;
        break;

    case 'M':
        month = *(p + 2) == 'r' ? 2 : 4;
        break;

    case 'A':
        month = *(p + 1) == 'p' ? 3 : 7;
        break;

    case 'S':
        month = 8;
        break;

    case 'O':
        month = 9;
        break;

    case 'N':
        month = 10;
        break;

    case 'D':
        month = 11;
        break;

    default:
        return -1;
    }

    p += 3;

    if ((fmt == rfc822 && *p != ' ') || (fmt == rfc850 && *p != '-')) {
        return -1;
    }

    p++;

    if (fmt == rfc822) {
        if (*p < '0' || *p > '9' || *(p + 1) < '0' || *(p + 1) > '9'
            || *(p + 2) < '0' || *(p + 2) > '9'
            || *(p + 3) < '0' || *(p + 3) > '9')
        {
            return -1;
        }

        year = (*p - '0') * 1000 + (*(p + 1) - '0') * 100
               + (*(p + 2) - '0') * 10 + *(p + 3) - '0';
        p += 4;

    } else if (fmt == rfc850) {
        if (*p < '0' || *p > '9' || *(p + 1) < '0' || *(p + 1) > '9') {
            return -1;
        }

        year = (*p - '0') * 10 + *(p + 1) - '0';
        year += (year < 70) ? 2000 : 1900;
        p += 2;
    }

    if (fmt == isoc) {
        if (*p == ' ') {
            p++;
        }

        if (*p < '0' || *p > '9') {
            return -1;
        }

        day = *p++ - '0';

        if (*p != ' ') {
            if (*p < '0' || *p > '9') {
                return -1;
            }

            day = day * 10 + *p++ - '0';
        }

        if (end - p < 14) {
            return -1;
        }
    }

    if (*p++ != ' ') {
        return -1;
    }

    if (*p < '0' || *p > '9' || *(p + 1) < '0' || *(p + 1) > '9') {
        return -1;
    }

    hour = (*p - '0') * 10 + *(p + 1) - '0';
    p += 2;

    if (*p++ != ':') {
        return -1;
    }

    if (*p < '0' || *p > '9' || *(p + 1) < '0' || *(p + 1) > '9') {
        return -1;
    }

    min = (*p - '0') * 10 + *(p + 1) - '0';
    p += 2;

    if (*p++ != ':') {
        return -1;
    }

    if (*p < '0' || *p > '9' || *(p + 1) < '0' || *(p + 1) > '9') {
        return -1;
    }

    sec = (*p - '0') * 10 + *(p + 1) - '0';

    if (fmt == isoc) {
        p += 2;

        if (*p++ != ' ') {
            return -1;
        }

        if (*p < '0' || *p > '9' || *(p + 1) < '0' || *(p + 1) > '9'
            || *(p + 2) < '0' || *(p + 2) > '9'
            || *(p + 3) < '0' || *(p + 3) > '9')
        {
            return -1;
        }

        year = (*p - '0') * 1000 + (*(p + 1) - '0') * 100
               + (*(p + 2) - '0') * 10 + *(p + 3) - '0';
    }

    if (hour > 23 || min > 59 || sec > 59) {
         return -1;
    }

    if (day == 29 && month == 1) {
        if ((year & 3) || ((year % 100 == 0) && (year % 400) != 0)) {
            return -1;
        }

    } else if (day > mday[month]) {
        return -1;
    }

    if (sizeof(time_t) <= 4 && year >= 2038) {
        return -1;
    }

    /*
     * shift new year to March 1 and start months from 1 (not 0),
     * it's needed for Gauss's formula
     */

    if (--month <= 0) {
       month += 12;
       year -= 1;
    }

           /* Gauss's formula for Grigorian days from 1 March 1 BC */

    return (365 * year + year / 4 - year / 100 + year / 400
            + 367 * month / 12 - 31
            + day

           /*
            * 719527 days were between March 1, 1 BC and March 1, 1970,
            * 31 and 28 days in January and February 1970
            */

            - 719527 + 31 + 28) * 86400 + hour * 3600 + min * 60 + sec;
}


void
bolt_gmtime(time_t t, struct tm *tp)
{
    int sec, min, hour, mday, mon, year, wday, yday, days;

    days = t / 86400;

    /* Jaunary 1, 1970 was Thursday */
    wday = (4 + days) % 7;

    t %= 86400;
    hour = t / 3600;
    t %= 3600;
    min = t / 60;
    sec = t % 60;

    /* the algorithm based on Gauss's formula */

    days = days - (31 + 28) + 719527;

    year = days * 400 / (365 * 400 + 100 - 4 + 1);
    yday = days - (365 * year + year / 4 - year / 100 + year / 400);

    mon = (yday + 31) * 12 / 367;
    mday = yday - (mon * 367 / 12 - 31);

    mon += 2;

    if (yday >= 306) {

        /*
         * there is no "yday" in Win32 SYSTEMTIME
         *
         * yday -= 306;
         */

        year++;
        mon -= 12;

        if (mday == 0) {
            /* Jaunary 31 */
            mon = 1;
            mday = 31;

        } else if (mon == 2) {

            if ((year % 4 == 0) && (year % 100 || (year % 400 == 0))) {
                if (mday > 29) {
                    mon = 3;
                    mday -= 29;
                }

            } else if (mday > 28) {
                mon = 3;
                mday -= 28;
            }
        }
    }

    tp->tm_sec = sec;
    tp->tm_min = min;
    tp->tm_hour = hour;
    tp->tm_mday = mday;
    tp->tm_mon = mon;
    tp->tm_year = year;
    tp->tm_wday = wday;
}


size_t
bolt_format_time(char *buf, time_t t)
{
    struct tm tm;

    bolt_gmtime(t, &tm);

    return snprintf((char *) buf, sizeof("Mon, 28 Sep 1970 06:00:00 GMT"),
                                  "%s, %02d %s %4d %02d:%02d:%02d GMT",
                                  week[tm.tm_wday],
                                  tm.tm_mday,
                                  months[tm.tm_mon - 1],
                                  tm.tm_year,
                                  tm.tm_hour,
                                  tm.tm_min,
                                  tm.tm_sec);
}
