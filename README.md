# Stromzähler

This is a personal project to read the data of an electronic electricity meter,
store it in a database and visualize it.

## Overview

The electricity meter I have at home has an infrared serial interface with
which it sends every second

* the amount of used energy (in kWh)
* the total power of all three phases (in W)
* the power of each phase (in W)
* the voltage of each phase (in V)

using the [SML](https://de.wikipedia.org/wiki/Smart_Message_Language) Protocol.

To read this data I use a single board computer, the Raspberry Pi Model B
(running Linux) and a custom circuit that uses a phototransistor to receive the
infrared signal and is connected to the serial port of the Raspberry Pi's GPIO
header.

This project implements a program written in C, that reads from the serial
interface, parses the values of the electricity meter and stores it in the
database. This program is started and logged via the service manager
[systemd](https://systemd.io/).

As database [PostgreSQL](https://www.postgresql.org/) is used. There are
multiple tables stored in the database. The main table stores each meter value
with the corresponding timestamp. Then there are tables to store the daily and
monthly energy consumption. Moreover a table with only one row holds the
current meter values, and the daily energy usage up to the current moment. This
table is used for better performance, since it is more efficient to fetch the
data from this table than to query the last row in the main table.

The main table is partitioned. This means the table consists of multiple
smaller tables, each holding the values for a specific month. This improves the
query performance and allows to easily remove old partitions.

Another component of this project is a shell script that is executed daily to
do maintenance work (create new partitions if necessary) and calculate the
daily/monthly energy usage and insert it in the corresponding tables. This
script is scheduled with
[systemd timers](https://wiki.archlinux.org/title/Systemd/Timers).

A detailed description of what I have done to setup the database on the
Raspberry Pi and build/install the program can be found
[here](documentation.md).

To backup the database from another computer over the network an additional
shell script is used.

At last [Grafana](https://grafana.com/grafana/) is used to visualize and
analyze the data.
