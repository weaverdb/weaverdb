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

echo "--- vector / halfvec comparison operators and btree opclasses ---"

out=$(run_session \
  "select vector_lt('[1,0,0]'::vector, '[2,0,0]'::vector);" \
  "select vector_eq('[1,0,0]'::vector, '[1,0,0]'::vector);" \
  "select vector_ne('[1,0,0]'::vector, '[1,0,1]'::vector);" \
  "select vector_le('[1,0,0]'::vector, '[1,0,0]'::vector);" \
  "select vector_ge('[2,0,0]'::vector, '[1,0,0]'::vector);" \
  "select vector_gt('[2,0,0]'::vector, '[1,0,0]'::vector);" \
  "select vector_cmp('[1,0,0]'::vector, '[2,0,0]'::vector);" \
  "select vector_eq('[1,0,0]'::vector, '[2,0,0]'::vector);" \
  "select '[1,0,0]'::vector < '[2,0,0]'::vector;" \
  "select '[2,0,0]'::vector < '[2,0,0]'::vector;" \
  "select '[1,0,0]'::vector = '[1,0,0]'::vector;" \
  "select '[1,0,0]'::vector <> '[1,0,1]'::vector;")
assert_no_error "vector comparison funcs/ops" "$out"
assert_scalar "vector_lt" "$out" "vector_lt" "t"
assert_scalar "vector_eq true" "$out" "vector_eq" "t"
assert_scalar "vector_ne" "$out" "vector_ne" "t"
assert_scalar "vector_cmp" "$out" "vector_cmp" "-1"
assert_scalar "vector_eq false" "$out" "vector_eq" "f"
assert_scalar "vector < operator" "$out" "?column?" "t"
assert_scalar "vector < equal is false" "$out" "?column?" "f"

out=$(run_session \
  "select '[1,0,0]'::halfvec < '[2,0,0]'::halfvec;" \
  "select '[1,0,0]'::halfvec = '[1,0,0]'::halfvec;" \
  "select '[1,0,0]'::halfvec <> '[1,0,1]'::halfvec;" \
  "select '[1,0,0]'::halfvec <= '[1,0,0]'::halfvec;" \
  "select '[2,0,0]'::halfvec >= '[1,0,0]'::halfvec;" \
  "select '[2,0,0]'::halfvec > '[1,0,0]'::halfvec;" \
  "select '[1,0,0]'::halfvec < '[1,0,0]'::halfvec;" \
  "select '[1,0,0]'::halfvec = '[2,0,0]'::halfvec;")
assert_no_error "halfvec comparison operators" "$out"
assert_scalar "halfvec < operator" "$out" "?column?" "t"
assert_scalar "halfvec < equal is false" "$out" "?column?" "f"
assert_scalar "halfvec = false" "$out" "?column?" "f"

out=$(run_session \
  "create table pv_feat_bt (id int, emb vector);" \
  "insert into pv_feat_bt values (1, '[1,0,0]');" \
  "insert into pv_feat_bt values (2, '[2,0,0]');" \
  "insert into pv_feat_bt values (3, '[0,1,0]');" \
  "create index pv_feat_bt_idx on pv_feat_bt using btree (emb vector_ops);" \
  "select id from pv_feat_bt where emb = '[1,0,0]';")
assert_no_error "vector btree vector_ops" "$out"
assert_ids "vector btree equality" "$out" 1

out=$(run_session \
  "select id from pv_feat_bt where emb < '[2,0,0]' order by emb;")
assert_ids "vector btree range order" "$out" 3 1

out=$(run_session \
  "create table pv_feat_hv_bt (id int, emb halfvec);" \
  "insert into pv_feat_hv_bt values (1, '[1,0,0]');" \
  "insert into pv_feat_hv_bt values (2, '[2,0,0]');" \
  "insert into pv_feat_hv_bt values (3, '[0,1,0]');" \
  "create index pv_feat_hv_bt_idx on pv_feat_hv_bt using btree (emb halfvec_ops);" \
  "select id from pv_feat_hv_bt where emb = '[1,0,0]';")
