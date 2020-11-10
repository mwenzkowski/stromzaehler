// Copyright © 2020 Maximilian Wenzkowski

#include "date.h"
#include "smlReader.h"
#include <assert.h> // assert()
#include <errno.h>
#include <libpq-fe.h>
#include <math.h> // lround()
#include <stdbool.h> //Für die Werte true und false
#include <stdio.h> // snprintf(), fprintf()
#include <stdlib.h> // exit()
#include <time.h> // time()
#include <unistd.h> // sleep()


const char SERIAL_DEV[] = "/dev/ttyAMA0";
const int QUERY_BUF_LEN = 512;

struct counter_cache {
	bool empty;
	double counter;
	time_t timestamp;
};

void
counter_cache_insert(struct counter_cache *cache, double counter)
{
		assert(cache);
		cache->counter = counter;
		cache->timestamp = time(NULL);
		assert(cache->timestamp != (time_t) -1);
		cache->empty = false;
}

void counter_cache_clear(struct counter_cache *cache)
{
		assert(cache);
		cache->empty = true;
}


struct stromzaehler {
	PGconn *dbConn;
	smlReader_t *smlReader;

	struct counter_cache counter_cache;

	struct date current_date;

	bool hasCounterAtStartOfDay;
	double counterAtStartOfDay;
};


void
error_exit(struct stromzaehler *stromzaehler)
{
	if (stromzaehler->dbConn) {
		PQfinish(stromzaehler->dbConn);
	}
	if (stromzaehler->smlReader) {
		smlReader_close(stromzaehler->smlReader);
	}
	exit(EXIT_FAILURE);
}

void
stromzaehler_create_SmlReader(struct stromzaehler *stromzaehler)
{
	assert(stromzaehler);

	stromzaehler->smlReader = smlReader_create(SERIAL_DEV);
	if (stromzaehler->smlReader == NULL) {
		// smlReader_create() gibt eine Fehlermeldung aus
		error_exit(stromzaehler);
	}
}

void
stromzaehler_connect_to_db(struct stromzaehler *stromzaehler)
{
	assert(stromzaehler);

	stromzaehler->dbConn = PQconnectdb("user=stromzähler dbname=stromzähler");
	if (stromzaehler->dbConn == NULL) {
		fprintf(stderr, "PQconnectdb() fehlgeschlagen");
		error_exit(stromzaehler);
	}
	if (PQstatus(stromzaehler->dbConn) == CONNECTION_BAD) {
		fprintf(stderr, "Connection to database failed: %s\n",
			PQerrorMessage(stromzaehler->dbConn));
		error_exit(stromzaehler);
	}
}

void
stromzaehler_get_counterAtStartOfDay_from_db(struct stromzaehler *stromzaehler)
{
	assert(stromzaehler);
	assert(stromzaehler->dbConn);

	stromzaehler->hasCounterAtStartOfDay = false;

	char query_buf[QUERY_BUF_LEN];
	PGresult *res = NULL;

	struct date *current = &stromzaehler->current_date;
	struct date prev;
	get_previous_date(current, &prev);

	int len = snprintf(query_buf, QUERY_BUF_LEN,
		"SELECT energy FROM stromzähler "
		"WHERE timestamp >= '%04d-%02d-%02d 23:59:00' "
		"AND timestamp < '%04d-%02d-%02d' "
		"ORDER BY timestamp DESC LIMIT 1;",
		prev.year, prev.month, prev.day,
		current->year, current->month, current->day);

	assert(len < QUERY_BUF_LEN && "query_buf too small");

	res = PQexec(stromzaehler->dbConn, query_buf);
	if (res == NULL) {
		fprintf(stderr, "PQexec failed: probably OOM\n");
		error_exit(stromzaehler);
	}

	if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		fprintf(stderr, "PQexec failed: %s\n",
			PQerrorMessage(stromzaehler->dbConn));

		if (PQstatus(stromzaehler->dbConn) == CONNECTION_BAD) {
			fprintf(stderr, "Connection to database lost");
			error_exit(stromzaehler);
		}

		goto error;
	}

	if (PQntuples(res) > 0) {
		char *start = PQgetvalue(res, 0, 0);
		char *end = NULL;
		errno = 0;
		stromzaehler->counterAtStartOfDay = strtod(start, &end);
		if (start != end && errno == 0) {
			stromzaehler->hasCounterAtStartOfDay = true;
		}
	}
error:
	if (res) {
		PQclear(res);
	}
}

void
stromzaehler_init(struct stromzaehler *stromzaehler)
{
	assert(stromzaehler);
	stromzaehler_create_SmlReader(stromzaehler);
	stromzaehler_connect_to_db(stromzaehler);

	get_current_date(&stromzaehler->current_date);
	stromzaehler_get_counterAtStartOfDay_from_db(stromzaehler);

	counter_cache_clear(&stromzaehler->counter_cache);
}

