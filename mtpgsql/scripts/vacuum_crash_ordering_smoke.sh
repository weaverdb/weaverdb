#!/usr/bin/env bash
# Vacuum / index crash-ordering smoke tests.
#
# Exercises the index-first vacuum barrier:
#   1) Safe crash after durable index cleanup (before heap) — restart + vacuum OK
#   2) Unsafe skip-barrier heap cleanup — restart + vacuum/recover removes orphans
#   3) Normal delete + vacuum + uniqueness re-insert
#
# Usage:
#   ./mtpgsql/scripts/vacuum_crash_ordering_smoke.sh [MTPG_PREFIX] [DATADIR]
#
# MTPG_PREFIX defaults to build/mtpg (relative to repo root).
# DATADIR defaults to /tmp/weaver-vacuum-crash-$$.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MTPG="${1:-$ROOT/build/mtpg}"
DATADIR="${2:-/tmp/weaver-vacuum-crash-$$}"
PSQL="$MTPG/bin/postgres"
INITDB="$MTPG/bin/initdb"

if [[ ! -x "$PSQL" || ! -x "$INITDB" ]]; then
  echo "missing mtpg binaries under $MTPG (build first)" >&2
  exit 1
fi

cleanup() {
  unset WEAVER_VACUUM_CRASH_POINT || true
  rm -rf "$DATADIR"
}
trap cleanup EXIT

rm -rf "$DATADIR"
"$INITDB" -D "$DATADIR" >/dev/null
"$PSQL" -D "$DATADIR" -o /dev/null template1 <<'SQL'
create database vaccrash;
SQL

run_sql() {
  local db="$1"
  shift
  "$PSQL" -D "$DATADIR" -o /dev/null "$db" <<SQL
$@
SQL
}

echo "== setup table + index =="
run_sql vaccrash "
create table vac_ord (id int4, val varchar(64));
create unique index vac_ord_id_idx on vac_ord (id);
insert into vac_ord values (1, 'a');
insert into vac_ord values (2, 'b');
insert into vac_ord values (3, 'c');
insert into vac_ord values (4, 'd');
insert into vac_ord values (5, 'e');
delete from vac_ord where id = 2;
delete from vac_ord where id = 4;
select id from vac_ord order by id;
"

echo "== scenario A: crash after index barrier, before heap =="
set +e
WEAVER_VACUUM_CRASH_POINT=after_index_barrier \
  "$PSQL" -D "$DATADIR" -o /dev/null vaccrash <<'SQL'
vacuum vac_ord;
SQL
rc=$?
set -e
if [[ $rc -eq 0 ]]; then
  echo "expected FATAL from vacuum_crash_point=after_index_barrier" >&2
  exit 1
fi
echo "crash A observed (rc=$rc); restarting vacuum"
unset WEAVER_VACUUM_CRASH_POINT
run_sql vaccrash "vacuum vac_ord;"
run_sql vaccrash "
select id from vac_ord order by id;
insert into vac_ord values (2, 'b2');
select id from vac_ord where id = 2;
"

echo "== scenario B: unsafe heap-before-barrier crash + recover =="
run_sql vaccrash "
delete from vac_ord where id in (1, 3);
"
set +e
WEAVER_VACUUM_CRASH_POINT=skip_barrier_heap_then_crash \
  "$PSQL" -D "$DATADIR" -o /dev/null vaccrash <<'SQL'
vacuum vac_ord;
SQL
rc=$?
set -e
if [[ $rc -eq 0 ]]; then
  echo "expected FATAL from skip_barrier_heap_then_crash" >&2
  exit 1
fi
echo "crash B observed (rc=$rc); vacuum/recover should clear orphans"
unset WEAVER_VACUUM_CRASH_POINT
run_sql vaccrash "vacuum vac_ord;"
run_sql vaccrash "
insert into vac_ord values (1, 'a2');
select id, val from vac_ord order by id;
"

echo "== scenario C: normal vacuum uniqueness =="
run_sql vaccrash "
delete from vac_ord where id = 5;
vacuum vac_ord;
insert into vac_ord values (5, 'e2');
select count(*) from vac_ord where id = 5;
"

echo "vacuum_crash_ordering_smoke: OK"
