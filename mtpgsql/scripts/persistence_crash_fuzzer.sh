#!/usr/bin/env bash
# Seeded crash-restart fuzzer for Weaver persistence.
#
# Each round:
#   1. Fresh datadir, table with unique btree + HNSW + IVFFlat
#   2. Random insert / delete / update while tracking the live id set
#   3. VACUUM with a crash injection point
#   4. New process, VACUUM, check heap/index invariants
#
# Crash points (existing WEAVER_VACUUM_CRASH_POINT hooks):
#   after_index_barrier              — durable index cleanup, then FATAL (safe)
#   skip_barrier_heap_then_crash     — heap LP recycle without flush (orphan TIDs)
#
# Invariants after restart + vacuum:
#   - SELECT id matches the fuzzer's live set
#   - A deleted unique id is re-insertable (no stale index TID)
#   - ANN ORDER BY does not return deleted ids
#
# Not yet covered here: mid-CriticalIO insert / cart FLUSHING join via
# in-process hooks. External SIGKILL coverage for those windows lives in
# mtpgsql/scripts/index_heap_crash_consistency.sh (no production crashers).
#
# Usage:
#   ./mtpgsql/scripts/persistence_crash_fuzzer.sh [seed] [rounds] [mtpg_prefix]
#
# Defaults: seed from time, 4 rounds, build_test/mtpg then build/mtpg.
# Replay a failure with the printed seed:
#   ./mtpgsql/scripts/persistence_crash_fuzzer.sh 1740000000 4

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SEED="${1:-}"
ROUNDS="${2:-4}"
if [[ -z "$SEED" ]]; then
  SEED="$(date +%s)"
fi
RANDOM="$SEED"

if [[ -n "${3:-}" ]]; then
  MTPG="$3"
elif [[ -x "${PGVECTOR_BUILD_DIR:-$ROOT/build_test}/mtpg/bin/postgres" ]]; then
  MTPG="${PGVECTOR_BUILD_DIR:-$ROOT/build_test}/mtpg"
else
  MTPG="$ROOT/build/mtpg"
fi

PSQL="$MTPG/bin/postgres"
INITDB="$MTPG/bin/initdb"
MAX_ID=16
START_N=10
OPS="${WEAVER_FUZZ_OPS:-12}"

if [[ ! -x "$PSQL" || ! -x "$INITDB" ]]; then
  echo "missing mtpg binaries under $MTPG (build first)" >&2
  exit 1
fi

echo "persistence_crash_fuzzer: seed=$SEED rounds=$ROUNDS ops=$OPS mtpg=$MTPG"

CRASH_POINTS="after_index_barrier skip_barrier_heap_then_crash"

# --- id-set helpers (bash 3.2: no associative arrays) ---

set_has() {
  local needle="$1"
  local x
  for x in $2; do
    [[ "$x" == "$needle" ]] && return 0
  done
  return 1
}

set_add() {
  local s="$1" id="$2"
  if set_has "$id" "$s"; then
    echo "$s"
  else
    echo "$s $id"
  fi
}

set_remove() {
  local s="$1" id="$2" out="" x
  for x in $s; do
    [[ "$x" != "$id" ]] && out="$out $x"
  done
  echo "$out"
}

set_count() {
  set -- $1
  echo $#
}

