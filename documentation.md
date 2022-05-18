# 1. Configure GPU memory usage of the Raspberry Pi

The Raspberry Pi 1B has just 512 MB RAM. Set the portion that is reseverd for
the GPU to the minimum of 16MB to have more memory available for programs and
the operating system. To do that edit the file `/boot/config.txt` and change
the corresponding line to:

	gpu_mem=16

Warning: With this setting there are not all features of the GPU available.


# 2. Install PostgreSQL

```sh
$ sudo apt install postgresql postgresql-client libpq-dev
```

This creates the Linux user `postgres`, the postgres user `postgres`
(superuser) and the data base cluster `/var/lib/postgresql/11/main`.

The existing data base clusters can be listed with: `pg_lsclusters`

**Stop service**: `sudo systemctl stop postgresql@11-main`

**Start service**: `sudo systemctl start postgresql@11-main`

**Restart service**: `sudo systemctl restart postgresql@11-main`


# 3. Configure PostgreSQL

## /etc/postgresql/11/main/postgresql.conf

Edit the file `/etc/postgresql/11/main/postgresql.conf` (as root) and change the
following settings:

	listen_addresses = '*'

	max_connections = 20
	shared_buffers = 76MB
	work_mem = 6MB
	maintenance_work_mem = 24MB
	effective_cache_size = 188MB
	max_parallel_maintenance_workers = 0
	max_parallel_workers_per_gather = 0
	max_parallel_workers = 0


Information to the chosen numbers:

**shared_buffers**:
With at least 1Gb RAM 25% of the RAM size is optimal, with less one can start
with 15% and can go upto 25%.

	(512-16) * 0.15 = 74,4 MB ~ 76 MB

**work_mem**:
The memory amount that can be use per database connection. Optimal size is (25%
of RAM) / `max_connections`.

	(512-16) * 0.25 = 124
	124 / 20 = 6,2 MB ~ 6 MB

**maintenance_work_mem**:
Memory amount that can be used for maintenance work. It is recommended to set
this value higher than `work_mem`, this improves the VACUUM performance. In
general this value should be 5% of the RAM size.

	(512-16) * 0.05 = 24,8 MB ~ 24 MB

**effective_cache_size**:
Estimates how much RAM can be used by the operating system for caching of the
hard drive and the database. Ideal is 50% of the RAM. We intend to use 60MB for
the OS and Grafana.

	(512-16) / 2 - 60 = 188 MB


**max_parallel_\***:
The Raspberry Pi 1B has only one CPU core, hence parallel workers doesn't make
sense.


## /etc/postgresql/11/main/pg_hba.conf

Edit the file `/etc/postgresql/11/main/pg_hba.conf` (as root) and make the
following settings:

	# TYPE  DATABASE        USER            ADDRESS                 METHOD

	# "local" is for Unix domain socket connections only
	local   all             all                                     trust


This change allows every local linux user to connect as every database user
(besides the DB user `postgres`). Only the linux user `postgres` is allowed to
connect as database user `postgres` because of the default setting in the line
above the changed line:

	local   all             postgres                                peer

Allow connections as DB user `stromzähler` to the database `stromzähler` over
the network:

	host    stromzähler     stromzähler     192.168.2.0/24          trust

## Restart data base

After these changes postgresql must be restarted to apply them:

```sh
$ sudo systemctl restart postgresql@11-main
```


# 2. Create database user and database

```sh
$ sudo -iu postgres
[postgres]$ createuser stromzähler
[postgres]$ createdb -O stromzähler stromzähler
[postgres]$ exit
```


# 3. Create tables

Connect as database user `stromzähler` to the database `stromzähler` with the
command-line client `psql`:

```sh
$ psql -U stromzähler -d stromzähler
```

Then execute the following SQL-Commands to create the tables and indixes.

## Partitioned table 'stromzähler'