bool
counter_cache_valid(struct counter_cache *cache, struct date *date)
{
	assert(cache);
	assert(date);

	if (cache->empty) {
		return false;
	}

	time_t time = date_to_time(date);
	return (time >= cache->timestamp && time - cache->timestamp <= 60);
}


void
stromzaehler_update_counterAtStartOfDay(struct stromzaehler *stromzaehler)
{
	assert(stromzaehler);

	struct date old_date = stromzaehler->current_date;
	get_current_date(&stromzaehler->current_date);

	if (!date_is_equal(&stromzaehler->current_date, &old_date)) {

		if (counter_cache_valid(&stromzaehler->counter_cache, &stromzaehler->current_date)) {
			stromzaehler->counterAtStartOfDay = stromzaehler->counter_cache.counter;
			stromzaehler->hasCounterAtStartOfDay = true;
		} else {
			stromzaehler_get_counterAtStartOfDay_from_db(stromzaehler);
		}
	}
}

void
insert_measurement(struct stromzaehler *stromzaehler,
		struct measurement *measurement)
{
	assert(stromzaehler);
	assert(stromzaehler->dbConn);
	assert(measurement);

	char query_buf[QUERY_BUF_LEN];

	int len = snprintf(query_buf, QUERY_BUF_LEN,
		"INSERT INTO stromzähler(energy, power_total, power_phase1, power_phase2, "
		"power_phase3) VALUES(%.7f, %ld, %ld, %ld, %ld);",
		measurement->energy_count, lround(measurement->power),
		lround(measurement->powerL1), lround(measurement->powerL2),
		lround(measurement->powerL3));
	assert(len < QUERY_BUF_LEN && "query_buf too small");

	PGresult *res = PQexec(stromzaehler->dbConn, query_buf);
	if (res == NULL) {
		fprintf(stderr, "PQexec failed: probably OOM\n");
		error_exit(stromzaehler);
	}

	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		fprintf(stderr, "PQexec failed: %s\n",
			PQerrorMessage(stromzaehler->dbConn));

		if (PQstatus(stromzaehler->dbConn) == CONNECTION_BAD) {
			fprintf(stderr, "Connection to database lost");
			error_exit(stromzaehler);
		}
	}

	PQclear(res);
}

void
update_current_values(struct stromzaehler *stromzaehler,
		struct measurement *measurement)
{
	assert(stromzaehler);
	assert(stromzaehler->dbConn);
	assert(measurement);

	char query_buf[QUERY_BUF_LEN];
	int len;
	if (stromzaehler->hasCounterAtStartOfDay) {
		len = snprintf(query_buf, QUERY_BUF_LEN,
			"UPDATE current_values SET timestamp = CURRENT_TIMESTAMP, "
			"energy = %.7f, energy_daily = %.7f WHERE id = 0;",
			measurement->energy_count,
			measurement->energy_count - stromzaehler->counterAtStartOfDay);
	} else {
		len = snprintf(query_buf, QUERY_BUF_LEN,
			"UPDATE current_values SET timestamp = CURRENT_TIMESTAMP, "
			"energy = NULL, energy_daily = NULL WHERE id = 0;");
	}
	assert(len < QUERY_BUF_LEN && "query_buf too small");

	PGresult *res = PQexec(stromzaehler->dbConn, query_buf);
	if (res == NULL) {
		fprintf(stderr, "PQexec failed: probably OOM\n");
		error_exit(stromzaehler);
	}

	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		fprintf(stderr, "PQexec failed: %s\n",
			PQerrorMessage(stromzaehler->dbConn));

		if (PQstatus(stromzaehler->dbConn) == CONNECTION_BAD) {
			fprintf(stderr, "Connection to database lost");
			error_exit(stromzaehler);
		}
	}

	PQclear(res);
}

int
main()
{
	// Zeilenweise Pufferung einstellen, für den Fall das stdout/stderr
	// nicht mit einem Terminal verbunden sind. Dies ist z.B. der Fall wenn
	// dieses Programm als Dienst automatisch beim Booten gestartet wird.
	setlinebuf(stdout);
	setlinebuf(stderr);

	struct stromzaehler stromzaehler;
	stromzaehler_init(&stromzaehler);

	struct measurement measurement;
	while (smlReader_nextMeasurement(stromzaehler.smlReader, &measurement)) {

		counter_cache_insert(&stromzaehler.counter_cache, measurement.energy_count);

		insert_measurement(&stromzaehler, &measurement);
		stromzaehler_update_counterAtStartOfDay(&stromzaehler);
		update_current_values(&stromzaehler, &measurement);
	}

	fprintf(stderr, "smlReader_nextMeasurement() fehlgeschlagen");
	error_exit(&stromzaehler);
}
