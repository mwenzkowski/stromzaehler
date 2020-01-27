// Copyright © 2020 Maximilian Wenzkowski

#include <assert.h> // assert()
#include <curl/curl.h>
#include <errno.h> // Für die Variable errno
#include <inttypes.h>
#include <stdbool.h> //Für die Werte true und false
#include <stdint.h> // uint64_t
#include <stdio.h> // printf(), fprintf()
#include <stdlib.h> // exit(), strtod()
#include <string.h> // strncmp(), strchr()
#include <time.h> // localtime()
#include "smlReader.h"

#define BUF_LEN 1024
#define ABS(x) ((x) < 0 ? (-(x)) : (x))


const char serial_dev[] = "/dev/ttyAMA0";

const char INFLUXDB_WRITE_URL[] =
	"http://localhost:8086/write?db=stromzaehler&precision=ms";

const char SELECT_FORMAT[] =
	"SELECT count FROM \"stromzaehler\" WHERE time >= %" PRIu64
	" AND time < %" PRIu64 " ORDER BY DESC LIMIT 1";

const char INFLUXDB_QUERY_URL_FORMAT[] =
	"http://localhost:8086/query?db=stromzaehler&q=%s";

struct write_buf {
	char buf[BUF_LEN];
	size_t size;
};


char curl_data[BUF_LEN];

// parse_result() liest aus der Antwort der Datenbank-Anfrage (Parameter
// *result), den ersten Zählerstand aus und speichert ihn in den Parameter
// *count. Enthält die Antwort keine Zählerstände (es existieren kein oder
// Fehler) wird *count nicht verändert und false zurückgegeben, sonst wird true
// zurückgegeben.
// Die Antwort der Datenbank ist ein json-Objekt. Diese Funktion parst aber
// nicht allgemein json sondern tut folgendes:
//
//   1. Suche das erste Auftreten des Substrings 'value'
//   2. Suche das erste Komma nach 'value'
//   3. Lese den String nach dem Komma als Gleitkommazahl ein
bool parse_result(const char *result, double *count) {
	assert(result);
	assert(count);

	size_t res_len = strlen(result);

	const char pattern[] = "value";
	size_t pattern_len = strlen(pattern);

	if (res_len < pattern_len) {
		return false;
	}

	// Suche den Substring "value"
	const char *start = NULL;
	for (size_t i = 0; i < res_len - (pattern_len-1); i++) {
		if (strncmp(result+i, pattern, pattern_len) == 0) {
			start = result+i;
			break;
		}
	}
	if (start == NULL) {
		return false;
	}

	// Suche das nächste ','
	start = strchr(start, ',');
	if (start == NULL) {
		return false;
	}
	start++;

	char *end = NULL;
	errno = 0;
	double d = strtod(start, &end);
	if (start == end || errno != 0) {
		return false;
	}

	*count = d;
	return true;
}

size_t write_callback(char *contents, size_t size, size_t nmemb, void *userpointer)
{
	assert(userpointer);
	struct write_buf *wbuf = userpointer;
	size_t realsize = size * nmemb;

	if (wbuf->size + realsize >= BUF_LEN -1) {
		// Nicht genug Platz im buffer
		return 0;
	}
	memcpy(&(wbuf->buf[wbuf->size]), contents, realsize);
	wbuf->size += realsize;
	wbuf->buf[wbuf->size] = 0;

	return realsize;
}

