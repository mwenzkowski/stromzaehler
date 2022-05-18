// Copyright © 2021 Maximilian Wenzkowski

#include <assert.h> // assert();
#include <errno.h>
#include <fcntl.h> // open()
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h> // fprintf()
#include <stdlib.h> // calloc();
#include <string.h>
#include <sys/ioctl.h> // ioctl()
#include <termios.h> // tcgetattr(), tcsetattr()
#include <time.h>
#include <unistd.h> // close()

#include "smlReader.h"
#include "crc16.h"

#define SML_LEN 404
#define READ_LEN 255 // must be > 4

#define CRC_START 366

#define MSG_START 59
#define MSG_LEN 306

#define END_SEQ_START 396

#define SEC_INDEX_START 104

#define ENERGY_COUNT_START 168

#define POWER_START    192 // signed
#define POWER_L1_START 216
#define POWER_L2_START 240
#define POWER_L3_START 264

#define VOLTAGE_L1_START 288 // unsigned
#define VOLTAGE_L2_START 306
#define VOLTAGE_L3_START 324


const uint8_t endSeq[] = {0x1b, 0x1b, 0x1b, 0x1b, 0x1a};

static int
serialPort_open(const char* device)
{
	int bits;
	struct termios config = {0};

	int fd = open(device, O_RDWR | O_NOCTTY);

	if (fd < 0) {
		fprintf(stderr, "Error: Opening %s failed (%s)\n",
			device, strerror(errno));
		return -1;
	}

	// set RTS
	ioctl(fd, TIOCMGET, &bits);
	bits |= TIOCM_RTS;
	ioctl(fd, TIOCMSET, &bits);

	if (tcgetattr(fd, &config) < 0) {
		fprintf(stderr, "Error: tcgetattr() failed (%s)\n",
			strerror(errno));
		return -1;
	}

	// set 8-N-1
	config.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR
			| ICRNL | IXON);
	config.c_oflag &= ~OPOST;
	config.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	config.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
	config.c_cflag |= CS8;

	// set speed to 9600 baud
	if (cfsetispeed(&config, B9600) < 0) {
		fprintf(stderr, "Error: cfsetispeed() failed (%s)\n",
			strerror(errno));
		return -1;
	}
	if (cfsetospeed(&config, B9600) < 0) {
		fprintf(stderr, "Error: cfsetospeed() failed (%s)\n",
			strerror(errno));
		return -1;
	}

	config.c_cc[VMIN] = 255;
	config.c_cc[VTIME] = 1;

	if (tcsetattr(fd, TCSANOW, &config) < 0) {
		fprintf(stderr, "Error: tcsetattr() failed (%s)\n",
			strerror(errno));
		return -1;
	}

	return fd;
}

struct smlReader {
	char *device;
	int fd;
	uint8_t sml_buf[SML_LEN];
	uint8_t read_buf[READ_LEN];
	unsigned next, len;
};

smlReader_t *
smlReader_create(const char *device)
{
	assert(device != 0);

	struct smlReader *sr = calloc(1, sizeof(struct smlReader));
	if (sr == NULL) {
		fprintf(stderr, "Error: Out of memory.\n");
		return NULL;
	}

	sr->device = strdup(device);
	if (sr->device == NULL) {
		fprintf(stderr, "Error: strdup() failed (%s)\n",
			strerror(errno));
		free(sr);
		return NULL;
	}

	sr->fd  = serialPort_open(device);
	if (sr->fd == -1) {
		free(sr->device);
		free(sr);
		return NULL;
	}

	return sr;
}

void
smlReader_close(struct smlReader *sr)
{
	if (sr == NULL) {
		return;
	}

	if (close(sr->fd) < 0) {
		fprintf(stderr, "Error: Closing %s faild (%s).\n",
			sr->device, strerror(errno));
	}
	free(sr->device);
	free(sr);
}

static bool
readByte(struct smlReader *sr, uint8_t *dest)
{
	assert(sr != NULL);
	assert(sr->fd >= 0);

	while (sr->len == 0) {
		int n = read(sr->fd, sr->read_buf, READ_LEN);
		if (n == -1) {
			fprintf(stderr, "Error: Reading from %s "
				"failed (%s).\n",
				sr->device, strerror(errno));
			return false;
		}
		sr->len = n;
		sr->next = 0;
	}

	*dest = sr->read_buf[sr->next];
	sr->next++;
	sr->len--;

	return true;
}