assert_no_error "halfvec btree halfvec_ops" "$out"
assert_ids "halfvec btree equality" "$out" 1

out=$(run_session \
  "select id from pv_feat_hv_bt where emb < '[2,0,0]' order by emb;")
assert_ids "halfvec btree range order" "$out" 3 1

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
got=$(ids_from_output "$out")
first=$(echo "$got" | sed -n '1p')
second=$(echo "$got" | sed -n '2p')
if [[ "$first" == "1" && ( "$second" == "2" || "$second" == "3" ) ]]; then
  echo "OK: halfvec_ip_ops ORDER BY <#> (1 then tie 2|3)"
else
  echo "FAIL: halfvec_ip_ops ORDER BY <#> (got: $(echo "$got" | tr '\n' ' '))" >&2
  failures=$((failures + 1))
fi

out=$(run_session \
  "create index pv_feat_hv_cos_ivf on pv_feat_hv using ivfflat (emb halfvec_cosine_ops) with (lists = 2);" \
  "create index pv_feat_hv_cos_hnsw on pv_feat_hv using hnsw (emb halfvec_cosine_ops) with (m = 8, ef_construction = 32);")
assert_no_error "halfvec cosine index DDL" "$out"

out=$(run_session "select id from pv_feat_hv order by emb <=> '[1,0,0]' limit 2;")
got=$(ids_from_output "$out")
first=$(echo "$got" | sed -n '1p')
second=$(echo "$got" | sed -n '2p')
if [[ "$first" == "1" && ( "$second" == "2" || "$second" == "3" ) ]]; then
  echo "OK: halfvec_cosine_ops ORDER BY <=> (1 then tie 2|3)"
else
  echo "FAIL: halfvec_cosine_ops ORDER BY <=> (got: $(echo "$got" | tr '\n' ' '))" >&2
  failures=$((failures + 1))
fi

echo "--- sparsevec ip/cosine hnsw opclasses ---"

out=$(run_session \
  "create table pv_feat_sv (id int, emb sparsevec);" \
  "insert into pv_feat_sv values (1, '{1:1}/3');" \
  "insert into pv_feat_sv values (2, '{2:1}/3');" \
  "insert into pv_feat_sv values (3, '{3:1}/3');" \
  "create index pv_feat_sv_ip on pv_feat_sv using hnsw (emb sparsevec_ip_ops) with (m = 8, ef_construction = 32);")
assert_no_error "sparsevec ip index DDL" "$out"

out=$(run_session "select id from pv_feat_sv order by emb <#> '{1:1}/3' limit 2;")
got=$(ids_from_output "$out")
first=$(echo "$got" | sed -n '1p')
second=$(echo "$got" | sed -n '2p')
if [[ "$first" == "1" && ( "$second" == "2" || "$second" == "3" ) ]]; then
  echo "OK: sparsevec_ip_ops ORDER BY <#> (1 then tie 2|3)"
else
  echo "FAIL: sparsevec_ip_ops ORDER BY <#> (got: $(echo "$got" | tr '\n' ' '))" >&2
  failures=$((failures + 1))
fi

out=$(run_session \
  "create index pv_feat_sv_cos on pv_feat_sv using hnsw (emb sparsevec_cosine_ops) with (m = 8, ef_construction = 32);")
assert_no_error "sparsevec cosine index DDL" "$out"

out=$(run_session "select id from pv_feat_sv order by emb <=> '{1:1}/3' limit 2;")
got=$(ids_from_output "$out")
first=$(echo "$got" | sed -n '1p')
second=$(echo "$got" | sed -n '2p')
if [[ "$first" == "1" && ( "$second" == "2" || "$second" == "3" ) ]]; then
  echo "OK: sparsevec_cosine_ops ORDER BY <=> (1 then tie 2|3)"
else
  echo "FAIL: sparsevec_cosine_ops ORDER BY <=> (got: $(echo "$got" | tr '\n' ' '))" >&2
  failures=$((failures + 1))
fi

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

