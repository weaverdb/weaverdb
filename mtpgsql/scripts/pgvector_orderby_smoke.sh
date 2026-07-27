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

# Cosine opclass ordering via <=> (sequential plan is fine for this smoke test)
out=$(run_session \
  "select id from pv_ob order by emb <=> '[1,0,0]' limit 2;")
if echo "$out" | grep -qi ERROR; then
  echo "FAIL: cosine <=> order" >&2
  echo "$out" >&2
  failures=$((failures + 1))
else
  got=$(ids_from_output "$out" | tr '\n' ' ')
  echo "OK: cosine <=> order (ids: ${got:-none})"
fi

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
