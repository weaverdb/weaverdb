#!/usr/bin/env bash
# ORDER BY vector distance (<->) regression checks (standalone postgres backend).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${PGVECTOR_BUILD_DIR:-$ROOT/build_test}"
BINDIR="$BUILD/mtpg/bin"
PG="$BINDIR/postgres"
INITDB="$BINDIR/initdb"

if [[ ! -x "$PG" ]]; then
  echo "postgres binary not found at $PG" >&2
  exit 1
fi

TESTDIR="$(mktemp -d "${TMPDIR:-/tmp}/pgvector_orderby.XXXXXX")"
cleanup() { rm -rf "$TESTDIR"; }
trap cleanup EXIT

"$INITDB" -D "$TESTDIR" >/dev/null

run_session() {
  printf '%s\n' "$@" | "$PG" -D "$TESTDIR" template1 2>&1
}

ids_from_output() {
  echo "$1" | grep -oE 'id = "[0-9]+"' | sed 's/id = "//;s/"//' || true
}

failures=0
assert_ids() {
  local desc="$1"
  local out="$2"
  shift 2
  local expected=("$@")
  local got=()
  local line
  while IFS= read -r line; do
    [[ -n "$line" ]] && got+=("$line")
  done < <(ids_from_output "$out")
  if [[ "${#got[@]}" -ne "${#expected[@]}" ]]; then
    echo "FAIL: $desc (got ${#got[@]} rows, want ${#expected[@]}): $(printf '%s ' "${got[@]+"${got[@]}"}")" >&2
    failures=$((failures + 1))
    return
  fi
  local i
  for i in "${!expected[@]}"; do
    if [[ "${got[$i]}" != "${expected[$i]}" ]]; then
      echo "FAIL: $desc (row $((i + 1)): got ${got[$i]}, want ${expected[$i]})" >&2
      echo "$out" >&2
      failures=$((failures + 1))
      return
    fi
  done
  echo "OK: $desc"
}

setup_out=$(run_session \
  "create table pv_ob (id int, emb vector);" \
  "insert into pv_ob values (1, '[1,0,0]');" \
  "insert into pv_ob values (2, '[0,1,0]');" \
  "insert into pv_ob values (3, '[0,0,1]');" \
  "insert into pv_ob values (4, '[9,0,0]');" \
  "create index pv_ob_ivf on pv_ob using ivfflat (emb vector_l2_ops) with (lists = 2);" \
  "create index pv_ob_hnsw on pv_ob using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32);")
if echo "$setup_out" | grep -qi ERROR; then
  echo "FAIL: setup" >&2
  echo "$setup_out" >&2
  exit 1
fi
echo "OK: setup table, rows, ivfflat + hnsw"

out=$(run_session "select id from pv_ob order by emb <-> '[1,0,0]' limit 3;")
assert_ids "L2 order to [1,0,0] limit 3" "$out" 1 2 3

out=$(run_session "select id from pv_ob order by emb <-> '[1,0,0]' limit 1;")
assert_ids "L2 order limit 1" "$out" 1

out=$(run_session "select id from pv_ob order by emb <-> '[0,0,1]' limit 2;")
got=$(ids_from_output "$out")
first=$(echo "$got" | sed -n '1p')
second=$(echo "$got" | sed -n '2p')
if [[ "$first" == "3" && ( "$second" == "1" || "$second" == "2" ) ]]; then
  echo "OK: L2 order to [0,0,1] limit 2 (3 then tie 1|2)"
else
  echo "FAIL: L2 order to [0,0,1] limit 2 (got: $(echo "$got" | tr '\n' ' '))" >&2
  failures=$((failures + 1))
fi

out=$(run_session \
  "select id from pv_ob where id >= 2 order by emb <-> '[1,0,0]' limit 2;")
assert_ids "WHERE id>=2 then L2 order" "$out" 2 3

out=$(run_session \
  "select id from pv_ob where id in (1, 4) order by emb <-> '[1,0,0]';")
assert_ids "filter ids 1,4 nearest first" "$out" 1 4

out=$(run_session \
  "select id from pv_ob order by l2_distance(emb, '[1,0,0]') limit 2;")
assert_ids "ORDER BY l2_distance() expr" "$out" 1 2