# --- bytea blob ↔ vector conversion + functional index ---
out=$(run_session \
  "select vector_to_bytea('[1,0,0]'::vector) is not null;" \
  "select bytea_to_vector(vector_to_bytea('[1,0,0]'::vector)) <-> '[1,0,0]'::vector;")
assert_no_error "bytea_to_vector round-trip" "$out"
assert_scalar "bytea_to_vector distance" "$out" "?column?" "0"

out=$(run_session \
  "create table pv_feat_blob (id int, emb bytea);" \
  "insert into pv_feat_blob values (1, vector_to_bytea('[1,0,0]'));" \
  "insert into pv_feat_blob values (2, vector_to_bytea('[0,1,0]'));" \
  "insert into pv_feat_blob values (3, vector_to_bytea('[0,0,1]'));" \
  "create index pv_feat_blob_hnsw on pv_feat_blob using hnsw (bytea_to_vector(emb) vector_l2_ops) with (m = 8, ef_construction = 32);" \
  "create index pv_feat_blob_ivf on pv_feat_blob using ivfflat (bytea_to_vector(emb) vector_l2_ops) with (lists = 2);" \
  "select id from pv_feat_blob order by bytea_to_vector(emb) <-> '[1,0,0]' limit 2;")
assert_no_error "bytea functional indexes" "$out"
got=$(ids_from_output "$out")
first=$(echo "$got" | sed -n '1p')
second=$(echo "$got" | sed -n '2p')
if [[ "$first" == "1" && ( "$second" == "2" || "$second" == "3" ) ]]; then
  echo "OK: bytea functional ORDER BY nearest (1 then tie 2|3)"
else
  echo "FAIL: bytea functional ORDER BY nearest (got: $(echo "$got" | tr '\n' ' '))" >&2
  failures=$((failures + 1))
fi

out=$(run_session \
  "insert into pv_feat_blob values (4, vector_to_bytea('[1,1,0]'));" \
  "select id from pv_feat_blob order by bytea_to_vector(emb) <-> '[1,1,0]' limit 1;")
assert_no_error "bytea functional post-index insert" "$out"
assert_ids "bytea functional post-index nearest" "$out" 4

# --- SQL type blob (OID 1803) ↔ vector conversion + functional index ---
out=$(run_session \
  "select vector_to_blob('[1,0,0]'::vector) is not null;" \
  "select blob_to_vector(vector_to_blob('[1,0,0]'::vector)) <-> '[1,0,0]'::vector;")
assert_no_error "blob_to_vector round-trip" "$out"
assert_scalar "blob_to_vector distance" "$out" "?column?" "0"

out=$(run_session \
  "create table pv_feat_typed_blob (id int, emb blob);" \
  "insert into pv_feat_typed_blob values (1, vector_to_blob('[1,0,0]'));" \
  "insert into pv_feat_typed_blob values (2, vector_to_blob('[0,1,0]'));" \
  "insert into pv_feat_typed_blob values (3, vector_to_blob('[0,0,1]'));" \
  "create index pv_feat_typed_blob_hnsw on pv_feat_typed_blob using hnsw (blob_to_vector(emb) vector_l2_ops) with (m = 8, ef_construction = 32);" \
  "create index pv_feat_typed_blob_ivf on pv_feat_typed_blob using ivfflat (blob_to_vector(emb) vector_l2_ops) with (lists = 2);" \
  "select id from pv_feat_typed_blob order by blob_to_vector(emb) <-> '[1,0,0]' limit 2;")
assert_no_error "blob functional indexes" "$out"
got=$(ids_from_output "$out")
first=$(echo "$got" | sed -n '1p')
second=$(echo "$got" | sed -n '2p')
if [[ "$first" == "1" && ( "$second" == "2" || "$second" == "3" ) ]]; then
  echo "OK: blob functional ORDER BY nearest (1 then tie 2|3)"
else
  echo "FAIL: blob functional ORDER BY nearest (got: $(echo "$got" | tr '\n' ' '))" >&2
  failures=$((failures + 1))
fi

