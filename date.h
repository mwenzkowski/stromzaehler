// Copyright © 2020 Maximilian Wenzkowski

#ifndef DATE_H
#define DATE_H

#include <stdbool.h>
#include <time.h>

struct date {
	unsigned day;
	unsigned month;
	unsigned year;
};

void get_current_date(struct date *date);
void get_previous_date(struct date *date, struct date *prev_date);
time_t date_to_time(struct date *date);
bool date_is_equal(struct date *a, struct date *b);

#endif
