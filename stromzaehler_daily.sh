#!/bin/bash
# Copyright © 2020 Maximilian Wenzkowski

# -e: Skript sofort beenden wenn ein Befehl fehlschlägt
# -u: Behandle unbekannte Variablen als Fehler
# -x: Gib jeden Befehl aus, bevor er ausgeführt wird
# -o pipefail: Hat (mind.) ein Befehl in einer Kette von Pipe-Befehlen einen
#              von Null verschiedenen Exit-Code, wird der von Null verschiedene
#              Exit-Code von dem am weitesten rechts stehenden Befehl als
#              Exit-Code der gesamten Pipe-Kette benutzt. Ansonsten ist der
#              Exit-Code Null.
set -euo pipefail

readonly db_user="stromzähler"
readonly db_name="stromzähler"
readonly table_basename="stromzähler"
readonly table_default="${table_basename}_default"
readonly timestamp_column="timestamp"

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


update_partitioned_table