out=$(run_session \
  "insert into pv_feat_typed_blob values (4, vector_to_blob('[1,1,0]'));" \
  "select id from pv_feat_typed_blob order by blob_to_vector(emb) <-> '[1,1,0]' limit 1;")
assert_no_error "blob functional post-index insert" "$out"
assert_ids "blob functional post-index nearest" "$out" 4

out=$(run_session \
  "select blob_to_halfvec(halfvec_to_blob('[1,0,0]'::halfvec)) <-> '[1,0,0]'::halfvec as hv_dist;" \
  "select blob_to_sparsevec(sparsevec_to_blob('{1:1,3:2}/5'::sparsevec)) <-> '{1:1,3:2}/5'::sparsevec as sv_dist;" \
  "select blob_to_bit(bit_to_blob('B101'::varbit)) = 'B101'::varbit as bit_eq;")
assert_no_error "halfvec/sparsevec/bit blob converters" "$out"
assert_scalar "blob_to_halfvec distance" "$out" "hv_dist" "0"
assert_scalar "blob_to_sparsevec distance" "$out" "sv_dist" "0"
assert_scalar "blob_to_bit round-trip" "$out" "bit_eq" "t"

# --- halfvec / sparsevec / bit bytea converters ---
out=$(run_session \
  "select halfvec_to_bytea('[1,0,0]'::halfvec) is not null as hv_ok;" \
  "select bytea_to_halfvec(halfvec_to_bytea('[1,0,0]'::halfvec)) <-> '[1,0,0]'::halfvec as hv_dist;" \
  "select sparsevec_to_bytea('{1:1,3:2}/5'::sparsevec) is not null as sv_ok;" \
  "select bytea_to_sparsevec(sparsevec_to_bytea('{1:1,3:2}/5'::sparsevec)) <-> '{1:1,3:2}/5'::sparsevec as sv_dist;" \
  "select bit_to_bytea('B101'::varbit) is not null as bit_ok;" \
  "select bytea_to_bit(bit_to_bytea('B101'::varbit)) = 'B101'::varbit as bit_eq;")
assert_no_error "halfvec/sparsevec/bit bytea converters" "$out"
assert_scalar "bytea_to_halfvec distance" "$out" "hv_dist" "0"
assert_scalar "bytea_to_sparsevec distance" "$out" "sv_dist" "0"
assert_scalar "bytea_to_bit round-trip" "$out" "bit_eq" "t"

out=$(run_session \
  "create table pv_feat_half_blob (id int, emb bytea);" \
  "insert into pv_feat_half_blob values (1, halfvec_to_bytea('[1,0,0]'::halfvec));" \
  "insert into pv_feat_half_blob values (2, halfvec_to_bytea('[0,1,0]'::halfvec));" \
  "insert into pv_feat_half_blob values (3, halfvec_to_bytea('[0,0,1]'::halfvec));" \
  "create index pv_feat_half_blob_hnsw on pv_feat_half_blob using hnsw (bytea_to_halfvec(emb) halfvec_l2_ops) with (m = 8, ef_construction = 32);" \
  "select id from pv_feat_half_blob order by bytea_to_halfvec(emb) <-> '[1,0,0]'::halfvec limit 2;")
assert_no_error "halfvec bytea functional index" "$out"
got=$(ids_from_output "$out")
first=$(echo "$got" | sed -n '1p')
second=$(echo "$got" | sed -n '2p')
if [[ "$first" == "1" && ( "$second" == "2" || "$second" == "3" ) ]]; then
  echo "OK: halfvec bytea functional ORDER BY nearest (1 then tie 2|3)"
else
  echo "FAIL: halfvec bytea functional ORDER BY nearest (got: $(echo "$got" | tr '\n' ' '))" >&2
  failures=$((failures + 1))
fi

# --- bytea columns keep plain storage (vector/halfvec/sparsevec stay 'e') ---
out=$(run_session \
  "create table pv_feat_ba_storage (id int, emb bytea);" \
  "insert into pv_feat_ba_storage values (1, vector_to_bytea('[1,0,0]'));" \
  "select attstorage from pg_attribute a, pg_class c where c.relname = 'pv_feat_ba_storage' and a.attrelid = c.oid and a.attname = 'emb';")
