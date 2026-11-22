#!/usr/bin/env bash
# Cluster black-box smoke bootstrap.
#
# Brings up the manager plane of the BigWorld cluster on this machine and
# verifies it reaches steady state:
#   MariaDB (pre-installed) -> bwmachined2 -> dbappmgr/cellappmgr/baseappmgr.
#
# The R4 asset tree (programming/fantasydemo/res) is a minimal placeholder: it
# carries entity definitions but no space definitions, so the managers come up
# and discover each other but no cellapp/baseapp/loginapp instances are
# spawned. The deeper login -> entity -> cell-switch -> crash-recovery ->
# DB-digest chain needs real (non-placeholder) assets plus a login client and
# stays out of scope here (repo rule: do not fabricate assets).
#
# What this script asserts (the achievable black-box checks):
#   1. every process registers with machined under its REAL pid - pids above
#      65535 must not truncate (the uint16 MGM pid regression), and machined
#      must not deregister a live process;
#   2. the three managers find each other's interfaces;
#   3. MariaDB is reachable and the bigworld database/user exist.
#
# Usage:
#   bootstrap.sh              bring up and assert steady state
#   bootstrap.sh --teardown   stop machined and the managers
set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="$REPO_ROOT/game/bin/server/el7"
LOG_DIR="${TMPDIR:-/tmp}/cluster-smoke"
BW_RES_PATH="$REPO_ROOT/scripts/cluster-smoke/res:$REPO_ROOT/game/res/bigworld:$REPO_ROOT/programming/fantasydemo/res"
DB_USER=bigworld
DB_PASS=bigworld
DB_NAME=bigworld
STEADY_WAIT=30

mkdir -p "$LOG_DIR"

log()  { printf '[cluster-smoke] %s\n' "$*"; }
fail() { printf '[cluster-smoke] FAIL: %s\n' "$*" >&2; exit 1; }

pids_of() { pgrep -x "$1" 2>/dev/null || true; }

stop_all() {
	local p
	for p in $(pids_of dbappmgr) $(pids_of cellappmgr) $(pids_of baseappmgr); do
		kill "$p" 2>/dev/null || true
	done
	if [ -f /tmp/bwmachined.pid ]; then
		kill "$(cat /tmp/bwmachined.pid)" 2>/dev/null || true
		rm -f /tmp/bwmachined.pid
	fi
	# Fallback: the pidfile may be stale; take anything still running.
	for p in $(pgrep -x bwmachined2 2>/dev/null); do
		kill "$p" 2>/dev/null || true
	done
	sleep 1
}

teardown() {
	log "stopping machined and managers"
	stop_all
	log "stopped"
}

provision_db() {
	# MariaDB on this host uses socket auth for root; sudo -n mysql is the
	# passwordless path. Idempotent: CREATE IF NOT EXISTS everywhere.
	if ! sudo -n mysql <<SQL
CREATE DATABASE IF NOT EXISTS $DB_NAME CHARACTER SET utf8mb4;
CREATE USER IF NOT EXISTS '$DB_USER'@'localhost' IDENTIFIED BY '$DB_PASS';
CREATE USER IF NOT EXISTS '$DB_USER'@'127.0.0.1' IDENTIFIED BY '$DB_PASS';
GRANT ALL PRIVILEGES ON $DB_NAME.* TO '$DB_USER'@'localhost';
GRANT ALL PRIVILEGES ON $DB_NAME.* TO '$DB_USER'@'127.0.0.1';
FLUSH PRIVILEGES;
SQL
	then
		fail "cannot provision MariaDB (is mysqld running? is passwordless sudo available?)"
	fi
	if ! mysql -h 127.0.0.1 -u "$DB_USER" -p"$DB_PASS" -e "SELECT 1" "$DB_NAME" >/dev/null 2>&1; then
		fail "TCP login as $DB_USER failed"
	fi
	log "db ready ($DB_NAME@$DB_USER on 127.0.0.1:3306)"
}

ensure_user_config() {
	# BWConfig on Linux loads server/bw_<pw_name>.xml; the committed overlay is
	# named after its author, so regenerate it under the running user's name.
	local overlay="$REPO_ROOT/scripts/cluster-smoke/res/server/bw_$(id -un).xml"
	if [ ! -f "$overlay" ]; then
		cp "$REPO_ROOT/scripts/cluster-smoke/res/server/bw_cui.xml" "$overlay"
		log "generated per-user config $(basename "$overlay")"
	fi
}

