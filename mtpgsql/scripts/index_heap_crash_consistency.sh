#!/usr/bin/env bash
# Index-to-heap crash consistency harness (multiuser + shadow log).
#
# Initdb is CLI and all-or-nothing: if it succeeds, Ami xid 512 is committed
# on disk. If Ami is not committed after initdb, that is fatal for the suite.
# Production does not reconstruct that xid after a crash, and Ami must never
# be overwritten with 0.
#
# Every SQL process after initdb — setup, crash target, and recover —
# is weaver_embed_sql, which starts through initweaverbackend() /
# GoMultiuser() so pg_shadowlog is on. Do not use single-user postgres
# here: that path sets logging=false and never writes shadow pages.
#
# The crash target is killed from the outside (SIGKILL and/or a test-only
# write interceptor). Production code has no crash points. SIGKILL the
# embed worker, not a JVM. The Java-API equivalent (child JVM via
# WeaverInitializer) is pgjava_c IndexHeapCrashConsistencyJavaTest /
# ./gradlew :pgjava_c:indexHeapCrashLong.
#
# Each round:
#   1. Fresh datadir, table with unique btree + HNSW + IVFFlat (or crash
#      during CREATE INDEX, depending on scenario).
#   2. A mutating workload in a child weaver_embed_sql process.
#   3. Crash that child: timed SIGKILL, or SIGKILL after N page-sized writes
#      via libweaver_crash_injector (never linked into libweaver).
#   4. New embed process: replay shadow log, VACUUM, then check that
#      indexes agree with the heap.
#
# Post-crash live set is unknown (unflushed commits may vanish). Invariants
# are internal consistency, not "matches the pre-crash id set":
#   - seqscan ids == indexscan ids
#   - no duplicate unique keys in the heap
#   - a key absent from the heap is insertable on the unique index
#   - a key present in the heap is rejected by the unique index
#   - ANN hits are a subset of heap ids
#
# Usage:
#   ./mtpgsql/scripts/index_heap_crash_consistency.sh [seed] [rounds] [mtpg_prefix]
#
# Env:
#   WEAVER_CRASH_ROUNDS          default 25 (or $2)
#   WEAVER_CRASH_DURATION_SEC    optional wall-clock cap
#   WEAVER_CRASH_SMOKE=1         3 rounds, short workloads (Gradle/CI)
#   WEAVER_CRASH_KEEP=1          leave datadir on failure
#   WEAVER_CRASH_TIMEOUT_SEC     per-SQL timeout (default 90)
#   WEAVER_CRASH_SCENARIO        run only this scenario name
#   WEAVER_CRASH_INCLUDE_WRITES=1  include write-interceptor scenarios in smoke
#
# Replay:
#   ./mtpgsql/scripts/index_heap_crash_consistency.sh <seed> <rounds>

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

# A caller (Gradle, a previous preload test) must not leak the interceptor
# into initdb / recovery processes.
unset DYLD_INSERT_LIBRARIES DYLD_FORCE_FLAT_NAMESPACE LD_PRELOAD
unset WEAVER_CRASH_AFTER_WRITES WEAVER_CRASH_MIN_BYTES WEAVER_CRASH_GATE
SEED="${1:-}"
if [[ -z "$SEED" ]]; then
  SEED="$(date +%s)"
fi
RANDOM="$SEED"

if [[ "${WEAVER_CRASH_SMOKE:-}" == "1" ]]; then
  ROUNDS="${2:-${WEAVER_CRASH_ROUNDS:-3}}"
  START_N=12
  MAX_ID=24
  BURST=80
else
  ROUNDS="${2:-${WEAVER_CRASH_ROUNDS:-25}}"
  START_N=24
  MAX_ID=48
  BURST=400
fi

if [[ -n "${3:-}" ]]; then
  MTPG="$3"
elif [[ -x "${PGVECTOR_BUILD_DIR:-$ROOT/build_test}/mtpg/bin/weaver_embed_sql" ]]; then
  MTPG="${PGVECTOR_BUILD_DIR:-$ROOT/build_test}/mtpg"
else
  MTPG="$ROOT/build/mtpg"
fi

EMBED="$MTPG/bin/weaver_embed_sql"
INITDB="$MTPG/bin/initdb"
TIMEOUT_SEC="${WEAVER_CRASH_TIMEOUT_SEC:-90}"
DURATION_SEC="${WEAVER_CRASH_DURATION_SEC:-}"
START_EPOCH="$(date +%s)"