// get_count() fragt den jüngsten Zählerstand ab dessen Zeitstempel t
// start <= t < end erfült und schreibt diesen Wert in *count. Existiert kein
// Eintrag in der Datenbank für diesen Zeitraum oder gab es einen Fehler bei
// der Abfrage wird *count auf -1.0 gesetzt.
// Gibt true zurück wenn die http-Verbindung zur Datenbank fehlgeschlagen ist
// und diese Funktion nochmal ausgeführt werden sollte, sonst wird true
// zurückgegeben.
bool get_count(uint64_t start_time, uint64_t end_time, double *count)
{
	assert(count);
	bool retry = false;
	char *url_encoded_query = NULL;

	CURL *c = curl_easy_init();
	if (!c) {
		goto error;
	}

	char buf[BUF_LEN];
	int len = snprintf(buf, BUF_LEN, SELECT_FORMAT, start_time, end_time);
	if (len >= BUF_LEN) {
		fprintf(stderr, "Buffer 'buf' in get_count() zu klein "
			"(%d benötigt, %d vorhanden)\n", len, BUF_LEN);
		goto error;
	}

	url_encoded_query = curl_easy_escape(c, buf, len);
	if (url_encoded_query == NULL) {
		fprintf(stderr, "curl_easy_escape() fehlgeschlagen\n");
		goto error;
	}

	len = snprintf(buf, BUF_LEN, INFLUXDB_QUERY_URL_FORMAT,
		url_encoded_query);
	if (len >= BUF_LEN) {
		fprintf(stderr, "Buffer 'buf' in get_count() zu klein "
			"(%d benötigt, %d vorhanden)\n", len, BUF_LEN);
		goto error;
	}

	curl_easy_setopt(c, CURLOPT_URL, buf);

	struct write_buf wbuf = {0};
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &wbuf);

	CURLcode res = curl_easy_perform(c);
	if(res != CURLE_OK) {

		fprintf(stderr, "Fehler: Abfrage aus der Datenbank "
			"fehlgeschlagen (%s)\n", curl_easy_strerror(res));

		*count = -1.0;
		retry = true;
		goto error;
	}

	CURLcode http_code = 0;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
	if (http_code != 200) {
		// influxdb gibt 200 als HTTP-Status-Code zurück wenn die
		// Anfrage erfolgreich war
		fprintf(stderr, "influxdb Anfrage per curl ist fehlerhaft"
			" (http-Code %d)\n", http_code);
		goto error;
	}


	if (parse_result(wbuf.buf, count) == false) {
		*count = -1.0;
	}

error:
	// curl_free(NULL) und curl_easy_cleanup(NULL) sind wie NOP
	curl_free(url_encoded_query);
	curl_easy_cleanup(c);

	return retry;
}

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
	// Zeilenweise Pufferung einstellen, für den Fall das stdout/stderr
	// nicht mit einem Terminal verbunden sind. Dies ist z.B. der Fall wenn
	// dieses Programm als Dienst automatisch beim Booten gestartet wird.
	setlinebuf(stdout);
	setlinebuf(stderr);

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

	double ref_count = -1.0;
	bool get_ref_count = true;

	double day_consumption = -1.0;
	bool calc_day_consumption = false;

	struct tm *ltime;
	int day_of_year = -1;

	struct measurement m;

	while (smlReader_nextMeasurement(reader, &m)) {

		ltime = localtime(&m.timestamp.tv_sec);

		if (ltime->tm_yday != day_of_year || get_ref_count) {

			printf("Frage Zählerstand vom Anfang des Tages aus "
				"der Datenbank ab.\n");

			day_of_year = ltime->tm_yday;

			// setze ltime auf den aktuellen Tag um 00:00 Uhr
			ltime->tm_sec = ltime->tm_min = ltime->tm_hour = 0;

			// ltime in Sekunden nach UNIX-Anfangs-Zeit umrechnen
			uint64_t end = mktime(ltime);
			uint64_t start = end - 60;

			// start, end in Nanosekunden umrechnen
			start *= 1000000000;
			end *= 1000000000;

			get_ref_count = get_count(start,end, &ref_count);

			if (ref_count == -1.0) {
				calc_day_consumption = false;
				day_consumption = -1.0;
			} else {
				calc_day_consumption = true;
			}
		}

		if (calc_day_consumption) {
			day_consumption = m.energy_count - ref_count;
		}

		insert_data(curl, &m, day_consumption);
	}

	smlReader_close(reader);
	curl_global_cleanup();
}