assert_no_error "bytea attstorage probe" "$out"
if echo "$out" | grep -Fq 'attstorage = "p"'; then
  echo "OK: bytea attstorage is plain"
else
  echo "FAIL: expected bytea attstorage='p'" >&2
  echo "$out" >&2
  failures=$((failures + 1))
fi

echo "--- arithmetic / avg / leftover helpers ---"

out=$(run_session \
  "select '[1,2,3]'::vector + '[4,5,6]'::vector;" \
  "select '[4,5,6]'::vector - '[1,2,3]'::vector;" \
  "select '[2,3,4]'::vector * '[1,2,3]'::vector;" \
  "select vector_add('[1,2,3]'::vector, '[4,5,6]'::vector);")
assert_no_error "vector arithmetic ops" "$out"
assert_scalar "vector +" "$out" "?column?" "[5,7,9]"
assert_scalar "vector -" "$out" "?column?" "[3,3,3]"
assert_scalar "vector *" "$out" "?column?" "[2,6,12]"
assert_scalar "vector_add" "$out" "vector_add" "[5,7,9]"

out=$(run_session \
  "select '[1,2,3]'::halfvec + '[4,5,6]'::halfvec;" \
  "select '[4,5,6]'::halfvec - '[1,2,3]'::halfvec;" \
  "select '[2,3,4]'::halfvec * '[1,2,3]'::halfvec;")
assert_no_error "halfvec arithmetic ops" "$out"
assert_scalar "halfvec +" "$out" "?column?" "[5,7,9]"
assert_scalar "halfvec -" "$out" "?column?" "[3,3,3]"
assert_scalar "halfvec *" "$out" "?column?" "[2,6,12]"

out=$(run_session \
  "create table pv_feat_avg (id int, emb vector);" \
  "insert into pv_feat_avg values (1, '[1,2,3]');" \
  "insert into pv_feat_avg values (2, '[3,4,5]');" \
  "select avg(emb) from pv_feat_avg;" \
  "select sum(emb) from pv_feat_avg;")
assert_no_error "vector avg/sum aggregates" "$out"
assert_scalar "avg(vector)" "$out" "avg" "[2,3,4]"
assert_scalar "sum(vector)" "$out" "sum" "[4,6,8]"

out=$(run_session \
  "create table pv_feat_hv_avg (id int, emb halfvec);" \
  "insert into pv_feat_hv_avg values (1, '[1,2,3]');" \
  "insert into pv_feat_hv_avg values (2, '[3,4,5]');" \
  "select avg(emb) from pv_feat_hv_avg;" \
  "select sum(emb) from pv_feat_hv_avg;")
assert_no_error "halfvec avg/sum aggregates" "$out"
assert_scalar "avg(halfvec)" "$out" "avg" "[2,3,4]"
assert_scalar "sum(halfvec)" "$out" "sum" "[4,6,8]"

out=$(run_session \
  "select vector_dims('[1,2,3,4]'::vector);" \
  "select halfvec_vector_dims('[1,2,3]'::halfvec);" \
  "select inner_product('[1,2,3]'::vector, '[4,5,6]'::vector);" \
  "select halfvec_inner_product('[1,2,3]'::halfvec, '[4,5,6]'::halfvec);" \
  "select sparsevec_inner_product('{1:1,2:2}/3'::sparsevec, '{1:4,2:5}/3'::sparsevec);" \
  "select l1_distance('[1,0,0]'::vector, '[0,1,0]'::vector);" \
  "select '[1,0,0]'::vector <+> '[0,1,0]'::vector;" \
  "select halfvec_l1_distance('[1,0,0]'::halfvec, '[0,1,0]'::halfvec);" \
  "select sparsevec_l1_distance('{1:1}/2'::sparsevec, '{2:1}/2'::sparsevec);" \
  "select subvector('[1,2,3,4]'::vector, 2, 2);" \
  "select halfvec_subvector('[1,2,3,4]'::halfvec, 2, 2);" \
  "select '[1,2]'::vector || '[3,4]'::vector;" \
  "select '[1,2]'::halfvec || '[3,4]'::halfvec;")