if [[ ! -x "$EMBED" || ! -x "$INITDB" ]]; then
  echo "missing mtpg binaries under $MTPG (need weaver_embed_sql + initdb; rebuild postgres)" >&2
  exit 1
fi

# weaver_embed_sql is linked to libweaver.
export DYLD_LIBRARY_PATH="$MTPG/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export LD_LIBRARY_PATH="$MTPG/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

ulimit -c 0 2>/dev/null || true

SOEXT="so"
if [[ "$(uname -s)" == "Darwin" ]]; then
  SOEXT="dylib"
fi

INJECTOR="$MTPG/lib/libweaver_crash_injector.$SOEXT"

ensure_injector() {
  if [[ -f "$INJECTOR" ]]; then
    return 0
  fi
  local src="$ROOT/mtpgsql/src/test/crash_injector/crash_injector.c"
  local out="${TMPDIR:-/tmp}/libweaver_crash_injector.$$.$SOEXT"
  if [[ ! -f "$src" ]]; then
    return 1
  fi
  if ! cc -shared -fPIC -o "$out" "$src" 2>/dev/null; then
    return 1
  fi
  INJECTOR="$out"
  return 0
}

HAS_INJECTOR=0
if ensure_injector; then
  HAS_INJECTOR=1
fi

SCENARIOS="dml_timed vacuum_timed mixed_timed build_hnsw_timed dml_writes vacuum_writes mixed_writes build_btree_timed"
if [[ "${WEAVER_CRASH_SMOKE:-}" == "1" && -z "${WEAVER_CRASH_SCENARIO:-}" && "${WEAVER_CRASH_INCLUDE_WRITES:-}" != "1" ]]; then
  # Write-count kills can hit initdb-durable pg_log pages. A missing
  # Ami xid after a successful initdb is a crash-recovery failure, not a
  # reason to re-run initdb. Keep write scenarios in the long suite; CI
  # smoke uses timed SIGKILL after ready.
  SCENARIOS="dml_timed vacuum_timed mixed_timed build_hnsw_timed build_btree_timed"
fi
if [[ -n "${WEAVER_CRASH_SCENARIO:-}" ]]; then
  SCENARIOS="$WEAVER_CRASH_SCENARIO"
fi

echo "index_heap_crash_consistency: seed=$SEED rounds=$ROUNDS mtpg=$MTPG injector=$HAS_INJECTOR scenarios=$SCENARIOS mode=multiuser shadowlog=on"

# --- helpers ---

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

