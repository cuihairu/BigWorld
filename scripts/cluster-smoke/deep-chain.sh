#!/usr/bin/env bash
# Cluster black-box deep-chain bootstrap (extends bootstrap.sh).
#
# Brings up the FULL server set for the client_integration/python/simple asset
# tree and asserts every process reaches steady state:
#   MariaDB -> bwmachined2 -> dbappmgr/cellappmgr/baseappmgr
#            -> dbapp -> cellapp -> baseapp -> loginapp
#
# Requires a baseapp without the bigworld.key license check: run
#   build_license_free_baseapp.sh
# first (that key is not shipped and cannot be fabricated, and baseapp aborts
# startup if it is missing - ping_manager.cpp).
#
# The deep chain (login -> entity create -> cell switch -> baseapp crash
# recovery -> DB digest) is driven afterwards by simple_python_client, built
# from the example's own sources. This script only gets the full app set up
# and asserted; the client drive and crash-recovery are separate steps so each
# failure is attributable.
#
# Usage:
#   deep-chain.sh               bring up the full app set and assert steady state
#   deep-chain.sh --teardown    stop everything this script started
set -u

# Capture cores when an app crashes; the app's cwd is the repo root, so a
# segfault leaves core.<pid> next to the cluster logs for a gdb backtrace.
ulimit -c unlimited 2>/dev/null || true

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="$REPO_ROOT/game/bin/server/el7"
EX_RES="$REPO_ROOT/programming/bigworld/examples/client_integration/python/simple/res"
SMOKE_RES="$REPO_ROOT/scripts/cluster-smoke/res"
LOG_DIR="${TMPDIR:-/tmp}/cluster-smoke"
BW_RES_PATH="$EX_RES:$REPO_ROOT/game/res/bigworld:$SMOKE_RES"
DB_USER=bigworld
DB_PASS=bigworld
DB_NAME=bigworld
STEADY_WAIT=45

# apps started in dependency order; (name, log marker). serviceapp is a
# BaseApp variant (Makefile.serviceapp) that DBApp and LoginApp require
# before they accept logons (desiredServiceApps defaults to 1).
APPS=(dbapp cellapp baseapp loginapp serviceapp)
mkdir -p "$LOG_DIR"

log()  { printf '[deep-chain] %s\n' "$*"; }
fail() { printf '[deep-chain] FAIL: %s\n' "$*" >&2; exit 1; }

pids_of() { pgrep -x "$1" 2>/dev/null || true; }

stop_all() {
	local p
	for p in $(pids_of loginapp) $(pids_of baseapp) $(pids_of cellapp) \
	         $(pids_of dbapp) $(pids_of serviceapp) \
	         $(pids_of dbappmgr) $(pids_of cellappmgr) \
	         $(pids_of baseappmgr); do
		kill "$p" 2>/dev/null || true
	done
	if [ -f /tmp/bwmachined.pid ]; then
		kill "$(cat /tmp/bwmachined.pid)" 2>/dev/null || true
		rm -f /tmp/bwmachined.pid
	fi
	for p in $(pgrep -x bwmachined2 2>/dev/null); do
		kill "$p" 2>/dev/null || true
	done
	sleep 2
}

teardown() {
	log "stopping full app set"
	stop_all
	log "stopped"
}

provision_db() {
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
	# BWConfig on Linux loads server/bw_<pw_name>.xml; regenerate it from the
	# deep overlay so config edits are picked up each run. BW_DEEP_OVERLAY lets
	# the DOC-GIF evidence pipeline (scripts/config-switch-evidence.py) supply a
	# per-switch variant without touching the committed bw_deep.xml.
	local overlay="${BW_DEEP_OVERLAY:-$REPO_ROOT/scripts/cluster-smoke/res/server/bw_deep.xml}"
	cp "$overlay" \
		"$REPO_ROOT/scripts/cluster-smoke/res/server/bw_$(id -un).xml"
	log "generated per-user config from $(basename "$overlay")"
}

