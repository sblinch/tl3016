#!/bin/bash
here=$(dirname -- $(readlink -f "$0"))

outfile=/var/www/html/ups.json
jsonfile=/tmp/ups.$$.json

function cleanup {
	rm -f $jsonfile
}

trap cleanup INT TERM EXIT

function report {
	local i

	# if at first you don't succeed...
	for i in 1 2 3; do
		$here/tripplite > $jsonfile
		[ $? -eq 0 ] && return 0
	done

	return 1
}

[ ! -x "$here/tripplite" ] && echo "must run ./build.sh to build the application first" && exit 1
[ -z "$(command -v uhubctl)" ] && echo "uhubctl must be installed (eg: apt-get install uhubctl)" && exit 1

[ ! -z "$1" ] && outfile="$1"

for a in `seq 1 10`; do
	echo "Polling UPS ..." >&2
	report
	if [ $? -eq 0 ]; then
		mv -f $jsonfile $outfile
		exit $?
	fi

	echo "Failed; resetting port ..." >&2

	portno=$( uhubctl | awk '/09ae:3016/ { p=$2; gsub("[^0-9]+","",p); print p}' );
	[ -z "$portno" ] && echo "cannot identify UPS port"  >&2 && exit 1
	uhubctl --ports $portno --action cycle

	sleep $(( a * 2 ))
done

echo "Failed; giving up" >&2
exit 1
