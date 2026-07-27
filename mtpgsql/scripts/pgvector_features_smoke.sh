#!/usr/bin/env bash
# Broad regression: pgvector SQL functions, comparisons, and index opclasses not covered by pgvector_orderby_smoke.sh.
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

TESTDIR="$(mktemp -d "${TMPDIR:-/tmp}/pgvector_features.XXXXXX")"
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

assert_scalar() {
  local desc="$1"
  local out="$2"
  local col="$3"
  local want="$4"
  if echo "$out" | grep -Fq "${col} = \"${want}\""; then
    echo "OK: $desc"
  else
    echo "FAIL: $desc (want ${col} = \"${want}\")" >&2
    echo "$out" >&2
    failures=$((failures + 1))
  fi
}

assert_no_error() {
  local desc="$1"
  local out="$2"
  if echo "$out" | grep -qiE 'ERROR:|syntax error'; then
    echo "FAIL: $desc" >&2
    echo "$out" >&2
    failures=$((failures + 1))
    return 1
  fi
  echo "OK: $desc"
  return 0
}

echo "--- distance and utility SQL functions ---"

out=$(run_session \
  "select vector_l2_squared_distance('[1,0,0]'::vector, '[0,1,0]'::vector);" \
  "select vector_negative_inner_product('[1,0,0]'::vector, '[1,0,0]'::vector);" \
  "select cosine_distance('[1,0,0]'::vector, '[0,1,0]'::vector);" \
  "select l2_normalize('[3,4,0]'::vector);")
assert_scalar "vector_l2_squared_distance" "$out" "vector_l2_squared_distance" "2"
assert_scalar "vector_negative_inner_product" "$out" "vector_negative_inner_product" "-1"
assert_scalar "cosine_distance orthogonal" "$out" "cosine_distance" "1"
if echo "$out" | grep -Fq 'l2_normalize = "[0.6,0.8,0]"'; then
  echo "OK: l2_normalize"
else
  echo "FAIL: l2_normalize" >&2
  echo "$out" >&2
  failures=$((failures + 1))
fi

out=$(run_session \
  "select halfvec_l2_squared_distance('[1,0,0]'::halfvec, '[0,1,0]'::halfvec);" \
  "select halfvec_l2_norm('[3,4,0]'::halfvec);" \
  "select halfvec_negative_inner_product('[1,0,0]'::halfvec, '[1,0,0]'::halfvec);" \
  "select halfvec_cosine_distance('[1,0,0]'::halfvec, '[0,1,0]'::halfvec);" \
  "select halfvec_lt('[1,0,0]'::halfvec, '[2,0,0]'::halfvec);" \
  "select halfvec_eq('[1,0,0]'::halfvec, '[1,0,0]'::halfvec);")
assert_scalar "halfvec_l2_squared_distance" "$out" "halfvec_l2_squared_distance" "2"
assert_scalar "halfvec_l2_norm" "$out" "halfvec_l2_norm" "5"
assert_scalar "halfvec_negative_inner_product" "$out" "halfvec_negative_inner_product" "-1"
assert_scalar "halfvec_cosine_distance" "$out" "halfvec_cosine_distance" "1"
assert_scalar "halfvec_lt" "$out" "halfvec_lt" "t"
assert_scalar "halfvec_eq" "$out" "halfvec_eq" "t"

out=$(run_session \
  "select sparsevec_l2_squared_distance('{1:1}/3'::sparsevec, '{2:1}/3'::sparsevec);" \
  "select sparsevec_l2_norm('{3:4}/5'::sparsevec);" \
  "select sparsevec_cosine_distance('{1:1}/2'::sparsevec, '{2:1}/2'::sparsevec);")
assert_scalar "sparsevec_l2_squared_distance" "$out" "sparsevec_l2_squared_distance" "2"
assert_scalar "sparsevec_l2_norm" "$out" "sparsevec_l2_norm" "4"
assert_no_error "sparsevec_cosine_distance" "$out"

out=$(run_session \
  "select hamming_distance('B100'::varbit, 'B010'::varbit);" \
  "select jaccard_distance('B100'::varbit, 'B110'::varbit);")
assert_scalar "hamming_distance" "$out" "hamming_distance" "2"
assert_scalar "jaccard_distance" "$out" "jaccard_distance" "0.5"

echo "--- halfvec ip/cosine index opclasses ---"

out=$(run_session \
  "create table pv_feat_hv (id int, emb halfvec);" \
  "insert into pv_feat_hv values (1, '[1,0,0]');" \
  "insert into pv_feat_hv values (2, '[0,1,0]');" \
  "insert into pv_feat_hv values (3, '[0,0,1]');" \
  "create index pv_feat_hv_ip_ivf on pv_feat_hv using ivfflat (emb halfvec_ip_ops) with (lists = 2);" \
  "create index pv_feat_hv_ip_hnsw on pv_feat_hv using hnsw (emb halfvec_ip_ops) with (m = 8, ef_construction = 32);")
assert_no_error "halfvec ip index DDL" "$out"

out=$(run_session "select id from pv_feat_hv order by emb <#> '[1,0,0]' limit 2;")
assert_ids "halfvec_ip_ops ORDER BY <#>" "$out" 1 2

out=$(run_session \
  "create index pv_feat_hv_cos_ivf on pv_feat_hv using ivfflat (emb halfvec_cosine_ops) with (lists = 2);" \
  "create index pv_feat_hv_cos_hnsw on pv_feat_hv using hnsw (emb halfvec_cosine_ops) with (m = 8, ef_construction = 32);")
assert_no_error "halfvec cosine index DDL" "$out"

