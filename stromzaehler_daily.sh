#!/bin/bash
# Copyright © 2021 Maximilian Wenzkowski

# -e: Exit script immediatly if a command fails
# -u: handle unknown variables like an error
# -o pipefail: If (at least) one command in a chain of pipe-commands has a
#              non zero exit code, the exit code of the rightmost failed
#              command is returned as the exit code of the whole pipe chain
set -euo pipefail

readonly db_user="stromzähler"
readonly db_name="stromzähler"
readonly table_basename="stromzähler"
readonly table_default="${table_basename}_default"
readonly timestamp_column="timestamp"
readonly tagesverbrauch_table="tagesverbrauch"
readonly monatsverbrauch_table="monatsverbrauch"

function has_table {
	local -r table_name="$1"
	local result

	[ -z "$table_name" ] && return 1

	result="$(psql -U "$db_user" -d "$db_name" --tuples-only --no-align \
		-c "SELECT tablename FROM pg_catalog.pg_tables WHERE schemaname = 'public' AND tablename = '$table_name';")"

	! [ -z "$result" ]
}

function create_partition_if_necessary {
	local -r year="$1"
	[ -z "$year" ] && return 1

	local -r month="$2"
	[ -z "$month" ] && return 1

	local -r table="${table_basename}_${year}_${month}"

	if ! has_table "$table"
	then
		local -r from="${year}-${month}-01"
		local -r to="$(date "--date=$from +1 month" +%F)"
		psql -U "$db_user" -d "$db_name" <<EOF
BEGIN;
ALTER TABLE $table_basename DETACH PARTITION $table_default;
CREATE TABLE $table PARTITION OF $table_basename
	FOR VALUES FROM ('$from') TO ('$to');
INSERT INTO $table (
	SELECT * FROM $table_default
	WHERE $timestamp_column >= '$from' AND $timestamp_column < '$to'
);
DELETE FROM $table_default
WHERE $timestamp_column >= '$from' AND $timestamp_column < '$to';
ALTER TABLE $table_basename ATTACH PARTITION $table_default DEFAULT;
COMMIT;
EOF
	fi
}

function update_partitioned_table {
	local -r now="$(date +%F)" # Format: yyyy-mm-dd

	local -r year_current="${now%%-*}" # yyyy
	local -r temp="${now#*-}" # mm-dd
	local -r month_current="${temp%%-*}" # dd

	local -r year_next="$(date "--date=${year_current}-${month_current}-01 +1 month" +%Y)"
	local -r month_next="$(date "--date=${year_current}-${month_current}-01 +1 month" +%m)"

	create_partition_if_necessary "$year_current" "$month_current"
	create_partition_if_necessary "$year_next" "$month_next"
}

function insert_daily_energy_consumption {
	local -r date="$1"
	local -r prev="$(date "--date=$date -1 day" +%F)"
	local -r next="$(date "--date=$date +1 day" +%F)"

	psql -U "$db_user" -d "$db_name" <<EOF
INSERT INTO $tagesverbrauch_table
SELECT '$date'::DATE,
	(SELECT energy
	FROM stromzähler
	WHERE timestamp >= '$date 23:59:00' AND timestamp < '$next'
	ORDER BY timestamp DESC LIMIT 1)
	-
	(SELECT energy
	FROM stromzähler
	WHERE timestamp >= '$prev 23:59:00' AND timestamp < '$date'
	ORDER BY timestamp DESC LIMIT 1);
EOF
}

function insert_missing_daily_energy_consumption {

	local -r today="$(date +%F)"

	local date="$(psql -U "$db_user" -d "$db_name" --tuples-only --no-align \
		-c "SELECT date FROM $tagesverbrauch_table ORDER BY date DESC LIMIT 1;")"
	if [ -z "$date" ]
	then
		date="2019-10-17" # First day with known energy usage
	else
		date="$(date "--date=$date +1 day" +%F)"
	fi

	# Abort if the last date in the data base is older or equal to todays date
	local -r today_sec="$(date -d "$today" +%s)"
	local -r date_sec="$(date -d "$date" +%s)"
	[ "$date_sec" -ge "$today_sec" ] && return

	while [ "$date" != "$today" ]
	do
		insert_daily_energy_consumption "$date"
		date="$(date "--date=$date + 1 day" +%F)"
	done

}

function insert_monthly_energy_consumption {
	local -r date="$1" # have to be in format yyyy-mm-dd

	local -r year="${date%%-*}" # yyyy
	local -r temp="${date#*-}" # mm-dd
	local -r month="${temp%%-*}" # dd

	local -r first_day="$year-$month-01"
	local -r last_day="$(date "--date=$first_day +1 month -1 day" +%F)"

	local -r prev="$(date "--date=$first_day -1 day" +%F)"
	local -r next="$(date "--date=$last_day +1 day" +%F)"

	psql -U "$db_user" -d "$db_name" <<EOF
INSERT INTO $monatsverbrauch_table
SELECT '$date'::DATE,
	(SELECT energy
	FROM stromzähler
	WHERE timestamp >= '$last_day 23:59:00' AND timestamp < '$next'
	ORDER BY timestamp DESC LIMIT 1)
	-
	(SELECT energy
	FROM stromzähler
	WHERE timestamp >= '$prev 23:59:00' AND timestamp < '$first_day'
	ORDER BY timestamp DESC LIMIT 1);
EOF
}

function insert_missing_monthly_energy_consumption {

	local -r current_month="$(date +%Y-%m-01)"
	local -r prev_month="$(date "--date=$current_month -1 month" +%Y-%m-01)"

	local last_month="$(psql -U "$db_user" -d "$db_name" --tuples-only --no-align \
		-c "SELECT date FROM $monatsverbrauch_table ORDER BY date DESC LIMIT 1;")"

	if [ -z "$last_month" ]
	then
		# After this month follows the first month with known energy usage
		last_month="2019-10-01"
	fi
	last_month="$(date "--date=$last_month" +%Y-%m-01)"
	local month="$(date "--date=$last_month + 1 month" +%Y-%m-01)"

	# Abort if the last date in the data base is older or equal to todays date
	local -r prev_month_sec="$(date -d "$prev_month" +%s)"
	local -r month_sec="$(date -d "$month" +%s)"
	[ "$month_sec" -gt "$prev_month_sec" ] && return

	while [ "$month" != "$current_month" ]
	do
		echo $month
		insert_monthly_energy_consumption "$month"
		month="$(date "--date=$month + 1 month" +%Y-%m-01)"
	done

}


update_partitioned_table
insert_missing_daily_energy_consumption
insert_missing_monthly_energy_consumption
