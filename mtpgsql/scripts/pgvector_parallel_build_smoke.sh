#!/usr/bin/env bash
# Prove pthread parallel HNSW build + IVFFlat assign actually run (NOTICE lines)
# and that ANN order-by stays correct afterward.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${PGVECTOR_BUILD_DIR:-$ROOT/build_test}"
BINDIR="$BUILD/mtpg/bin"
PG="$BINDIR/postgres"
INITDB="$BINDIR/initdb"

ROWS="${PGVECTOR_PARALLEL_ROWS:-3000}"
DIM=32
WORKERS=2
MIN_PAGES=$((WORKERS * 8))

if [[ ! -x "$PG" ]]; then
  echo "postgres binary not found at $PG" >&2
  exit 1
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/pgvector_parallel.XXXXXX")"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

"$INITDB" -D "$TMP/data" >/dev/null

run_sql_file() {
  "$PG" -S 131072 -D "$TMP/data" template1 <"$1" 2>&1
}

run_session() {
  printf '%s\n' "$@" | "$PG" -S 131072 -D "$TMP/data" template1 2>&1
}

# Spike on axis (id % DIM), unique last coordinate = id (keeps nearest unambiguous).
vec() {
  local id="$1"
  local axis=$((id % DIM))
  local parts=()
  local d
  for ((d = 0; d < DIM; d++)); do
    if ((d == DIM - 1)); then
      parts+=("$id")
    elif ((d == axis)); then
      parts+=("1")
    else
      parts+=("0")
    fi
  done
  local IFS=,
  echo "[${parts[*]}]"
}

echo "--- parallel build smoke: rows=$ROWS workers=$WORKERS ---"

LOAD="$TMP/load.sql"
{
  echo "create table pv_par_h (id int, emb vector);"
  echo "create table pv_par_i (id int, emb vector);"
  i=0
  while ((i < ROWS)); do
    v="$(vec "$i")"
    echo "insert into pv_par_h values ($i, '$v');"
    echo "insert into pv_par_i values ($i, '$v');"
    i=$((i + 1))
  done
  echo "vacuum pv_par_h;"
  echo "vacuum pv_par_i;"
  echo "select relpages from pg_class where relname = 'pv_par_h';"
  echo "select relpages from pg_class where relname = 'pv_par_i';"
} >"$LOAD"

echo "loading $ROWS rows × 2 tables..."
LOAD_OUT=$(run_sql_file "$LOAD")
if echo "$LOAD_OUT" | grep -qiE 'ERROR:|FATAL:'; then
  echo "FAIL: load" >&2
  echo "$LOAD_OUT" >&2
  exit 1
fi

PAGE_VALS=$(echo "$LOAD_OUT" | grep -oE 'relpages = "[0-9]+"' | sed 's/[^0-9]//g')
H_PAGES=$(echo "$PAGE_VALS" | sed -n '1p')
I_PAGES=$(echo "$PAGE_VALS" | sed -n '2p')
H_PAGES="${H_PAGES:-0}"
I_PAGES="${I_PAGES:-0}"
echo "heap pages: hnsw=$H_PAGES ivf=$I_PAGES (need >=$MIN_PAGES)"
if [[ "$H_PAGES" -lt "$MIN_PAGES" || "$I_PAGES" -lt "$MIN_PAGES" ]]; then
  echo "FAIL: not enough heap pages for parallel path" >&2
  exit 1
fi

Q="$(vec 1)"
echo "building HNSW with build_workers=$WORKERS..."
HNSW_OUT=$(run_session \
  "set hnsw.build_workers = $WORKERS;" \
  "create index pv_par_h_idx on pv_par_h using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32);" \
  "set hnsw.build_workers = 1;" \
  "set hnsw.ef_search = 100;" \
  "set enable_seqscan = off;" \
  "select id from pv_par_h order by emb <-> '$Q' limit 1;")
if echo "$HNSW_OUT" | grep -qiE 'ERROR:|FATAL:'; then
  echo "FAIL: parallel hnsw build" >&2
  echo "$HNSW_OUT" >&2
  exit 1
fi
if ! echo "$HNSW_OUT" | grep -Fq "hnsw parallel build: $WORKERS workers"; then
  echo "FAIL: missing HNSW parallel NOTICE (serial fallback?)" >&2
  echo "$HNSW_OUT" >&2
  exit 1
fi
if ! echo "$HNSW_OUT" | grep -Fq 'id = "1"'; then
  echo "FAIL: HNSW ANN nearest != 1" >&2
  echo "$HNSW_OUT" >&2
  exit 1
fi
echo "OK: parallel HNSW build + ANN"

LISTS=$(python3 -c "import math; print(max(4, int(round(math.sqrt($ROWS)))))")
PROBES=$(python3 -c "print(max(4, min($LISTS, max(1, $LISTS // 4))))")
echo "building IVFFlat with assign_workers=$WORKERS lists=$LISTS probes=$PROBES..."
IVF_OUT=$(run_session \
  "set ivfflat.assign_workers = $WORKERS;" \
  "create index pv_par_i_idx on pv_par_i using ivfflat (emb vector_l2_ops) with (lists = $LISTS);" \
  "set ivfflat.assign_workers = 1;" \
  "set ivfflat.probes = $PROBES;" \
  "set enable_seqscan = off;" \
  "select id from pv_par_i order by emb <-> '$Q' limit 1;")
if echo "$IVF_OUT" | grep -qiE 'ERROR:|FATAL:'; then
  echo "FAIL: parallel ivfflat build" >&2
  echo "$IVF_OUT" >&2
  exit 1
fi
if ! echo "$IVF_OUT" | grep -Fq "ivfflat parallel assign: $WORKERS workers"; then
  echo "FAIL: missing IVFFlat parallel NOTICE (serial fallback?)" >&2
  echo "$IVF_OUT" >&2
  exit 1
fi
if ! echo "$IVF_OUT" | grep -Fq 'id = "1"'; then
  echo "FAIL: IVFFlat ANN nearest != 1" >&2
  echo "$IVF_OUT" >&2
  exit 1
fi
echo "OK: parallel IVFFlat assign + ANN"
echo "ALL PASS"