assert_no_error "utility helpers / L1 / concat" "$out"
assert_scalar "vector_dims" "$out" "vector_dims" "4"
assert_scalar "halfvec_vector_dims" "$out" "halfvec_vector_dims" "3"
assert_scalar "inner_product" "$out" "inner_product" "32"
assert_scalar "halfvec_inner_product" "$out" "halfvec_inner_product" "32"
assert_scalar "sparsevec_inner_product" "$out" "sparsevec_inner_product" "14"
assert_scalar "l1_distance" "$out" "l1_distance" "2"
assert_scalar "vector <+> operator" "$out" "?column?" "2"
assert_scalar "halfvec_l1_distance" "$out" "halfvec_l1_distance" "2"
assert_scalar "sparsevec_l1_distance" "$out" "sparsevec_l1_distance" "2"
assert_scalar "subvector" "$out" "subvector" "[2,3]"
assert_scalar "halfvec_subvector" "$out" "halfvec_subvector" "[2,3]"
assert_scalar "vector ||" "$out" "?column?" "[1,2,3,4]"
assert_scalar "halfvec ||" "$out" "?column?" "[1,2,3,4]"

echo "--- binary_quantize + sparsevec btree ---"

out=$(run_session \
  "select binary_quantize('[1,-1,0.5,0]'::vector);" \
  "select halfvec_binary_quantize('[1,-1,0.5,0]'::halfvec);")
assert_no_error "binary_quantize family" "$out"

out=$(run_session \
  "create table pv_feat_sv_bt (id int, emb sparsevec);" \
  "insert into pv_feat_sv_bt values (1, '{1:1}/3');" \
  "insert into pv_feat_sv_bt values (2, '{1:2}/3');" \
  "insert into pv_feat_sv_bt values (3, '{2:1}/3');" \
  "create index pv_feat_sv_bt_idx on pv_feat_sv_bt using btree (emb sparsevec_ops);" \
  "select id from pv_feat_sv_bt where emb = '{1:1}/3'::sparsevec;")
assert_no_error "sparsevec_ops btree" "$out"
assert_ids "sparsevec btree equality" "$out" 1

out=$(run_session \
  "select id from pv_feat_sv_bt where emb < '{1:2}/3'::sparsevec order by emb;")
assert_ids "sparsevec btree range order" "$out" 3 1

echo "--- runtime search knobs (SET/SHOW/RESET) ---"

out=$(run_session \
  "show hnsw.ef_search;" \
  "set hnsw.ef_search = 80;" \
  "show hnsw.ef_search;" \
  "reset hnsw.ef_search;" \
  "show hnsw.ef_search;")
assert_no_error "hnsw.ef_search SET/SHOW/RESET" "$out"
if echo "$out" | grep -Fq 'hnsw.ef_search is 40' && \
   echo "$out" | grep -Fq 'hnsw.ef_search is 80'; then
  echo "OK: hnsw.ef_search defaults and SET"
else
  echo "FAIL: hnsw.ef_search SHOW values" >&2
  echo "$out" >&2
  failures=$((failures + 1))
fi

out=$(run_session \
  "show ivfflat.probes;" \
  "set ivfflat.probes to 5;" \
  "show ivfflat.probes;" \
  "set ivfflat_probes = 3;" \
  "show ivfflat_probes;" \
  "reset ivfflat.probes;" \
  "show ivfflat.probes;")
assert_no_error "ivfflat.probes SET/SHOW/RESET" "$out"
if echo "$out" | grep -Fq 'ivfflat.probes is 1' && \
   echo "$out" | grep -Fq 'ivfflat.probes is 5' && \
   echo "$out" | grep -Fq 'ivfflat.probes is 3'; then
  echo "OK: ivfflat.probes dotted and underscore aliases"
else
  echo "FAIL: ivfflat.probes SHOW values" >&2
  echo "$out" >&2
  failures=$((failures + 1))
