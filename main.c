// Copyright © 2020 Maximilian Wenzkowski

#include "smlReader.h"
#include <assert.h> // assert()
#include <inttypes.h>
#include <libpq-fe.h>
#include <math.h> // lround()
#include <stdbool.h> //Für die Werte true und false
#include <stdio.h> // snprintf(), fprintf()
#include <stdlib.h> // exit()
#include <unistd.h> // sleep()

#define BUF_LEN 1024


const char serial_dev[] = "/dev/ttyAMA0";


char query[BUF_LEN];


bool insert_data(PGconn *conn, struct measurement *m)
{
	assert(conn);
	assert(m);

	snprintf(query, BUF_LEN,
		"INSERT INTO stromzähler(energy, power_total, power_phase1, power_phase2, "
		"power_phase3) VALUES(%.7f, %ld, %ld, %ld, %ld);",
		m->energy_count, lround(m->power), lround(m->powerL1),
		lround(m->powerL2), lround(m->powerL3));

	PGresult *res = PQexec(conn, query);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		fprintf(stderr, "PQexec failed: %s\n", PQerrorMessage(conn));
		PQclear(res);
		return false;
	}

	PQclear(res);
	return true;
}


int main()
{
	// Zeilenweise Pufferung einstellen, für den Fall das stdout/stderr
	// nicht mit einem Terminal verbunden sind. Dies ist z.B. der Fall wenn
	// dieses Programm als Dienst automatisch beim Booten gestartet wird.
	setlinebuf(stdout);
	setlinebuf(stderr);


	smlReader_t *reader = smlReader_create(serial_dev);
	if (reader == NULL) {
		// smlReader_create() gibt eine Fehlermeldung aus
		exit(EXIT_FAILURE);
	}

	while(true) {

		PGconn *conn = PQconnectdb("user=stromzähler dbname=stromzähler");
		if (conn == NULL) {
			fprintf(stderr, "PQconnectdb() fehlgeschlagen");
			PQfinish(conn);
			smlReader_close(reader);
			exit(EXIT_FAILURE);
		}
		if (PQstatus(conn) == CONNECTION_BAD) {
			fprintf(stderr, "Connection to database failed: %s\n",
				PQerrorMessage(conn));
			PQfinish(conn);
			sleep(5); // Warte 5 Sekunden
			break;
		}

		struct measurement m;
		while(true) {
			if (!smlReader_nextMeasurement(reader, &m)) {
				fprintf(stderr, "smlReader_nextMeasurement() fehlgeschlagen");
				PQfinish(conn);
				smlReader_close(reader);
				exit(EXIT_FAILURE);
			}
			insert_data(conn, &m);
		}

		PQfinish(conn);
		sleep(5); // Warte 5 Sekunden
	}
	smlReader_close(reader);
}