# Cosine on pv_ob: [9,0,0] is parallel to [1,0,0] so cosine distance is 0
out=$(run_session \
  "select id from pv_ob order by emb <=> '[1,0,0]' limit 2;")
assert_ids "cosine <=> order (seq) limit 2" "$out" 1 4

out=$(run_session \
  "create table pv_ip (id int, emb vector);" \
  "insert into pv_ip values (1, '[1,0,0]');" \
  "insert into pv_ip values (2, '[0,1,0]');" \
  "insert into pv_ip values (3, '[0,0,1]');" \
  "create index pv_ip_ivf on pv_ip using ivfflat (emb vector_ip_ops) with (lists = 2);" \
  "create index pv_ip_hnsw on pv_ip using hnsw (emb vector_ip_ops) with (m = 8, ef_construction = 32);" \
  "select id from pv_ip order by emb <#> '[1,0,0]' limit 2;")
if echo "$out" | grep -qi ERROR; then
  echo "FAIL: vector_ip_ops index + ORDER BY <#>" >&2
  echo "$out" >&2
  failures=$((failures + 1))
else
  assert_ids "vector_ip_ops ORDER BY <#> limit 2" "$out" 1 2
fi

out=$(run_session \
  "create table pv_cos (id int, emb vector);" \
  "insert into pv_cos values (1, '[1,0,0]');" \
  "insert into pv_cos values (2, '[0,1,0]');" \
  "insert into pv_cos values (3, '[0,0,1]');" \
  "create index pv_cos_ivf on pv_cos using ivfflat (emb vector_cosine_ops) with (lists = 2);" \
  "create index pv_cos_hnsw on pv_cos using hnsw (emb vector_cosine_ops) with (m = 8, ef_construction = 32);" \
  "select id from pv_cos order by emb <=> '[1,0,0]' limit 2;")
if echo "$out" | grep -qi ERROR; then
  echo "FAIL: vector_cosine_ops index + ORDER BY <=>" >&2
  echo "$out" >&2
  failures=$((failures + 1))
else
  assert_ids "vector_cosine_ops ORDER BY <=> limit 2" "$out" 1 2
fi

echo "--- EXPLAIN: distance ORDER BY plan ---"

out=$(run_session \
  "explain select id from pv_ob order by emb <-> '[1,0,0]' limit 2;")
if echo "$out" | grep -qi ERROR; then
  echo "FAIL: EXPLAIN ORDER BY <->" >&2
  echo "$out" >&2
  failures=$((failures + 1))
elif echo "$out" | grep -q 'QUERY PLAN' && echo "$out" | grep -qE 'Sort|Index Scan|Seq Scan'; then
  # ANN Index Scan is not yet selected for ORDER BY (neighbor-tuple build gap);
  # Seq Scan + Sort is the correct working plan today.
  echo "OK: EXPLAIN produces a distance ORDER BY plan"
else
  echo "FAIL: EXPLAIN missing expected plan nodes" >&2
  echo "$out" >&2
  failures=$((failures + 1))
fi

out=$(run_session \
  "explain select id from pv_cos order by emb <=> '[1,0,0]' limit 2;")
if echo "$out" | grep -q 'QUERY PLAN' && echo "$out" | grep -qE 'Sort|Index Scan|Seq Scan'; then
  echo "OK: EXPLAIN produces a cosine ORDER BY plan"
else
  echo "FAIL: EXPLAIN expected plan for cosine" >&2
  echo "$out" >&2
  failures=$((failures + 1))
fi

echo "--- medium-scale recall (200 points on a line) ---"

# Build insert batch for ids 0..199 with emb = [id, 0]
recall_sql="create table pv_recall (id int, emb vector);"
for i in $(seq 0 199); do
  recall_sql+=$'\n'"insert into pv_recall values ($i, '[$i,0]');"
done
recall_sql+=$'\n'"create index pv_recall_hnsw on pv_recall using hnsw (emb vector_l2_ops) with (m = 16, ef_construction = 64);"
recall_sql+=$'\n'"set hnsw.ef_search = 100;"
recall_sql+=$'\n'"select id from pv_recall order by emb <-> '[100,0]' limit 5;"

