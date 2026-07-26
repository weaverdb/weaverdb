#!/usr/bin/env bash
# End-to-end smoke test: vector type, ivfflat/hnsw indexes, ORDER BY distance.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${PGVECTOR_BUILD_DIR:-$ROOT/build_test}"
BINDIR="$BUILD/mtpg/bin"
PG="$BINDIR/postgres"
INITDB="$BINDIR/initdb"

if [[ ! -x "$PG" ]]; then
  echo "postgres binary not found at $PG (set PGVECTOR_BUILD_DIR or run cmake build)" >&2
  exit 1
fi

TESTDIR="$(mktemp -d "${TMPDIR:-/tmp}/pgvector_smoke.XXXXXX")"
cleanup() { rm -rf "$TESTDIR"; }
trap cleanup EXIT

"$INITDB" -D "$TESTDIR" >/dev/null

run_sql() {
  printf '%s\n' "$@" | "$PG" -D "$TESTDIR" template1 2>&1
}

failures=0
check() {
  local desc="$1"
  local out="$2"
  local pattern="$3"
  if echo "$out" | grep -qE "$pattern"; then
    echo "OK: $desc"
  else
    echo "FAIL: $desc" >&2
    echo "$out" >&2
    failures=$((failures + 1))
  fi
}

out=$(run_sql "create table pv_smoke (id int, emb vector);")
check "create table" "$out" 'CREATE|backend>'

out=$(run_sql \
  "insert into pv_smoke values (1, '[1,0,0]');" \
  "insert into pv_smoke values (2, '[0,1,0]');" \
  "insert into pv_smoke values (3, '[0,0,1]');")
check "insert rows" "$out" 'INSERT'

out=$(run_sql "create index pv_smoke_ivf on pv_smoke using ivfflat (emb vector_l2_ops) with (lists = 2);")
if echo "$out" | grep -qi ERROR; then
  echo "FAIL: ivfflat index" >&2
  echo "$out" >&2
  failures=$((failures + 1))
else
  echo "OK: ivfflat index"
fi

out=$(run_sql "create index pv_smoke_hnsw on pv_smoke using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32);")
if echo "$out" | grep -qi ERROR; then
  echo "FAIL: hnsw index" >&2
  echo "$out" >&2
  failures=$((failures + 1))
else
  echo "OK: hnsw index"
fi

out=$(run_sql "select id from pv_smoke order by emb <-> '[1,0,0]' limit 2;")
check "order by distance returns id=1" "$out" 'id = "1"'
check "order by distance returns id=2" "$out" 'id = "2"'

out=$(run_sql \
  "insert into pv_smoke values (10, '[1,1,0]');" \
  "update pv_smoke set emb = '[1,0,0]' where id = 2;" \
  "delete from pv_smoke where id = 3;" \
  "insert into pv_smoke (id, emb) values (11, null);" \
  "select count(*) from pv_smoke where emb is null;" \
  "vacuum pv_smoke;")
check "mutations after indexes" "$out" 'INSERT|UPDATE|DELETE|VACUUM|backend>'
check "null emb count" "$out" 'count = "1"'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
"$SCRIPT_DIR/pgvector_orderby_smoke.sh"

if [[ "$failures" -ne 0 ]]; then
  echo "$failures check(s) failed" >&2
  exit 1
fi

echo "pgvector smoke test passed"
