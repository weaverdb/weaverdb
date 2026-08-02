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

assert_no_error() {
  local desc="$1"
  local out="$2"
  if echo "$out" | grep -qi ERROR; then
    echo "FAIL: $desc" >&2
    echo "$out" >&2
    failures=$((failures + 1))
    return 1
  fi
  return 0
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
got=$(ids_from_output "$out")
first=$(echo "$got" | sed -n '1p')
second=$(echo "$got" | sed -n '2p')
third=$(echo "$got" | sed -n '3p')
if [[ "$first" == "1" && ( "$second" == "2" || "$second" == "3" ) && ( "$third" == "2" || "$third" == "3" ) && "$second" != "$third" ]]; then
  echo "OK: L2 order to [1,0,0] limit 3 (1 then tie 2|3)"
else
  echo "FAIL: L2 order to [1,0,0] limit 3 (got: $(echo "$got" | tr '\n' ' '))" >&2
  failures=$((failures + 1))
fi

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
got=$(ids_from_output "$out")
first=$(echo "$got" | sed -n '1p')
second=$(echo "$got" | sed -n '2p')
if [[ ( "$first" == "2" || "$first" == "3" ) && ( "$second" == "2" || "$second" == "3" ) && "$first" != "$second" ]]; then
  echo "OK: WHERE id>=2 then L2 order (tie 2|3)"
else
  echo "FAIL: WHERE id>=2 then L2 order (got: $(echo "$got" | tr '\n' ' '))" >&2
  failures=$((failures + 1))
fi

out=$(run_session \
  "select id from pv_ob where id in (1, 4) order by emb <-> '[1,0,0]';")
assert_ids "filter ids 1,4 nearest first" "$out" 1 4

out=$(run_session \
  "select id from pv_ob order by l2_distance(emb, '[1,0,0]') limit 2;")
got=$(ids_from_output "$out")
first=$(echo "$got" | sed -n '1p')
second=$(echo "$got" | sed -n '2p')
if [[ "$first" == "1" && ( "$second" == "2" || "$second" == "3" ) ]]; then
  echo "OK: ORDER BY l2_distance() expr (1 then tie 2|3)"
else
  echo "FAIL: ORDER BY l2_distance() expr (got: $(echo "$got" | tr '\n' ' '))" >&2
  failures=$((failures + 1))
fi

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
  got=$(ids_from_output "$out")
  first=$(echo "$got" | sed -n '1p')
  second=$(echo "$got" | sed -n '2p')
  if [[ "$first" == "1" && ( "$second" == "2" || "$second" == "3" ) ]]; then
    echo "OK: vector_ip_ops ORDER BY <#> limit 2 (1 then tie 2|3)"
  else
    echo "FAIL: vector_ip_ops ORDER BY <#> (got: $(echo "$got" | tr '\n' ' '))" >&2
    failures=$((failures + 1))
  fi
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
  got=$(ids_from_output "$out")
  first=$(echo "$got" | sed -n '1p')
  second=$(echo "$got" | sed -n '2p')
  if [[ "$first" == "1" && ( "$second" == "2" || "$second" == "3" ) ]]; then
    echo "OK: vector_cosine_ops ORDER BY <=> limit 2 (1 then tie 2|3)"
  else
    echo "FAIL: vector_cosine_ops ORDER BY <=> (got: $(echo "$got" | tr '\n' ' '))" >&2
    failures=$((failures + 1))
  fi
fi

echo "--- EXPLAIN: distance ORDER BY plan ---"

out=$(run_session \
  "set enable_seqscan = off;" \
  "explain select id from pv_ob order by emb <-> '[1,0,0]' limit 2;")
if echo "$out" | grep -qi ERROR; then
  echo "FAIL: EXPLAIN ORDER BY <->" >&2
  echo "$out" >&2
  failures=$((failures + 1))
elif echo "$out" | grep -q 'Index Scan'; then
  echo "OK: EXPLAIN uses Index Scan for L2 ORDER BY"
else
  echo "FAIL: EXPLAIN expected Index Scan for L2 ORDER BY" >&2
  echo "$out" >&2
  failures=$((failures + 1))
fi

out=$(run_session \
  "set enable_seqscan = off;" \
  "explain select id from pv_cos order by emb <=> '[1,0,0]' limit 2;")
if echo "$out" | grep -q 'Index Scan'; then
  echo "OK: EXPLAIN uses Index Scan for cosine ORDER BY"
else
  echo "FAIL: EXPLAIN expected Index Scan for cosine" >&2
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
  got=$(ids_from_output "$out")
  first=$(echo "$got" | sed -n '1p')
  second=$(echo "$got" | sed -n '2p')
  if [[ "$first" == "1" && ( "$second" == "2" || "$second" == "3" ) ]]; then
    echo "OK: halfvec_l2_ops ORDER BY <-> limit 2 (1 then tie 2|3)"
  else
    echo "FAIL: halfvec_l2_ops ORDER BY <-> (got: $(echo "$got" | tr '\n' ' '))" >&2
    failures=$((failures + 1))
  fi
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
  got=$(ids_from_output "$out")
  first=$(echo "$got" | sed -n '1p')
  second=$(echo "$got" | sed -n '2p')
  if [[ "$first" == "1" && ( "$second" == "2" || "$second" == "3" ) ]]; then
    echo "OK: sparsevec_l2_ops ORDER BY <-> limit 2 (1 then tie 2|3)"
  else
    echo "FAIL: sparsevec_l2_ops ORDER BY <-> (got: $(echo "$got" | tr '\n' ' '))" >&2
    failures=$((failures + 1))
  fi
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
  got=$(ids_from_output "$out")
  first=$(echo "$got" | sed -n '1p')
  second=$(echo "$got" | sed -n '2p')
  if [[ "$first" == "1" && ( "$second" == "2" || "$second" == "3" ) ]]; then
    echo "OK: bit_hamming_ops ORDER BY <~> limit 2 (1 then tie 2|3)"
  else
    echo "FAIL: bit_hamming_ops ORDER BY <~> (got: $(echo "$got" | tr '\n' ' '))" >&2
    failures=$((failures + 1))
  fi
fi

echo "--- HNSW iterative_scan under selective WHERE + ORDER BY ---"
# Filter keeps only odd ids; nearest to [1,0,0] among odds is id=1 ([1,0,0]).
# Without iterative scan, Index Scan may exhaust candidates before finding a
# filtered match; relaxed_order resumes discarded candidates.
out=$(run_session \
  "create table pv_iter (id int, keep bool, emb vector);" \
  "insert into pv_iter values (1, true, '[1,0,0]');" \
  "insert into pv_iter values (2, false, '[0.99,0.1,0]');" \
  "insert into pv_iter values (3, true, '[0,1,0]');" \
  "insert into pv_iter values (4, false, '[0.98,0.2,0]');" \
  "insert into pv_iter values (5, true, '[0,0,1]');" \
  "insert into pv_iter values (6, false, '[0.97,0.3,0]');" \
  "insert into pv_iter values (7, true, '[0.5,0.5,0]');" \
  "insert into pv_iter values (8, false, '[0.96,0.4,0]');" \
  "insert into pv_iter values (9, true, '[0.1,0.1,0.1]');" \
  "insert into pv_iter values (10, false, '[0.95,0.5,0]');" \
  "create index pv_iter_hnsw on pv_iter using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32);" \
  "set enable_seqscan = off;" \
  "set hnsw.ef_search = 10;" \
  "set hnsw.iterative_scan = off;" \
  "select id from pv_iter where keep order by emb <-> '[1,0,0]' limit 1;")
assert_no_error "iterative_scan off filtered setup" "$out"
# Even with off, small dataset usually finds id=1; assert filter correctness.
got=$(ids_from_output "$out")
first=$(echo "$got" | sed -n '1p')
if [[ "$first" == "1" ]]; then
  echo "OK: filtered ANN (iterative_scan=off) nearest keep=true is id=1"
else
  # Soft: off mode may miss; still require no ERROR and only keep=true ids if any
  if [[ -n "$first" ]] && [[ "$first" =~ ^(1|3|5|7|9)$ ]]; then
    echo "OK: filtered ANN (iterative_scan=off) returned keep=true id=$first"
  else
    echo "FAIL: iterative_scan=off filtered result (got: $(echo "$got" | tr '\n' ' '))" >&2
    echo "$out" >&2
    failures=$((failures + 1))
  fi
fi

out=$(run_session \
  "set enable_seqscan = off;" \
  "set hnsw.ef_search = 10;" \
  "set hnsw.iterative_scan = relaxed_order;" \
  "set hnsw.max_scan_tuples = 20000;" \
  "select id from pv_iter where keep order by emb <-> '[1,0,0]' limit 3;")
assert_no_error "iterative_scan relaxed filtered" "$out"
got=$(ids_from_output "$out")
first=$(echo "$got" | sed -n '1p')
# All returned must be keep=true odds; nearest must be 1
bad=0
while IFS= read -r line; do
  [[ -z "$line" ]] && continue
  if [[ ! "$line" =~ ^(1|3|5|7|9)$ ]]; then
    bad=1
  fi
done <<< "$got"
if [[ "$first" == "1" && "$bad" -eq 0 ]]; then
  echo "OK: iterative_scan=relaxed_order filtered top-3 starts with 1, only keep=true"
else
  echo "FAIL: iterative_scan=relaxed filtered (got: $(echo "$got" | tr '\n' ' '))" >&2
  echo "$out" >&2
  failures=$((failures + 1))
fi

out=$(run_session \
  "set enable_seqscan = off;" \
  "set hnsw.iterative_scan = strict_order;" \
  "select id from pv_iter where keep order by emb <-> '[1,0,0]' limit 1;")
assert_no_error "iterative_scan strict filtered" "$out"
assert_ids "iterative_scan=strict_order filtered nearest" "$out" 1

echo "--- IVFFlat iterative_scan under selective WHERE ---"
out=$(run_session \
  "create table pv_iter_ivf (id int, keep bool, emb vector);" \
  "insert into pv_iter_ivf values (1, true, '[1,0,0]');" \
  "insert into pv_iter_ivf values (2, false, '[0.99,0.1,0]');" \
  "insert into pv_iter_ivf values (3, true, '[0,1,0]');" \
  "insert into pv_iter_ivf values (4, false, '[0.5,0.5,0]');" \
  "insert into pv_iter_ivf values (5, true, '[0,0,1]');" \
  "create index pv_iter_ivf_idx on pv_iter_ivf using ivfflat (emb vector_l2_ops) with (lists = 2);" \
  "set enable_seqscan = off;" \
  "set ivfflat.probes = 2;" \
  "set ivfflat.iterative_scan = relaxed_order;" \
  "select id from pv_iter_ivf where keep order by emb <-> '[1,0,0]' limit 1;")
assert_no_error "ivfflat iterative_scan relaxed filtered" "$out"
assert_ids "ivfflat iterative_scan filtered nearest" "$out" 1

if [[ "$failures" -ne 0 ]]; then
  echo "$failures ORDER BY check(s) failed" >&2
  exit 1
fi

echo "pgvector ORDER BY smoke test passed"
