// Copyright © 2019 Maximilian Wenzkowski

#include <assert.h> // assert()
#include <curl/curl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h> // printf()
#include <stdlib.h> // exit()
#include "smlReader.h"

#define BUF_LEN 1024


const char device[] = "/dev/ttyAMA0";

const char url[] = "http://localhost:8086/write?db=stromzaehler&precision=ms";

char curl_data[BUF_LEN];



bool insert_data(CURL *curl, struct measurement *m)
{
	assert(curl);
	assert(m);

	CURLcode res;

	uint64_t timestamp = (uint64_t) m->timestamp.tv_sec * UINT64_C(1000)
		+ (uint64_t) m->timestamp.tv_nsec / UINT64_C(1000000);

	long len = snprintf(curl_data, BUF_LEN, "stromzaehler count=%.7f,power=%.2f,power1=%.2f,power2=%.2f,power3=%.2f,volt1=%.1f,volt2=%.1f,volt3=%.1f %lld",
		m->energy_count, m->power, m->powerL1, m->powerL2, m->powerL3, m->voltageL1, m->voltageL2, m->voltageL3, timestamp);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, len);

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr, "Fehler: curl_easy_perform() fehlgeschlagen: %s\n",
			curl_easy_strerror(res));
		return false;
	}
	return true;
}


int main()
{
	CURL *curl;

	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();
	if (curl == NULL) {
		curl_global_cleanup();
		fprintf(stderr, "Initialisierung von curl fehlgeschlagen\n");
		exit(EXIT_FAILURE);
	}
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, curl_data);


	smlReader_t *reader = smlReader_create(device);
	if (reader == NULL) {
		// smlReader_create() gibt eine Fehlermeldung aus
		exit(EXIT_FAILURE);
	}

	struct measurement m;

	while (smlReader_nextMeasurement(reader, &m)) {

		if (insert_data(curl, &m) == false) {
			smlReader_close(reader);
			curl_global_cleanup();
			fprintf(stderr, "Übertragung mit curl fehlgeschlagen\n");
			exit(EXIT_FAILURE);
		}
	}

	smlReader_close(reader);
	curl_global_cleanup();
}
