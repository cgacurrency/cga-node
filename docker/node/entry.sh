#!/bin/bash

PATH="${PATH:-/bin}:/usr/bin"
export PATH

set -euo pipefail
IFS=$'\n\t'

network="$(cat /etc/cga-network)"
case "${network}" in
        live|'')
                network='live'
                dirSuffix=''
                ;;
        beta)
                dirSuffix='Beta'
                ;;
        test)
                dirSuffix='Test'
                ;;
esac


cgadir="${HOME}/CGA${dirSuffix}"
dbFile="${cgadir}/data.ldb"





	mkdir -p "${cgadir}"


if [ ! -f "${cgadir}/config.json" ]; then
        echo "Config File not found, adding default."
        cp "/usr/share/cga/config/${network}.json" "${cgadir}/config.json"
fi

# Start watching the log file we are going to log output to
logfile="${cgadir}/cga-docker-output.log"
tail -F "${logfile}" &

pid=''
firstTimeComplete=''
while true; do
	if [ -n "${firstTimeComplete}" ]; then
		sleep 10
	fi
	firstTimeComplete='true'

	if [ -f "${dbFile}" ]; then
		dbFileSize="$(stat -c %s "${dbFile}" 2>/dev/null)"
		if [ "${dbFileSize}" -gt $[1024 * 1024 * 1024 * 20] ]; then
			echo "ERROR: Database size grew above 20GB (size = ${dbFileSize})" >&2

			while [ -n "${pid}" ]; do
				kill "${pid}" >/dev/null 2>/dev/null || :
				if ! kill -0 "${pid}" >/dev/null 2>/dev/null; then
					pid=''
				fi
			done

			cga_node --vacuum
		fi
	fi

	if [ -n "${pid}" ]; then
		if ! kill -0 "${pid}" >/dev/null 2>/dev/null; then
			pid=''
		fi
	fi

	if [ -z "${pid}" ]; then
		cga_node --daemon &
		pid="$!"
	fi

	if [ "$(stat -c '%s' "${logfile}")" -gt 4194304 ]; then
		cp "${logfile}" "${logfile}.old"
		: > "${logfile}"
		echo "$(date) Rotated log file"
	fi
done >> "${logfile}" 2>&1