ensure_loginapp_keys() {
	# LoginApp::initLogOnParamsEncoder() aborts startup without
	# server/loginapp.privkey, and the client refuses to connect without the
	# matching loginapp.pubkey. BigWorld ships neither (the operator generates
	# them per deployment), so provision a pair like provision_db(). Both live in
	# the smoke res, never in the example asset tree; the client is driven with
	# BW_RES_PATH="$EX_RES:$SMOKE_RES" so it finds the pubkey there.
	local priv="$SMOKE_RES/server/loginapp.privkey"
	local pub="$SMOKE_RES/loginapp.pubkey"
	if [ -s "$priv" ] && [ -s "$pub" ]; then
		log "loginapp keypair present"
		return
	fi
	command -v openssl >/dev/null 2>&1 || fail "openssl not found (needed to generate the loginapp keypair)"
	rm -f "$priv"
	openssl genrsa -out "$priv" 2048 >/dev/null 2>&1 \
		|| fail "openssl genrsa failed"
	openssl rsa -in "$priv" -pubout -out "$pub" >/dev/null 2>&1 \
		|| fail "openssl rsa -pubout failed"
	log "generated loginapp RSA keypair"
}

start_machined() {
	if pgrep -x bwmachined2 >/dev/null 2>&1; then
		log "bwmachined2 already running"
		return
	fi
	( cd "$REPO_ROOT" &&
	  setsid env BW_RES_PATH="$BW_RES_PATH" "$BIN/tools/bwmachined2" \
		-f -p /tmp/bwmachined.pid </dev/null >"$LOG_DIR/bwmachined2.log" 2>&1 & )
	sleep 2
	pgrep -x bwmachined2 >/dev/null 2>&1 || fail "bwmachined2 did not start"
	log "bwmachined2 up"
}

start_one() {
	local app="$1" logfile="${2:-$LOG_DIR/$1.log}"
	( cd "$REPO_ROOT" &&
	  setsid env BW_RES_PATH="$BW_RES_PATH" "$BIN/server/$app" \
		</dev/null >"$logfile" 2>&1 & )
}

start_managers() {
	local app
	for app in dbappmgr cellappmgr baseappmgr; do
		start_one "$app"
	done
	sleep 3
	for app in dbappmgr cellappmgr baseappmgr; do
		pids_of "$app" | grep -q . || fail "$app did not start"
	done
	log "managers up"
}

start_apps() {
	local app
	for app in "${APPS[@]}"; do
		start_one "$app"
		log "started $app"
		sleep 3
		pids_of "$app" | grep -q . || fail "$app did not start (see $LOG_DIR/$app.log)"

		# A second BaseApp instance. Without it BaseAppMgr has no backup to hand
		# the entities to when a BaseApp dies, so onBaseAppDeath() reports
		# "No backup for BaseApp" and shutDownServerOnBadState tears the whole
		# cluster down instead of recovering (baseappmgr.cpp:1634-1654). With two
		# BaseApps, adjustBackupLocations() assigns backups and the survivor takes
		# over the dead one's entities.
		if [ "$app" = baseapp ]; then
			start_one baseapp "$LOG_DIR/baseapp2.log"
			sleep 3
			log "started second baseapp"
		fi
	done
}

marker() {
	case "$1" in
	dbappmgr) echo DBAppMgr ;; cellappmgr) echo CellAppMgr ;;
	baseappmgr) echo BaseAppMgr ;; loginapp) echo LoginApp ;;
	baseapp) echo BaseApp ;; cellapp) echo CellApp ;; dbapp) echo DBApp ;;
	serviceapp) echo ServiceApp ;;
	*) echo "$1" ;;
	esac
}

wait_steady() {
	local app m deadline
	deadline=$(( SECONDS + STEADY_WAIT ))
	while [ "$SECONDS" -lt "$deadline" ]; do
		local ok=1
		for app in dbappmgr cellappmgr baseappmgr "${APPS[@]}"; do
			m="$(marker "$app")"
			grep -q -- "---- $m is running ----" "$LOG_DIR/$app.log" 2>/dev/null || ok=0
		done
		# second baseapp instance (backup partner)
		grep -q -- "---- BaseApp is running ----" "$LOG_DIR/baseapp2.log" 2>/dev/null || ok=0
		[ "$ok" = 1 ] && return 0
		sleep 1
	done
	for app in dbappmgr cellappmgr baseappmgr "${APPS[@]}"; do
		m="$(marker "$app")"
		grep -q -- "---- $m is running ----" "$LOG_DIR/$app.log" 2>/dev/null ||
			fail "$app never reached steady state (see $LOG_DIR/$app.log)"
	done
	grep -q -- "---- BaseApp is running ----" "$LOG_DIR/baseapp2.log" 2>/dev/null ||
		fail "second baseapp never reached steady state (see $LOG_DIR/baseapp2.log)"
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
ensure_loginapp_keys
start_machined
start_managers
start_apps
wait_steady
log "full app set steady: dbappmgr/cellappmgr/baseappmgr/dbapp/cellapp/baseapp/loginapp"