```sql
CREATE TABLE stromzähler(
	id INTEGER GENERATED ALWAYS AS IDENTITY,
	timestamp TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
	energy DOUBLE PRECISION NOT NULL,
	power_total INTEGER NOT NULL,
	power_phase1 SMALLINT NOT NULL,
	power_phase2 SMALLINT NOT NULL,
	power_phase3 SMALLINT NOT NULL)
PARTITION BY RANGE(timestamp);

CREATE TABLE stromzähler_2019_10
PARTITION OF stromzähler
FOR VALUES FROM ('2019-10-01') TO ('2019-11-01');

CREATE TABLE stromzähler_2019_11
PARTITION OF stromzähler
FOR VALUES FROM ('2019-11-01') TO ('2019-12-01');

...

CREATE TABLE stromzähler_2020_12
PARTITION OF stromzähler
FOR VALUES FROM ('2020-12-01') TO ('2021-01-01');


CREATE TABLE stromzähler_default PARTITION OF stromzähler DEFAULT;

CREATE INDEX idx_stromzähler_timestamp
ON stromzähler
USING BRIN(timestamp) WITH (pages_per_range=4, autosummarize=true);
```

## Table for current values

```sql
CREATE TABLE current_values(
	id INTEGER,
	timestamp TIMESTAMPTZ NOT NULL,
	energy DOUBLE PRECISION,
	energy_daily DOUBLE PRECISION);
```

Crate a row with id=0, since the program expects it to exist.

```sql
INSERT INTO current_values VALUES(0, CURRENT_TIMESTAMP, NULL, NULL);
```

## Table for daily energy usage

```sql
CREATE TABLE tagesverbrauch(
	date DATE NOT NULL,
	energy DOUBLE PRECISION);

CREATE INDEX idx_tagesverbrauch_date ON tagesverbrauch(date);
```

## Table for monthly energy usage

```sql
CREATE TABLE monatsverbrauch(
	date DATE NOT NULL,
	energy DOUBLE PRECISION);

CREATE INDEX idx_monatsverbrauch_date ON monatsverbrauch(date);
```

# 4. Insert CSV backup file

	$psql -U stromzähler -d stromzähler
	stromzähler=> \copy stromzähler(timestamp, energy, power_total, power_phase1, power_phase2, power_phase3) from '/home/pi/csv_backup/all_up_to_2020-11-01.csv' DELIMITER ',' CSV HEADER


# 5. Compile program

Execute `make` in the root folder of this project:

```sh
$ make
```

This compiles the program into a binary with the name `stromzähler`.


# 6. Start the program automatically at boot

```sh
$ sudo cp stromzaehler.service /etc/systemd/system/
```

Then modify the path to the compiled binary in the file
`/etc/systemd/system/stromzaehler.service` if necessary. To to that edit the line

	ExecStart=/home/pi/stromzähler/stromzaehler

To reload the changed unit file execute
```sh
$ sudo systemctl daemon-reload
```

To activate starting the service at boot time and to start it immediately execute
```sh
$ sudo systemctl enable stromzaehler.service
$ sudo systemctl start stromzaehler.service
```

A single command that does both is
```sh
	$ sudo systemctl enable --now stromzaehler.service
```


# 7. Execute the script stromzaehler_daily.sh daily

```sh
$ sudo cp stromzaehler_daily.{service,timer} /etc/systemd/system/
```


Then change the path to the script in the file
`/etc/systemd/system/stromzaehler_daily.service` if necessary. To do that edit
the line

	ExecStart=/home/pi/stromzähler/stromzaehler_daily.sh

To reload the changed unit file execute

```sh
$ sudo systemctl daemon-reload
```

Activate and start the timer:

```sh
$ sudo systemctl enable --now stromzaehler_daily.timer
```

# 8. Remove partitions that are no longer needed

Connect as database user `stromzähler` to the database `stromzähler` with the
command-line client `psql`:

```sh
$ psql -U stromzähler -d stromzähler
```

At first one needs to detach the desired partition:
```sql
ALTER TABLE stromzähler DETACH PARTITION stromzähler_yyyy_mm;
```

Then one can delete the partition with
```sql
DROP TABLE stromzähler_yyyy_mm;
```
