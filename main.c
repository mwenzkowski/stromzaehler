// Copyright © 2019 Maximilian Wenzkowski

#include <stdio.h> // printf()
#include <stdlib.h> // exit()
#include "smlReader.h"



int main(int argc, char *argv[])
{
	const char dev[] = "/dev/ttyAMA0";

	smlReader_t *reader = smlReader_create(dev);
	if (reader == NULL) {
		exit(EXIT_FAILURE);
	}

	struct measurement m;

	while (smlReader_nextMeasurement(reader, &m)) {
		printf("Zählerstand:        %15.7f kWh\n", m.energy_count);
		printf("Leistung (Summe):   %10.2f W\n", m.power);
		printf("Leistung (Phase 1): %10.2f W\n", m.powerL1);
		printf("Leistung (Phase 2): %10.2f W\n", m.powerL2);
		printf("Leistung (Phase 3): %10.2f W\n", m.powerL3);
		printf("Spannung (Phase 1): %10.2f W\n", m.voltageL1);
		printf("Spannung (Phase 2): %10.2f W\n", m.voltageL2);
		printf("Spannung (Phase 3): %10.2f W\n", m.voltageL3);
		printf("Sekundenindex:      %d\n", m.seconds_index);
		printf("Zeitstempel: %s\n\n", ctime(&m.timestamp));
	}

	smlReader_close(reader);
}