set_pick() {
  local s="$1"
  set -- $s
  [[ $# -eq 0 ]] && return 1
  shift $((RANDOM % $#))
  echo "$1"
}

set_sorted() {
  echo "$1" | tr ' ' '\n' | grep -E '^[0-9]+$' | sort -n | tr '\n' ' ' | sed 's/ *$//'
}

vec_for() {
  # Distinct 3-d vectors so ANN has a stable nearest for id=1.
  echo "[$1,0,0]"
}

parse_ids() {
  echo "$1" | grep -oE 'id = "[0-9]+"' | sed 's/id = "//;s/"//' | tr '\n' ' ' | sed 's/ *$//'
}

pick_crash_point() {
  set -- $CRASH_POINTS
  shift $((RANDOM % $#))
  echo "$1"
}

# --- postgres ---

DATADIR=""
cleanup() {
  unset WEAVER_VACUUM_CRASH_POINT || true
  if [[ -n "$DATADIR" ]]; then
    rm -rf "$DATADIR"
  fi
}
trap cleanup EXIT

run_sql() {
  local db="$1"
  shift
  "$PSQL" -D "$DATADIR" "$db" 2>&1 <<SQL
$@
SQL
}

assert_no_error() {
  local desc="$1" out="$2"
  if echo "$out" | grep -qiE 'ERROR:|syntax error'; then
    echo "FAIL: $desc" >&2
    echo "$out" >&2
    return 1
  fi
}

# --- one isolated crash-restart round ---

run_round() {
  local round="$1"
  local live="" deleted="" id i op crash out got want missing
  local n_live n_deleted rc

  DATADIR="$(mktemp -d "${TMPDIR:-/tmp}/weaver-persist-fuzz.XXXXXX")"
  "$INITDB" -D "$DATADIR" >/dev/null
  out="$(run_sql template1 "create database persistfuzz;")"
  assert_no_error "round $round create database" "$out"

  echo "-- round $round: setup --"
  out="$(run_sql persistfuzz "
create table persist_fz (id int4, val varchar(64), emb vector);
create unique index persist_fz_id_idx on persist_fz (id);
")"
  assert_no_error "round $round create table/index" "$out"

  for i in $(seq 1 "$START_N"); do
    out="$(run_sql persistfuzz "insert into persist_fz values ($i, 'v$i', '$(vec_for "$i")');")"
    assert_no_error "round $round insert $i" "$out"
    live="$(set_add "$live" "$i")"
  done

  out="$(run_sql persistfuzz "
create index persist_fz_hnsw on persist_fz using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32);
create index persist_fz_ivf on persist_fz using ivfflat (emb vector_l2_ops) with (lists = 2);
")"
  assert_no_error "round $round create hnsw/ivfflat" "$out"

  echo "-- round $round: $OPS random ops --"
  i=0
  while [[ "$i" -lt "$OPS" ]]; do
    n_live="$(set_count "$live")"
    n_deleted="$(set_count "$deleted")"
    # 0 insert, 1 delete, 2 update, 3 vacuum (no crash)
    op=$((RANDOM % 4))
    if [[ "$n_live" -le 3 ]]; then
      op=0
    elif [[ "$n_deleted" -eq 0 ]]; then
      op=1
    fi
    case "$op" in
      0)
        missing=""
        for id in $(seq 1 "$MAX_ID"); do
          if ! set_has "$id" "$live"; then
            missing="$(set_add "$missing" "$id")"
          fi
        done
        if [[ "$(set_count "$missing")" -eq 0 ]]; then
          i=$((i + 1))
          continue
        fi
        id="$(set_pick "$missing")"
        echo "  op insert id=$id"
        out="$(run_sql persistfuzz "insert into persist_fz values ($id, 'v$id', '$(vec_for "$id")');")"
        assert_no_error "round $round insert id=$id" "$out"
        live="$(set_add "$live" "$id")"
        deleted="$(set_remove "$deleted" "$id")"
        ;;
      1)
        id="$(set_pick "$live")"
        echo "  op delete id=$id"
        out="$(run_sql persistfuzz "delete from persist_fz where id = $id;")"
        assert_no_error "round $round delete id=$id" "$out"
        live="$(set_remove "$live" "$id")"
        deleted="$(set_add "$deleted" "$id")"
        ;;
      2)
        id="$(set_pick "$live")"
        echo "  op update id=$id"
        out="$(run_sql persistfuzz "update persist_fz set emb = '[0,$id,0]' where id = $id;")"
        assert_no_error "round $round update id=$id" "$out"
        ;;
      3)
        echo "  op vacuum"
        out="$(run_sql persistfuzz "vacuum persist_fz;")"
        assert_no_error "round $round vacuum" "$out"
        ;;
    esac
    i=$((i + 1))
  done

  if [[ "$(set_count "$deleted")" -eq 0 ]]; then
    id="$(set_pick "$live")"
    echo "  op delete id=$id (ensure vacuum has work)"
    out="$(run_sql persistfuzz "delete from persist_fz where id = $id;")"
    assert_no_error "round $round forced delete" "$out"
    live="$(set_remove "$live" "$id")"
    deleted="$(set_add "$deleted" "$id")"
  fi

  crash="$(pick_crash_point)"
  echo "-- round $round: crash vacuum crash_point=$crash --"
  set +e
  WEAVER_VACUUM_CRASH_POINT="$crash" \
    "$PSQL" -D "$DATADIR" persistfuzz >/dev/null 2>&1 <<'SQL'
vacuum persist_fz;
SQL
  rc=$?
  set -e
  unset WEAVER_VACUUM_CRASH_POINT
  if [[ "$rc" -eq 0 ]]; then
    echo "FAIL: round $round expected FATAL from crash_point=$crash" >&2
    return 1
  fi
  echo "  crash observed rc=$rc"

  echo "-- round $round: restart + vacuum + check --"
  out="$(run_sql persistfuzz "vacuum persist_fz;")"
  assert_no_error "round $round post-crash vacuum" "$out"

  out="$(run_sql persistfuzz "select id from persist_fz order by id;")"
  assert_no_error "round $round select live ids" "$out"
  got="$(set_sorted "$(parse_ids "$out")")"
  want="$(set_sorted "$live")"
  if [[ "$got" != "$want" ]]; then
    echo "FAIL: round $round live id mismatch" >&2
    echo "  want: $want" >&2
    echo "  got:  $got" >&2
    echo "$out" >&2
    return 1
  fi
  echo "  OK heap ids = $want"

  id="$(set_pick "$deleted")"
  echo "  reinsert deleted id=$id"
  out="$(run_sql persistfuzz "insert into persist_fz values ($id, 're$id', '$(vec_for "$id")');")"
  assert_no_error "round $round reinsert id=$id (stale unique TID?)" "$out"
  live="$(set_add "$live" "$id")"
  deleted="$(set_remove "$deleted" "$id")"

  out="$(run_sql persistfuzz "
set enable_seqscan = off;
set ivfflat.probes = 2;
select id from persist_fz order by emb <-> '[1,0,0]' limit 8;
")"
  assert_no_error "round $round ann" "$out"
  got="$(parse_ids "$out")"
  for id in $got; do
    if set_has "$id" "$deleted"; then
      echo "FAIL: round $round ANN returned deleted id=$id" >&2
      echo "$out" >&2
      return 1
    fi
  done
  echo "  OK ANN ids subset of live ($got)"

  rm -rf "$DATADIR"
  DATADIR=""
  echo "OK round $round"
}

r=1
while [[ "$r" -le "$ROUNDS" ]]; do
  run_round "$r"
  r=$((r + 1))
done

echo "persistence_crash_fuzzer: OK seed=$SEED rounds=$ROUNDS"