static bool
check_received_data(struct smlReader *sr)
{
	assert(sr);

	// check if the end sequences was received
	if (memcmp(&(sr->sml_buf[END_SEQ_START]), endSeq, sizeof(endSeq)) != 0) {
		return false;
	}

	// verify checksum of the inner SML-Message
	uint16_t checksum = ((uint16_t) sr->sml_buf[CRC_START]) << 8;
	checksum |= (uint16_t) sr->sml_buf[CRC_START+1];

	if (crc16( &(sr->sml_buf[MSG_START]), MSG_LEN) != checksum) {
		fprintf(stderr, "Fehler bei der Übertragung (berechnete "
		"Checksumme stimmt nicht mit der empfangen überein)\n");
		fprintf(stderr, "Transmission error (calculated "
		"checksum differs from the received checksum)\n");
		return false;
	}
	return true;

}

static bool
readSmlFile(struct smlReader *sr)
{
	assert(sr);

	while (true) {
		unsigned len = 0;

		// Wait for the start sequence
		while (len < 8) {
			if (readByte(sr, &(sr->sml_buf[len])) == false) {
				return false;
			}
			if ((sr->sml_buf[len] == 0x1b && len < 4) ||
					(sr->sml_buf[len] == 0x01 && len >= 4)) {
				len++;
			} else {
				len = 0;
			}
		}

		// Read data and if two escape sequences occour in a row, ignore one
		// escape sequence
		unsigned esc_counter = 0;
		while (len < SML_LEN - 8) {
			if (readByte(sr, &(sr->sml_buf[len])) == false) {
				return false;
			}
			if (sr->sml_buf[len] == 0x1b) {
				if (++esc_counter == 8) {
					len -= 4;
					esc_counter = 0;
				}
			} else {
				esc_counter = 0;
			}
			len++;
		}

		// read end sequence
		while (len < SML_LEN) {
			if (readByte(sr, &(sr->sml_buf[len])) == false) {
				return false;
			}
			len++;
		}

		if (check_received_data(sr) == true) {
			break;
		}
	}

	return true;
}

static uint16_t read_uint16(uint8_t *data) {
	uint16_t result = ((uint16_t) data[0]) << 8;
	result |= ((uint16_t) data[1]);
	return result;
}

static uint32_t read_uint32(uint8_t *data) {
	uint32_t result = ((uint32_t) data[0]) << 3*8;
	result |= ((uint32_t) data[1]) << 2*8;
	result |= ((uint32_t) data[2]) << 1*8;
	result |= ((uint32_t) data[3]);
	return result;
}

static int64_t read_int64(uint8_t *data) {
	int64_t result = ((uint64_t) data[0]) << 7*8;
	result |= ((uint64_t) data[1]) << 6*8;
	result |= ((uint64_t) data[2]) << 5*8;
	result |= ((uint64_t) data[3]) << 4*8;
	result |= ((uint64_t) data[4]) << 3*8;
	result |= ((uint64_t) data[5]) << 2*8;
	result |= ((uint64_t) data[6]) << 1*8;
	result |= ((uint64_t) data[7]);
	return result;
}

static void read_measurements(struct smlReader *sr, struct measurement *m) {
	assert(sr);
	assert(m);

	m->seconds_index = read_uint32(&(sr->sml_buf[SEC_INDEX_START]));

	int64_t temp = read_int64(&(sr->sml_buf[ENERGY_COUNT_START]));
	m->energy_count = (double) temp / 10000000.0;

	temp = read_int64(&(sr->sml_buf[POWER_START]));
	m->power = (double) temp / 100.0;

	temp = read_int64(&(sr->sml_buf[POWER_L1_START]));
	m->powerL1 = (double) temp / 100.0;

	temp = read_int64(&(sr->sml_buf[POWER_L2_START]));
	m->powerL2 = (double) temp / 100.0;

	temp = read_int64(&(sr->sml_buf[POWER_L3_START]));
	m->powerL3 = (double) temp / 100.0;

	uint16_t voltage = read_uint16(&(sr->sml_buf[VOLTAGE_L1_START]));
	m->voltageL1 = (double) voltage / 10.0;

	voltage = read_uint16(&(sr->sml_buf[VOLTAGE_L2_START]));
	m->voltageL2 = (double) voltage / 10.0;

	voltage = read_uint16(&(sr->sml_buf[VOLTAGE_L3_START]));
	m->voltageL3 = (double) voltage / 10.0;
}

bool smlReader_nextMeasurement(struct smlReader *sr, struct measurement *m)
{
	assert(sr != NULL);
	assert(sr->fd >= 0);
	assert(m != NULL);

	if (readSmlFile(sr) == false) {
		return false;
	}

	read_measurements(sr, m);
	clock_gettime(CLOCK_REALTIME, &m->timestamp);

	return true;
}
