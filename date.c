// Copyright © 2020 Maximilian Wenzkowski

#include "date.h"
#include <assert.h> // assert()
#include <errno.h> // Variable errno
#include <stdbool.h> // Datentyp bool
#include <stdio.h> //fprintf()
#include <string.h> //strerror()
#include <time.h> //time(), localtime()


void
time_to_date(struct date *date, time_t time)
{
	assert(date);
	assert(time >= (time_t) 0);

	struct tm *tm = localtime(&time);
	assert(tm);

	date->day = tm->tm_mday;
	// tm_mon stores the month as 0-11 (January = 0)
	date->month = tm->tm_mon + 1;
	// tm_year stores the year as an offset of the year 1900
	date->year = tm->tm_year + 1900;
}

void
get_current_date(struct date *date)
{
	assert(date);

	time_t now = time(NULL);
	assert(now != (time_t) -1);

	time_to_date(date, now);
}

void
get_previous_date(struct date *date, struct date *prev_date)
{
	assert(date);
	assert(date->day >= 1 && date->day <= 31 &&
		date->month >= 1 && date->month <= 12 &&
		date->year >= 1900);
	assert(prev_date);

	struct tm tm;
	// tm_year stores the year as an offset of the year 1900
	tm.tm_year = date->year - 1900;
	// tm_mon stores the month as 0-11 (January = 0)
	tm.tm_mon = date->month - 1;
	tm.tm_mday = date->day;
	tm.tm_hour = 12;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	// setting this to -1 instructs mktime() to determine if daylight saving
	// time is in effect
	tm.tm_isdst = -1;

	time_t time = mktime(&tm);
	assert(time != (time_t) -1);
	

	// since tm.tm_hour was set to 12, it is irrelevant if daylight saving time
	// is in effect. It suffices to substract the number of seconds in a day
	time -= (time_t) 24 * 60 * 60;


	struct tm *tm_prev = localtime(&time);
	assert(tm_prev);

	prev_date->day = tm_prev->tm_mday;
	// tm_mon stores the month as 0-11 (January = 0)
	prev_date->month = tm_prev->tm_mon + 1;
	// tm_year stores the year as an offset of the year 1900
	prev_date->year = tm_prev->tm_year + 1900;
}

time_t
date_to_time(struct date *date)
{
	assert(date);
	assert(date->day >= 1 && date->day <= 31 &&
		date->month >= 1 && date->month <= 12 &&
		date->year >= 1900);

	struct tm tm;
	// tm_year stores the year as an offset of the year 1900
	tm.tm_year = date->year - 1900;
	// tm_mon stores the month as 0-11 (January = 0)
	tm.tm_mon = date->month - 1;
	tm.tm_mday = date->day;
	tm.tm_hour = 0;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	// setting this to -1 instructs mktime() to determine if daylight saving
	// time is in effect
	tm.tm_isdst = -1;

	time_t time = mktime(&tm);
	assert(time != (time_t) -1);

	return time;
}

bool
date_is_equal(struct date *a, struct date *b) 
{
	assert(a && b);
	return a->day == b->day && a->month == b->month && a->year == b->year;
}