set_sorted() {
  echo "$1" | tr ' ' '\n' | grep -E '^[0-9]+$' | sort -n | tr '\n' ' ' | sed 's/ *$//'
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

vec_for() {
  echo "[$1,0,0]"
}

parse_col() {
  local col="$1"
  echo "$2" | grep -oE "${col} = \"[^\"]+\"" | sed "s/${col} = \"//;s/\"$//" | tr '\n' ' ' | sed 's/ *$//'
}

parse_ids() {
  parse_col id "$1"
}

DATADIR=""
WORKDIR=""
KEEP_ON_FAIL=0

cleanup() {
  unset DYLD_INSERT_LIBRARIES DYLD_FORCE_FLAT_NAMESPACE LD_PRELOAD
  unset WEAVER_CRASH_AFTER_WRITES WEAVER_CRASH_MIN_BYTES WEAVER_CRASH_GATE
  if [[ "$KEEP_ON_FAIL" -eq 1 && -n "$DATADIR" ]]; then
    echo "keeping datadir $DATADIR (WEAVER_CRASH_KEEP=1)" >&2
    return
  fi
  if [[ -n "$DATADIR" ]]; then
    rm -rf "$DATADIR"
  fi
  if [[ -n "$WORKDIR" ]]; then
    rm -rf "$WORKDIR"
  fi
}
trap cleanup EXIT

fail() {
  echo "FAIL: $*" >&2
  echo "  seed=$SEED datadir=${DATADIR:-none}" >&2
  if [[ "${WEAVER_CRASH_KEEP:-}" == "1" ]]; then
    KEEP_ON_FAIL=1
  fi
  exit 1
}

run_sql_raw() {
  local db="$1"
  local sql="$2"
  unset DYLD_INSERT_LIBRARIES DYLD_FORCE_FLAT_NAMESPACE LD_PRELOAD
  unset WEAVER_CRASH_AFTER_WRITES WEAVER_CRASH_MIN_BYTES WEAVER_CRASH_GATE
  perl -e 'alarm shift; exec @ARGV' "$TIMEOUT_SEC" \
    "$EMBED" -D "$DATADIR" "$db" 2>&1 <<SQL
$sql
SQL
}

run_sql() {
  local db="$1"
  local sql="$2"
  local out rc
  set +e
  out="$(run_sql_raw "$db" "$sql")"
  rc=$?
  set -e
  if [[ "$rc" -ne 0 ]]; then
    echo "$out" >&2
    fail "weaver_embed_sql exited $rc running: ${sql:0:80}"
  fi
  if echo "$out" | grep -qiE 'ERROR:|FATAL|syntax error'; then
    echo "$out" >&2
    fail "SQL error: ${sql:0:80}"
  fi
  echo "$out"
}

sql_may_error() {
  local db="$1"
  local sql="$2"
  local out rc
  set +e
  out="$(run_sql_raw "$db" "$sql")"
  rc=$?
  set -e
  echo "$out"
  return 0
}

write_inserts() {
  local file="$1"
  local from="$2"
  local to="$3"
  local i
  for i in $(seq "$from" "$to"); do
    echo "insert into ihc_t values ($i, 'v$i', '$(vec_for "$i")');" >>"$file"
  done
}

# Extra DML so timed SIGKILL usually lands mid-workload, not after exit.
append_churn() {
  local file="$1"
  local i id
  local n="$BURST"
  for i in $(seq 1 "$n"); do
    id=$((1 + (i % MAX_ID)))
    case $((i % 6)) in
      0)
        echo "vacuum ihc_t;" >>"$file"
        ;;
      1)
        echo "delete from ihc_t where id = $id;" >>"$file"
        echo "insert into ihc_t values ($id, 'c$i', '$(vec_for "$id")');" >>"$file"
        ;;
      *)
        echo "update ihc_t set val = 'c$i' where id = $id;" >>"$file"
        ;;
    esac
  done
}

