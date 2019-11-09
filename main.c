// Copyright © 2019 Maximilian Wenzkowski

#include <assert.h> // assert()
#include <curl/curl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h> // printf()
#include <stdlib.h> // exit()
#include <time.h> // localtime()
#include "smlReader.h"

#define BUF_LEN 1024
#define ABS(x) ((x) < 0 ? (-(x)) : (x))


const char serial_dev[] = "/dev/ttyAMA0";
const char INFLUXDB_WRITE_URL[] =
	"http://localhost:8086/write?db=stromzaehler&precision=ms";

char curl_data[BUF_LEN];


CURL *create_influxdb_curl_handle()
{
	CURL *curl = curl_easy_init();
	if (curl == NULL) {
		curl_global_cleanup();
		return NULL;
	}
	curl_easy_setopt(curl, CURLOPT_URL, INFLUXDB_WRITE_URL);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, curl_data);

	return curl;
}

bool insert_data(CURL *curl, struct measurement *m, double day_consumption)
{
	assert(curl);
	assert(m);

	CURLcode res;

	uint64_t timestamp = (uint64_t) m->timestamp.tv_sec * UINT64_C(1000)
		+ (uint64_t) m->timestamp.tv_nsec / UINT64_C(1000000);

	long len = snprintf(curl_data, BUF_LEN,
		"stromzaehler count=%.7f,power=%.2f,power1=%.2f,power2=%.2f,"
		"power3=%.2f,volt1=%.1f,volt2=%.1f,volt3=%.1f %lld\n"
		"tagesverbrauch count=%.7f 0",
		m->energy_count, m->power, m->powerL1, m->powerL2, m->powerL3,
		m->voltageL1, m->voltageL2, m->voltageL3, timestamp,
		day_consumption);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, len);

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr,
			"Fehler: Senden der Stromzählerdaten an die Datenbank"
			" fehlgeschlagen (%s)\n", curl_easy_strerror(res));
		return false;
	}
	return true;
}


int main()
{
	curl_global_init(CURL_GLOBAL_ALL);

	CURL *curl = create_influxdb_curl_handle();
	if (curl == NULL) {
		fprintf(stderr, "Initialisierung von curl fehlgeschlagen\n");
		exit(EXIT_FAILURE);
	}

	smlReader_t *reader = smlReader_create(serial_dev);
	if (reader == NULL) {
		// smlReader_create() gibt eine Fehlermeldung aus
		exit(EXIT_FAILURE);
	}

	double ref_count;
	bool ref_count_known = false;
	double day_consumption;
	struct tm *ltime;
	int day_of_year = -1;

	struct measurement m;

	while (smlReader_nextMeasurement(reader, &m)) {

		ltime = localtime(&m.timestamp.tv_sec);

		if (ltime->tm_yday != day_of_year) {

			if (ltime->tm_hour == 0 && ltime->tm_min < 10) {
				day_of_year = ltime->tm_yday;
				ref_count = m.energy_count;
				ref_count_known = true;
			} else {
				ref_count_known = false;
			}
		}

		day_consumption = ref_count_known ? m.energy_count - ref_count : -1.0;
		insert_data(curl, &m, day_consumption);
	}

	smlReader_close(reader);
	curl_global_cleanup();
}
