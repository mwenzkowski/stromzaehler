// Copyright © 2019 Maximilian Wenzkowski

#ifndef SML_READER_H
#define SML_READER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct smlReader smlReader_t;

struct measurement {
	double energy_count;
	double power, powerL1, powerL2, powerL3;
	double voltageL1, voltageL2, voltageL3;
	uint32_t seconds_index;	
	struct timespec timestamp;
};

smlReader_t *smlReader_create(const char *device);
void smlReader_close(struct smlReader *sr);
bool smlReader_nextMeasurement (struct smlReader *sr, struct measurement *m);
#endif
