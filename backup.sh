#!/bin/bash
# Copyright © 2021 Maximilian Wenzkowski

set -o pipefail

backup_day() {
	echo "Backup of day $date:"

	local csv_dir="tabelle_stromzähler"
	local csv_file="$csv_dir/$date.csv.zst"

	[ -f "$csv_file" ] && {
		echo "Error: The file '$csv_file' already exists!"
		exit 1
	}

	mkdir -p "$csv_dir" || {
		echo "Error: The folder '$csv_dir' could not be created!"
		exit 1
	}

	local start="$(date --date="$date" --rfc-3339=seconds)"
	local end="$(date --date="$date + 1 day" --rfc-3339=seconds)"

	psql --host=192.168.2.80 --username=stromzähler --dbname=stromzähler \
		--csv \
		--command="SELECT timestamp,energy,power_total,power_phase1,power_phase2,power_phase3 FROM stromzähler WHERE timestamp >= '$start' AND timestamp < '$end';" |
		zstdmt -q -o "$csv_file" || {
			echo "Error: Creating the compressed csv file '$csv_file' failed!"
			rm "$csv_file"
			exit 1
		}
	echo Ok
}

readonly LAST_DATE_FILE="./.last_day"

if ! [ -f "$LAST_DATE_FILE" ]; then
	echo "Error: The file '$LAST_DATE_FILE' does not exist!"
	exit 1
fi

last_date="$(< "$LAST_DATE_FILE")"
curr_date="$(date --iso-8601=date)"

date="$(date --date="$last_date + 1 day" --iso-8601=date)"

while [[ "$date" < "$curr_date" ]]
do
	backup_day
	echo "$date" > "$LAST_DATE_FILE"
	date="$(date --date="$date + 1 day" --iso-8601=date)"
done

echo "Backup table of daily energy usage:"
	psql --host=192.168.2.80 --username=stromzähler --dbname=stromzähler \
		--csv --command="SELECT * FROM tagesverbrauch;" |
		zstdmt -q -f -o "tabelle_tagesverbrauch.csv.zst" || {
			echo "Error: Creating the compressed csv file failed!"
			exit 1
		}
echo Ok

echo "Backup table of monthly energy usages:"
	psql --host=192.168.2.80 --username=stromzähler --dbname=stromzähler \
		--csv --command="SELECT * FROM monatsverbrauch;" |
		zstdmt -q -f -o "tabelle_monatsverbrauch.csv.zst" || {
			echo "Error: Creating the compressed csv file failed!"
			exit 1
		}
echo Ok