out=$(run_session "$recall_sql")
if echo "$out" | grep -qi ERROR; then
  echo "FAIL: recall setup/query" >&2
  echo "$out" >&2
  failures=$((failures + 1))
else
  # Exact top-5 around 100: 100, then {99,101}, then {98,102} — order among ties may vary
  got=$(ids_from_output "$out" | tr '\n' ' ')
  first=$(echo "$got" | awk '{print $1}')
  if [[ "$first" != "100" ]]; then
    echo "FAIL: recall nearest should be 100, got: $got" >&2
    echo "$out" >&2
    failures=$((failures + 1))
  else
    # All five results must be in {98,99,100,101,102}
    ok=1
    for id in $(ids_from_output "$out"); do
      if [[ "$id" -lt 98 || "$id" -gt 102 ]]; then
        ok=0
        break
      fi
    done
    count=$(ids_from_output "$out" | wc -l | tr -d ' ')
    if [[ "$ok" -eq 1 && "$count" -eq 5 ]]; then
      echo "OK: recall top-5 around 100 (ids: $got)"
    else
      echo "FAIL: recall top-5 not within {98..102}: $got" >&2
      echo "$out" >&2
      failures=$((failures + 1))
    fi
  fi
fi

out=$(run_session \
  "explain select id from pv_recall order by emb <-> '[100,0]' limit 5;")
if echo "$out" | grep -q 'QUERY PLAN'; then
  echo "OK: EXPLAIN on recall table"
else
  echo "FAIL: EXPLAIN on recall table" >&2
  echo "$out" >&2
  failures=$((failures + 1))
fi

out=$(run_session \
  "create table pv_hv (id int, emb halfvec);" \
  "insert into pv_hv values (1, '[1,0,0]');" \
  "insert into pv_hv values (2, '[0,1,0]');" \
  "insert into pv_hv values (3, '[0,0,1]');" \
  "create index pv_hv_ivf on pv_hv using ivfflat (emb halfvec_l2_ops) with (lists = 2);" \
  "create index pv_hv_hnsw on pv_hv using hnsw (emb halfvec_l2_ops) with (m = 8, ef_construction = 32);" \
  "select id from pv_hv order by emb <-> '[1,0,0]' limit 2;")
if echo "$out" | grep -qi ERROR; then
  echo "FAIL: halfvec_l2_ops index + ORDER BY <->" >&2
  echo "$out" >&2
  failures=$((failures + 1))
else
  assert_ids "halfvec_l2_ops ORDER BY <-> limit 2" "$out" 1 2
fi

out=$(run_session \
  "create table pv_sv (id int, emb sparsevec);" \
  "insert into pv_sv values (1, '{1:1}/3');" \
  "insert into pv_sv values (2, '{2:1}/3');" \
  "insert into pv_sv values (3, '{3:1}/3');" \
  "create index pv_sv_hnsw on pv_sv using hnsw (emb sparsevec_l2_ops) with (m = 8, ef_construction = 32);" \
  "select id from pv_sv order by emb <-> '{1:1}/3' limit 2;")
if echo "$out" | grep -qi ERROR; then
  echo "FAIL: sparsevec_l2_ops index + ORDER BY <->" >&2
  echo "$out" >&2
  failures=$((failures + 1))
else
  assert_ids "sparsevec_l2_ops ORDER BY <-> limit 2" "$out" 1 2
fi

out=$(run_session \
  "create table pv_bit (id int, emb varbit);" \
  "insert into pv_bit values (1, 'B100');" \
  "insert into pv_bit values (2, 'B010');" \
  "insert into pv_bit values (3, 'B001');" \
  "create index pv_bit_hnsw on pv_bit using hnsw (emb bit_hamming_ops) with (m = 8, ef_construction = 32);" \
  "select id from pv_bit order by emb <~> 'B100' limit 2;")
if echo "$out" | grep -qi ERROR; then
  echo "FAIL: bit_hamming_ops index + ORDER BY <~>" >&2
  echo "$out" >&2
  failures=$((failures + 1))
else
  assert_ids "bit_hamming_ops ORDER BY <~> limit 2" "$out" 1 2
fi

if [[ "$failures" -ne 0 ]]; then
  echo "$failures ORDER BY check(s) failed" >&2
  exit 1
fi

echo "pgvector ORDER BY smoke test passed"