out=$(run_session "select id from pv_feat_hv order by emb <=> '[1,0,0]' limit 2;")
assert_ids "halfvec_cosine_ops ORDER BY <=>" "$out" 1 2

echo "--- sparsevec ip/cosine hnsw opclasses ---"

out=$(run_session \
  "create table pv_feat_sv (id int, emb sparsevec);" \
  "insert into pv_feat_sv values (1, '{1:1}/3');" \
  "insert into pv_feat_sv values (2, '{2:1}/3');" \
  "insert into pv_feat_sv values (3, '{3:1}/3');" \
  "create index pv_feat_sv_ip on pv_feat_sv using hnsw (emb sparsevec_ip_ops) with (m = 8, ef_construction = 32);")
assert_no_error "sparsevec ip index DDL" "$out"

out=$(run_session "select id from pv_feat_sv order by emb <#> '{1:1}/3' limit 2;")
assert_ids "sparsevec_ip_ops ORDER BY <#>" "$out" 1 2

out=$(run_session \
  "create index pv_feat_sv_cos on pv_feat_sv using hnsw (emb sparsevec_cosine_ops) with (m = 8, ef_construction = 32);")
assert_no_error "sparsevec cosine index DDL" "$out"

out=$(run_session "select id from pv_feat_sv order by emb <=> '{1:1}/3' limit 2;")
assert_ids "sparsevec_cosine_ops ORDER BY <=>" "$out" 1 2

echo "--- bit ivfflat hamming and hnsw jaccard ---"

out=$(run_session \
  "create table pv_feat_bit (id int, emb varbit);" \
  "insert into pv_feat_bit values (1, 'B100');" \
  "insert into pv_feat_bit values (2, 'B110');" \
  "insert into pv_feat_bit values (3, 'B010');" \
  "create index pv_feat_bit_ivf on pv_feat_bit using ivfflat (emb bit_hamming_ops) with (lists = 2);")
assert_no_error "bit hamming ivfflat DDL" "$out"

out=$(run_session "select id from pv_feat_bit order by emb <~> 'B100' limit 2;")
assert_ids "bit_hamming_ops ivfflat ORDER BY <~>" "$out" 1 2

out=$(run_session \
  "create index pv_feat_bit_j on pv_feat_bit using hnsw (emb bit_jaccard_ops) with (m = 8, ef_construction = 32);")
assert_no_error "bit jaccard hnsw DDL" "$out"

out=$(run_session "select id from pv_feat_bit order by emb <%> 'B100' limit 2;")
assert_ids "bit_jaccard_ops ORDER BY <%> (nearest B100 then B110)" "$out" 1 2

echo "--- mutations and vacuum on extended types with indexes ---"

out=$(run_session "update pv_feat_hv set emb = '[0.9,0.1,0]' where id = 2;")
assert_no_error "halfvec update" "$out"

out=$(run_session "select id from pv_feat_hv order by emb <-> '[0.9,0.1,0]' limit 1;")
assert_ids "halfvec update visible in ORDER BY" "$out" 2

out=$(run_session \
  "delete from pv_feat_sv where id = 3;" \
  "select count(*) from pv_feat_sv;")
assert_no_error "sparsevec delete" "$out"
if echo "$out" | grep -Fq 'count = "2"'; then
  echo "OK: sparsevec delete count"
else
  echo "FAIL: sparsevec delete count" >&2
  echo "$out" >&2
  failures=$((failures + 1))
fi

out=$(run_session \
  "vacuum pv_feat_hv;" \
  "vacuum pv_feat_bit;" \
  "vacuum pv_feat_sv;")
assert_no_error "vacuum on pgvector-indexed tables" "$out"

out=$(run_session \
  "insert into pv_feat_bit values (4, 'B001');" \
  "select id from pv_feat_bit order by emb <~> 'B001' limit 1;")
assert_no_error "bit insert after index" "$out"
assert_ids "bit post-index insert ORDER BY <~>" "$out" 4

out=$(run_session "select halfvec_l2_normalize('[3,4,0]'::halfvec);")
assert_no_error "halfvec_l2_normalize" "$out"
if echo "$out" | grep -q 'halfvec_l2_normalize = "\[0\.6'; then
  echo "OK: halfvec_l2_normalize magnitude"
else
  echo "FAIL: halfvec_l2_normalize output" >&2
  echo "$out" >&2
  failures=$((failures + 1))
fi

echo "--- vector / halfvec l2_distance SQL ---"

out=$(run_session \
  "select l2_distance('[1,0,0]'::vector, '[0,1,0]'::vector);" \
  "select halfvec_l2_distance('[1,0,0]'::halfvec, '[0,1,0]'::halfvec);" \
  "select sparsevec_negative_inner_product('{1:1}/2'::sparsevec, '{1:1}/2'::sparsevec);")
assert_no_error "l2_distance family" "$out"
assert_scalar "sparsevec_negative_inner_product" "$out" "sparsevec_negative_inner_product" "-1"
if echo "$out" | grep -Fq 'l2_distance = "1.4142135623731"'; then
  echo "OK: l2_distance"
else
  echo "FAIL: l2_distance (expected sqrt(2))" >&2
  echo "$out" >&2
  failures=$((failures + 1))
fi
if echo "$out" | grep -Fq 'halfvec_l2_distance = "1.4142135623731"'; then
  echo "OK: halfvec_l2_distance"
else
  echo "FAIL: halfvec_l2_distance" >&2
  echo "$out" >&2
  failures=$((failures + 1))
fi

if [[ "$failures" -ne 0 ]]; then
  echo "$failures pgvector feature check(s) failed" >&2
  exit 1
fi

echo "pgvector features smoke test passed"