fi

out=$(run_session \
  "set hnsw.iterative_scan = relaxed_order;" \
  "show hnsw.iterative_scan;" \
  "set ivfflat.iterative_scan = off;" \
  "show ivfflat.iterative_scan;" \
  "set hnsw.max_scan_tuples = 1000;" \
  "show hnsw.max_scan_tuples;" \
  "set hnsw.scan_mem_multiplier = 2;" \
  "show hnsw.scan_mem_multiplier;" \
  "set ivfflat.max_probes = 50;" \
  "show ivfflat.max_probes;" \
  "set hnsw.build_workers = 2;" \
  "show hnsw.build_workers;" \
  "set ivfflat.assign_workers = 2;" \
  "show ivfflat.assign_workers;" \
  "reset hnsw.build_workers;" \
  "show hnsw.build_workers;")
assert_no_error "remaining pgvector search knobs" "$out"
if echo "$out" | grep -Fq 'hnsw.iterative_scan is relaxed_order' && \
   echo "$out" | grep -Fq 'hnsw.max_scan_tuples is 1000' && \
   echo "$out" | grep -Fq 'ivfflat.max_probes is 50' && \
   echo "$out" | grep -Fq 'hnsw.build_workers is 2' && \
   echo "$out" | grep -Fq 'ivfflat.assign_workers is 2' && \
   echo "$out" | grep -Fq 'hnsw.build_workers is 1'; then
  echo "OK: iterative_scan / max_scan_tuples / max_probes / build workers"
else
  echo "FAIL: remaining search knob SHOW values" >&2
  echo "$out" >&2
  failures=$((failures + 1))
fi

# Parallel build remains opt-in (defaults to 1). Serial path must stay green.
out=$(run_session \
  "create table pv_par (id int, emb vector);" \
  "insert into pv_par values (1, '[1,0,0]');" \
  "insert into pv_par values (2, '[2,0,0]');" \
  "insert into pv_par values (3, '[3,0,0]');" \
  "set hnsw.build_workers = 1;" \
  "set ivfflat.assign_workers = 1;" \
  "create index pv_par_hnsw on pv_par using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32);" \
  "create index pv_par_ivf on pv_par using ivfflat (emb vector_l2_ops) with (lists = 2);" \
  "select id from pv_par order by emb <-> '[1,0,0]' limit 1;")
assert_no_error "serial hnsw/ivfflat index build" "$out"
assert_ids "serial build ANN nearest" "$out" 1

echo "--- SIMD-width distance correctness (dims that hit NEON/AVX lanes) ---"

# 16-dim vectors: NEON processes 4 floats/iter, AVX2 8 — exercises vectorized path + tail.
# Expected IP = sum_{i=0..15} (i+1)*(i+1) = sum k^2 for k=1..16 = 16*17*33/6 = 1496
# Expected L2^2 of a vs zeros = same 1496; L2^2(a,a) = 0; L1(a, zeros) = sum 1..16 = 136
out=$(run_session \
  "select inner_product('[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]'::vector, '[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]'::vector);" \
  "select vector_l2_squared_distance('[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]'::vector, '[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]'::vector);" \
  "select vector_l2_squared_distance('[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]'::vector, '[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]'::vector);" \
  "select l1_distance('[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]'::vector, '[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]'::vector);" \
  "select halfvec_inner_product('[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]'::halfvec, '[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]'::halfvec);" \
  "select halfvec_l2_squared_distance('[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]'::halfvec, '[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]'::halfvec);")
assert_no_error "simd-width distances" "$out"
assert_scalar "simd inner_product 16d" "$out" "inner_product" "1496"
assert_scalar "simd vector_l2_squared vs zero" "$out" "vector_l2_squared_distance" "1496"
assert_scalar "simd vector_l2_squared identical" "$out" "vector_l2_squared_distance" "0"
assert_scalar "simd l1_distance 16d" "$out" "l1_distance" "136"
assert_scalar "simd halfvec_inner_product 16d" "$out" "halfvec_inner_product" "1496"
assert_scalar "simd halfvec_l2_squared 16d" "$out" "halfvec_l2_squared_distance" "1496"