pick_scenario() {
  if [[ -n "${WEAVER_CRASH_SCENARIO:-}" ]]; then
    echo "$WEAVER_CRASH_SCENARIO"
    return
  fi
  set -- $SCENARIOS
  shift $((RANDOM % $#))
  echo "$1"
}

uses_writes() {
  [[ "$1" == *_writes ]]
}

scenario_base() {
  echo "${1%_timed}" | sed 's/_writes$//'
}

# --- crash launch ---

wait_crash_ready() {
  local pid="$1"
  local gate="$2"
  local i
  for i in $(seq 1 200); do
    if [[ -f "$gate" ]]; then
      return 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      return 1
    fi
    sleep 0.05
  done
  return 1
}

launch_and_crash() {
  local sqlfile="$1"
  local mode="$2"   # timed | writes
  local outf="$WORKDIR/crash.out"
  local gate="$WORKDIR/crash.gate"
  local pid delay writes rc watchdog

  : >"$outf"
  rm -f "$gate"
  export WEAVER_CRASH_GATE="$gate"

  if [[ "$mode" == "writes" ]]; then
    if [[ "$HAS_INJECTOR" -ne 1 ]]; then
      mode="timed"
    fi
  fi

  if [[ "$mode" == "writes" ]]; then
    writes=$((40 + RANDOM % 80))
    echo "  crash mode=writes after_writes=$writes injector=$INJECTOR"
    export WEAVER_CRASH_AFTER_WRITES="$writes"
    export WEAVER_CRASH_MIN_BYTES=4096
    if [[ "$(uname -s)" == "Darwin" ]]; then
      export DYLD_INSERT_LIBRARIES="$INJECTOR"
    else
      export LD_PRELOAD="$INJECTOR"
    fi
    set +e
    "$EMBED" -D "$DATADIR" persistcrash <"$sqlfile" >"$outf" 2>&1 &
    pid=$!
    wait_crash_ready "$pid" "$gate"
    (
      sleep "$TIMEOUT_SEC"
      kill -9 "$pid" 2>/dev/null
    ) &
    watchdog=$!
    wait "$pid"
    rc=$?
    kill "$watchdog" 2>/dev/null
    wait "$watchdog" 2>/dev/null
    set -e
    unset DYLD_INSERT_LIBRARIES DYLD_FORCE_FLAT_NAMESPACE LD_PRELOAD
    unset WEAVER_CRASH_AFTER_WRITES WEAVER_CRASH_MIN_BYTES WEAVER_CRASH_GATE
  else
    delay="$(awk -v r="$RANDOM" 'BEGIN { printf "%.3f", 0.4 + (r % 1200) / 1000 }')"
    echo "  crash mode=timed delay=${delay}s (after ready)"
    set +e
    (
      cat "$sqlfile"
      sleep 2
    ) | "$EMBED" -D "$DATADIR" persistcrash >"$outf" 2>&1 &
    pid=$!
    wait_crash_ready "$pid" "$gate"
    sleep "$delay"
    kill -9 "$pid" 2>/dev/null
    wait "$pid"
    rc=$?
    set -e
    unset WEAVER_CRASH_GATE
  fi

  echo "  child exited rc=$rc"
  sleep 0.15
  return 0
}

# --- consistency checks ---

index_exists() {
  local name="$1"
  local out
  out="$(run_sql persistcrash "select relname from pg_class where relname = '$name';")"
  echo "$out" | grep -qE "relname = \"${name}\""
}

# Recover FATAL "this should not be happening" means AmiTransactionId is
# not committed. That bit is written by a successful initdb and must not
# be overwritten with 0. After initdb has been checked, this is fatal.
is_missing_bootstrap_xid() {
  echo "$1" | grep -qE "this should not be happening|Ami xid 512"
}

assert_consistent() {
  local out heap_ids idx_ids ann_ids dups probe present missing

  echo "  vacuum + recover"
  out="$(sql_may_error persistcrash "vacuum ihc_t;")"
  if is_missing_bootstrap_xid "$out"; then
    echo "$out" >&2
    fail "Ami xid 512 was committed after initdb but missing after crash"
  fi
  if echo "$out" | grep -qiE 'ERROR:|FATAL'; then
    echo "$out" >&2
    fail "post-crash vacuum failed"
  fi

  out="$(run_sql persistcrash "
set enable_indexscan = off;
set enable_seqscan = on;
select id from ihc_t order by id;
")"
  heap_ids="$(set_sorted "$(parse_ids "$out")")"
  echo "  heap ids: ${heap_ids:-<empty>}"

  dups="$(echo "$heap_ids" | tr ' ' '\n' | grep -E '^[0-9]+$' | uniq -d | tr '\n' ' ' | sed 's/ *$//')"
  if [[ -n "$dups" ]]; then
    echo "$out" >&2
    fail "duplicate heap ids after crash/recover: $dups"
  fi

  if index_exists "ihc_id_idx"; then
    out="$(run_sql persistcrash "
set enable_seqscan = off;
set enable_indexscan = on;
select id from ihc_t order by id;
")"
    idx_ids="$(set_sorted "$(parse_ids "$out")")"
    if [[ "$heap_ids" != "$idx_ids" ]]; then
      echo "  heap: $heap_ids" >&2
      echo "  idx:  $idx_ids" >&2
      echo "$out" >&2
      fail "unique btree scan disagrees with heap seqscan (torn generation or missing recover?)"
    fi
    echo "  OK btree ids match heap"

    present=""
    missing=""
    local id
    for id in $(seq 1 "$MAX_ID"); do
      if set_has "$id" "$heap_ids"; then
        present="$(set_add "$present" "$id")"
      else
        missing="$(set_add "$missing" "$id")"
      fi
    done

    if [[ "$(set_count "$present")" -gt 0 ]]; then
      probe="$(set_pick "$present")"
      out="$(sql_may_error persistcrash "insert into ihc_t values ($probe, 'dup', '$(vec_for "$probe")');")"
      if ! echo "$out" | grep -qiE 'ERROR:|duplicate|already exists|Cannot insert'; then
        echo "$out" >&2
        fail "unique index accepted duplicate id=$probe"
      fi
      echo "  OK unique reject id=$probe"
    fi

    if [[ "$(set_count "$missing")" -gt 0 ]]; then
      probe="$(set_pick "$missing")"
      out="$(sql_may_error persistcrash "insert into ihc_t values ($probe, 're', '$(vec_for "$probe")');")"
      if echo "$out" | grep -qiE 'ERROR:|FATAL'; then
        echo "$out" >&2
        fail "stale unique TID blocked insert of absent id=$probe"
      fi
      echo "  OK reinsert absent id=$probe"
      # Put the row back out so later ANN checks still match heap_ids,
      # or refresh heap_ids. Simpler to delete it again.
      run_sql persistcrash "delete from ihc_t where id = $probe;" >/dev/null
    fi
  else
    echo "  unique index absent after crash (ok if crash was mid-build)"
  fi

  if index_exists "ihc_hnsw" || index_exists "ihc_ivf"; then
    out="$(run_sql persistcrash "
set enable_seqscan = off;
set ivfflat.probes = 2;
select id from ihc_t order by emb <-> '[1,0,0]' limit 8;
")"
    ann_ids="$(parse_ids "$out")"
    local id
    for id in $ann_ids; do
      if ! set_has "$id" "$heap_ids"; then
        echo "$out" >&2
        fail "ANN returned id=$id not in heap (${heap_ids:-<empty>})"
      fi
    done
    echo "  OK ANN ids subset of heap ($ann_ids)"
  fi

  out="$(sql_may_error persistcrash "vacuum ihc_t;")"
  if is_missing_bootstrap_xid "$out"; then
    echo "$out" >&2
    fail "Ami xid 512 was committed after initdb but missing after crash"
  fi
  if echo "$out" | grep -qiE 'ERROR:|FATAL'; then
    echo "$out" >&2
    fail "second vacuum failed"
  fi
  echo "  OK second vacuum"
}

# --- round ---

run_round() {
  local round="$1"
  local scenario crash_mode base sqlf i id n_live

  scenario="$(pick_scenario)"
  if uses_writes "$scenario"; then
    crash_mode="writes"
  else
    crash_mode="timed"
  fi
  base="$(scenario_base "$scenario")"

  DATADIR="$(mktemp -d "${TMPDIR:-/tmp}/weaver-ihc.XXXXXX")"
  WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/weaver-ihc-sql.XXXXXX")"
  "$INITDB" -D "$DATADIR" >/dev/null
  echo "  checking Ami xid after initdb"
  ami_out="$(sql_may_error template1 "select 1;")"
  if is_missing_bootstrap_xid "$ami_out"; then
    echo "$ami_out" >&2
    fail "Ami xid 512 not committed after initdb; ending tests"
  fi
  run_sql template1 "create database persistcrash;" >/dev/null

  echo "-- round $round scenario=$scenario --"

  sqlf="$WORKDIR/work.sql"
  : >"$sqlf"

  case "$base" in
    build_btree)
      run_sql persistcrash "
create table ihc_t (id int4, val varchar(64), emb vector);
" >/dev/null
      write_inserts "$WORKDIR/seed.sql" 1 "$START_N"
      run_sql persistcrash "$(cat "$WORKDIR/seed.sql")" >/dev/null
      echo "create unique index ihc_id_idx on ihc_t (id);" >>"$sqlf"
      # extra inserts while the index exists only if create finishes
      write_inserts "$sqlf" $((START_N + 1)) "$MAX_ID"
      ;;
    build_hnsw)
      run_sql persistcrash "
create table ihc_t (id int4, val varchar(64), emb vector);
create unique index ihc_id_idx on ihc_t (id);
" >/dev/null
      write_inserts "$WORKDIR/seed.sql" 1 "$START_N"
      run_sql persistcrash "$(cat "$WORKDIR/seed.sql")" >/dev/null
      echo "create index ihc_hnsw on ihc_t using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32);" >>"$sqlf"
      echo "create index ihc_ivf on ihc_t using ivfflat (emb vector_l2_ops) with (lists = 2);" >>"$sqlf"
      ;;
    vacuum)
      run_sql persistcrash "
