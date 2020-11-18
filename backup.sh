#!/bin/bash
# Copyright © 2020 Maximilian Wenzkowski

set -o pipefail

backup_day() {
	echo "Backup des Tages $date:"

	local csv_dir="tabelle_stromzähler"
	local csv_file="$csv_dir/$date.csv.zst"

	[ -f "$csv_file" ] && {
		echo "Fehler: Die Datei '$csv_file' existiert schon!"
		exit 1
	}

	mkdir -p "$csv_dir" || {
		echo "Fehler: Der Ordner '$csv_dir' konnte nicht erstellt werden!"
		exit 1
	}

	local start="$(date --date="$date" --rfc-3339=seconds)"
	local end="$(date --date="$date + 1 day" --rfc-3339=seconds)"

	psql --host=192.168.2.80 --username=stromzähler --dbname=stromzähler \
		--csv \
		--command="SELECT timestamp,energy,power_total,power_phase1,power_phase2,power_phase3 FROM stromzähler WHERE timestamp >= '2020-11-16' AND timestamp < '2020-11-17';" |
		zstdmt -q -o "$csv_file" || {
			echo "Fehler: Erstellen der komprimierten csv-Datei '$csv_file' fehlgeschlagen !"
			rm "$csv_file"
			exit 1
		}
	echo Ok
}

readonly LAST_DATE_FILE="./.last_day"

if ! [ -f "$LAST_DATE_FILE" ]; then
	echo "Fehler: Die Datei '$LAST_DATE_FILE' existiert nicht!"
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

echo "Backup der Tagesverbräuche:"
	psql --host=192.168.2.80 --username=stromzähler --dbname=stromzähler \
		--csv --command="SELECT * FROM tagesverbrauch;" |
		zstdmt -q -f -o "tabelle_tagesverbrauch.csv.zst" || {
			echo "Fehler: Erstellen der komprimierten csv-Datei fehlgeschlagen !"
			exit 1
		}