# 17-dim: full SIMD iterations + 1-element scalar tail
out=$(run_session \
  "select inner_product('[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1]'::vector, '[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1]'::vector);" \
  "select vector_l2_squared_distance('[1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1]'::vector, '[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]'::vector);")
assert_no_error "simd-width + tail" "$out"
assert_scalar "simd inner_product 17d ones" "$out" "inner_product" "17"
assert_scalar "simd l2_squared sparse 17d" "$out" "vector_l2_squared_distance" "2"

echo "--- IVFFlat vacuum delete correctness (TID reuse) ---"
# PageIndexMultiDelete must actually remove index items. If it no-ops, a stale
# IVFFlat entry keeps the deleted vector key while the heap TID can be reused
# by a later INSERT — Index Scan then ranks the wrong live row as a neighbor.
out=$(run_session \
  "create table pv_feat_vac_ivf (id int, emb vector);" \
  "insert into pv_feat_vac_ivf values (1, '[1,0,0]');" \
  "insert into pv_feat_vac_ivf values (2, '[0,1,0]');" \
  "insert into pv_feat_vac_ivf values (3, '[0,0,1]');" \
  "insert into pv_feat_vac_ivf values (4, '[0.95,0.05,0]');" \
  "create index pv_feat_vac_ivf_idx on pv_feat_vac_ivf using ivfflat (emb vector_l2_ops) with (lists = 2);" \
  "delete from pv_feat_vac_ivf where id = 4;" \
  "vacuum pv_feat_vac_ivf;" \
  "insert into pv_feat_vac_ivf values (40, '[0,1,0]');" \
  "set enable_seqscan = off;" \
  "set ivfflat.probes = 2;" \
  "select id from pv_feat_vac_ivf order by emb <-> '[1,0,0]' limit 1;")
assert_no_error "ivfflat vacuum TID-reuse setup" "$out"
assert_ids "ivfflat vacuum: nearest to [1,0,0] stays id=1 (not reused TID)" "$out" 1

out=$(run_session \
  "set enable_seqscan = off;" \
  "select id from pv_feat_vac_ivf order by emb <-> '[1,0,0]' limit 5;")
assert_no_error "ivfflat vacuum post-delete scan" "$out"
if echo "$out" | grep -qE 'id = "4"'; then
  echo "FAIL: deleted id=4 still visible via IVFFlat after VACUUM" >&2
  echo "$out" >&2
  failures=$((failures + 1))
else
  echo "OK: deleted id=4 absent from IVFFlat ANN results"
fi

echo "--- HNSW vacuum after delete (no deleted ids; live nearest) ---"
out=$(run_session \
  "create table pv_feat_vac_hnsw (id int, emb vector);" \
  "insert into pv_feat_vac_hnsw values (1, '[1,0,0]');" \
  "insert into pv_feat_vac_hnsw values (2, '[0,1,0]');" \
  "insert into pv_feat_vac_hnsw values (3, '[0,0,1]');" \
  "insert into pv_feat_vac_hnsw values (4, '[0.95,0.05,0]');" \
  "create index pv_feat_vac_hnsw_idx on pv_feat_vac_hnsw using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32);" \
  "delete from pv_feat_vac_hnsw where id = 4;" \
  "vacuum pv_feat_vac_hnsw;" \
  "insert into pv_feat_vac_hnsw values (40, '[0,1,0]');" \
  "set enable_seqscan = off;" \
  "set hnsw.ef_search = 40;" \
  "select id from pv_feat_vac_hnsw order by emb <-> '[1,0,0]' limit 1;")
assert_no_error "hnsw vacuum setup" "$out"
assert_ids "hnsw vacuum: nearest to [1,0,0] stays id=1" "$out" 1

if [[ "$failures" -ne 0 ]]; then
  echo "$failures pgvector feature check(s) failed" >&2
  exit 1
fi

echo "pgvector features smoke test passed"