create table ihc_t (id int4, val varchar(64), emb vector);
create unique index ihc_id_idx on ihc_t (id);
" >/dev/null
      write_inserts "$WORKDIR/seed.sql" 1 "$START_N"
      run_sql persistcrash "$(cat "$WORKDIR/seed.sql")" >/dev/null
      run_sql persistcrash "
create index ihc_hnsw on ihc_t using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32);
create index ihc_ivf on ihc_t using ivfflat (emb vector_l2_ops) with (lists = 2);
" >/dev/null
      for i in $(seq 1 2 "$START_N"); do
        echo "delete from ihc_t where id = $i;" >>"$sqlf"
      done
      echo "vacuum ihc_t;" >>"$sqlf"
      echo "vacuum ihc_t;" >>"$sqlf"
      ;;
    mixed)
      run_sql persistcrash "
create table ihc_t (id int4, val varchar(64), emb vector);
create unique index ihc_id_idx on ihc_t (id);
" >/dev/null
      write_inserts "$WORKDIR/seed.sql" 1 "$START_N"
      run_sql persistcrash "$(cat "$WORKDIR/seed.sql")" >/dev/null
      run_sql persistcrash "
create index ihc_hnsw on ihc_t using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32);
create index ihc_ivf on ihc_t using ivfflat (emb vector_l2_ops) with (lists = 2);
" >/dev/null
      live=""
      deleted=""
      for id in $(seq 1 "$START_N"); do
        live="$(set_add "$live" "$id")"
      done
      i=0
      while [[ "$i" -lt "$BURST" ]]; do
        n_live="$(set_count "$live")"
        n_deleted="$(set_count "$deleted")"
        op=$((RANDOM % 4))
        if [[ "$n_live" -le 2 ]]; then
          op=0
        elif [[ "$n_deleted" -eq 0 && "$n_live" -ge "$MAX_ID" ]]; then
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
            echo "insert into ihc_t values ($id, 'v$id', '$(vec_for "$id")');" >>"$sqlf"
            live="$(set_add "$live" "$id")"
            deleted="$(echo "$deleted" | tr ' ' '\n' | grep -v "^${id}$" | tr '\n' ' ')"
            ;;
          1)
            id="$(set_pick "$live")"
            echo "delete from ihc_t where id = $id;" >>"$sqlf"
            live="$(echo "$live" | tr ' ' '\n' | grep -v "^${id}$" | tr '\n' ' ')"
            deleted="$(set_add "$deleted" "$id")"
            ;;
          2)
            id="$(set_pick "$live")"
            echo "update ihc_t set val = 'u$id', emb = '[0,$id,0]' where id = $id;" >>"$sqlf"
            ;;
          3)
            echo "vacuum ihc_t;" >>"$sqlf"
            ;;
        esac
        i=$((i + 1))
      done
      ;;
    dml|*)
      run_sql persistcrash "
