#!/bin/sh
# Discovers system Python build settings for useSystemPython=1 builds.
# BIGWORLD_BEGIN(3.13 migration)
# Was hard-coded to the Python 2.4/2.x 'python-config'; now prefers a
# 3.13 interpreter and falls back to the generic python3-config.
# BIGWORLD_END

PYTHON_VERSION=3.13

PATH=$PATH:/usr/bin:/usr/local/bin
export PATH

find_config_program() {

	# Prefer a version-matched config, then any python3 config.
	for prog in python${PYTHON_VERSION}-config python3-config python-config
	do
		if command -v "$prog" >/dev/null 2>&1
		then
			echo "$prog"
			return 0
		fi
	done

	return 1
}

PY_CONFIG=`find_config_program`
ret=$?
if [ $ret == 0 ]; then
	$PY_CONFIG $1
else
	echo "ERROR: Unable to locate 'python3-config' in PATH=$PATH"
	exit $ret
fi