start_machined() {
	if pgrep -x bwmachined2 >/dev/null 2>&1; then
		log "bwmachined2 already running"
		return
	fi
	# setsid detaches the new session so this script never waits on the daemon,
	# and </dev/null keeps its stdin off this script's stdout pipe.
	( cd "$REPO_ROOT" &&
	  setsid env BW_RES_PATH="$BW_RES_PATH" "$BIN/tools/bwmachined2" \
		-f -p /tmp/bwmachined.pid </dev/null >"$LOG_DIR/bwmachined2.log" 2>&1 & )
	sleep 2
	pgrep -x bwmachined2 >/dev/null 2>&1 || fail "bwmachined2 did not start"
	log "bwmachined2 up"
}

start_managers() {
	local app
	for app in dbappmgr cellappmgr baseappmgr; do
		( cd "$REPO_ROOT" &&
		  setsid env BW_RES_PATH="$BW_RES_PATH" "$BIN/server/$app" \
			</dev/null >"$LOG_DIR/$app.log" 2>&1 & )
	done
	sleep 2
	for app in dbappmgr cellappmgr baseappmgr; do
		pids_of "$app" | grep -q . || fail "$app did not start"
	done
	log "managers up"
}

wait_steady_state() {
	local app marker deadline
	deadline=$(( SECONDS + STEADY_WAIT ))
	while [ "$SECONDS" -lt "$deadline" ]; do
		local ok=1
		for app in dbappmgr cellappmgr baseappmgr; do
			marker="$(steady_marker "$app")"
			grep -q -- "---- $marker is running ----" "$LOG_DIR/$app.log" 2>/dev/null || ok=0
		done
		[ "$ok" = 1 ] && return 0
		sleep 1
	done
	for app in dbappmgr cellappmgr baseappmgr; do
		marker="$(steady_marker "$app")"
		grep -q -- "---- $marker is running ----" "$LOG_DIR/$app.log" 2>/dev/null ||
			fail "$app never reached steady state (see $LOG_DIR/$app.log)"
	done
}

# The log reports the manager's human name (DBAppMgr/CellAppMgr/BaseAppMgr).
steady_marker() {
	case "$1" in
	dbappmgr)   echo DBAppMgr ;;
	cellappmgr) echo CellAppMgr ;;
	baseappmgr) echo BaseAppMgr ;;
	*)          echo "$1" ;;
	esac
}

assert_real_pid_registration() {
	# The uint16 MGM pid regression: machined used to register a truncated pid
	# and then deregister the (still live) process within a second. Assert the
	# registry holds each manager's real pid.
	local app pid found
	for app in dbappmgr cellappmgr baseappmgr; do
		pid="$(pids_of "$app" | head -1)"
		[ -n "$pid" ] || fail "$app died during the run"
		found="$(sudo -n grep "bwmachined2" /var/log/syslog 2>/dev/null |
			grep "Added .* (pid:$pid)" | tail -1)"
		[ -n "$found" ] ||
			fail "machined has no registration for $app with real pid $pid (pid truncation regression?)"
		log "machined registered $app with real pid $pid"
	done
	# And nothing was falsely declared dead since the managers came up.
	if sudo -n grep "bwmachined2" /var/log/syslog 2>/dev/null |
		awk -v since="$(date -d '2 minutes ago' '+%Y-%m-%dT%H:%M')" '$0 >= since' |
		grep -q "died without deregistering"; then
		fail "machined declared a live process dead (liveness check regression)"
	fi
}

assert_mutual_discovery() {
	grep -q "Found interface BaseAppMgrInterface" "$LOG_DIR/cellappmgr.log" ||
		fail "cellappmgr did not find BaseAppMgr"
	grep -q "Found interface CellAppMgrInterface" "$LOG_DIR/baseappmgr.log" ||
		fail "baseappmgr did not find CellAppMgr"
	grep -q "Found interface CellAppMgrInterface" "$LOG_DIR/dbappmgr.log" ||
		fail "dbappmgr did not find CellAppMgr"
	log "managers discovered each other"
}

case "${1:-}" in
--teardown)
	teardown
	exit 0
	;;
esac

log "stopping any stale cluster"
stop_all
provision_db
ensure_user_config
start_machined
start_managers
wait_steady_state
assert_real_pid_registration
assert_mutual_discovery
log "cluster smoke PASS (manager plane steady state)"