create table ihc_t (id int4, val varchar(64), emb vector);
create unique index ihc_id_idx on ihc_t (id);
" >/dev/null
      write_inserts "$WORKDIR/seed.sql" 1 "$START_N"
      run_sql persistcrash "$(cat "$WORKDIR/seed.sql")" >/dev/null
      run_sql persistcrash "
create index ihc_hnsw on ihc_t using hnsw (emb vector_l2_ops) with (m = 8, ef_construction = 32);
create index ihc_ivf on ihc_t using ivfflat (emb vector_l2_ops) with (lists = 2);
" >/dev/null
      write_inserts "$sqlf" $((START_N + 1)) "$MAX_ID"
      for i in $(seq 1 3 "$START_N"); do
        echo "update ihc_t set emb = '[0,$i,0]' where id = $i;" >>"$sqlf"
      done
      for i in $(seq 2 4 "$START_N"); do
        echo "delete from ihc_t where id = $i;" >>"$sqlf"
      done
      write_inserts "$sqlf" 2 2
      ;;
  esac

  append_churn "$sqlf"
  launch_and_crash "$sqlf" "$crash_mode"
  assert_consistent

  rm -rf "$DATADIR" "$WORKDIR"
  DATADIR=""
  WORKDIR=""

  echo "OK round $round"
  return 0
}

r=1
while [[ "$r" -le "$ROUNDS" ]]; do
  if [[ -n "$DURATION_SEC" ]]; then
    now="$(date +%s)"
    if [[ $((now - START_EPOCH)) -ge "$DURATION_SEC" ]]; then
      echo "duration cap ${DURATION_SEC}s reached after $((r - 1)) rounds"
      break
    fi
  fi
  set +e
  run_round "$r"
  rc=$?
  set -e
  if [[ "$rc" -ne 0 ]]; then
    exit "$rc"
  fi
  r=$((r + 1))
done

echo "index_heap_crash_consistency: OK seed=$SEED rounds=$((r - 1))"
