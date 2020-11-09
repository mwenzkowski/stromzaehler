// Copyright © 2020 Maximilian Wenzkowski

#include "smlReader.h"
#include <assert.h> // assert()
#include <libpq-fe.h>
#include <math.h> // lround()
#include <stdbool.h> //Für die Werte true und false
#include <stdio.h> // snprintf(), fprintf()
#include <stdlib.h> // exit()
#include <unistd.h> // sleep()


const char SERIAL_DEV[] = "/dev/ttyAMA0";
const int QUERY_BUF_LEN = 512;

struct stromzaehler {
	PGconn *dbConn;
	smlReader_t *smlReader;
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

int
main()
{
	// Zeilenweise Pufferung einstellen, für den Fall das stdout/stderr
	// nicht mit einem Terminal verbunden sind. Dies ist z.B. der Fall wenn
	// dieses Programm als Dienst automatisch beim Booten gestartet wird.
	setlinebuf(stdout);
	setlinebuf(stderr);

	struct stromzaehler stromzaehler = {0};
	stromzaehler_create_SmlReader(&stromzaehler);
	stromzaehler_connect_to_db(&stromzaehler);

	struct measurement measurement;
	while (smlReader_nextMeasurement(stromzaehler.smlReader, &measurement)) {
		insert_measurement(&stromzaehler, &measurement);
	}

	fprintf(stderr, "smlReader_nextMeasurement() fehlgeschlagen");
	error_exit(&stromzaehler);
}
