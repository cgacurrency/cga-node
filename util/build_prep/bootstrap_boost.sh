#!/usr/bin/env bash

bootstrapArgs=()
buildArgs=()
useClang='false'
useLibCXX='false'
keepArchive='false'
debugLevel=0
buildCArgs=()
buildCXXArgs=()
buildLDArgs=()
boostVersion='1.69'
while getopts 'hmcCkpvB:' OPT; do
	case "${OPT}" in
		h)
			echo "Usage: bootstrap_boost.sh [-hmcCkpv] [-B <boostVersion>]"
			echo "   -h                 This help"
			echo "   -m                 Build a minimal set of libraries needed for CGA"
			echo "   -c                 Use Clang"
			echo "   -C                 Use libc++ when using Clang"
			echo "   -k                 Keep the downloaded archive file"
			echo "   -p                 Build a PIC version of the objects"
			echo "   -v                 Increase debug level, may be repeated to increase it"
			echo "                      further"
			echo "   -B <boostVersion>  Specify version of Boost to build"
			exit 0
			;;
		m)
			bootstrapArgs+=('--with-libraries=thread,log,filesystem,program_options')
			;;
		c)
			useClang='true'
			;;
		C)
			useLibCXX='true'
			;;
		k)
			keepArchive='true'
			;;
		p)
			buildCXXArgs+=(-fPIC)
			buildCArgs+=(-fPIC)
			;;
		v)
			debugLevel=$[$debugLevel + 1]
			;;
		B)
			boostVersion="${OPTARG}"
			;;
	esac
done

set -o errexit
set -o xtrace

if ! c++ --version >/dev/null 2>/dev/null; then
	useClang='true'

	if ! clang++ --version >/dev/null 2>/dev/null; then
		echo "Unable to find a usable toolset" >&2

		exit 1
	fi
fi

if [ "${useClang}" = 'true' ]; then
	bootstrapArgs+=(--with-toolset=clang)
	buildArgs+=(toolset=clang)
	if [ "${useLibCXX}" = 'true' ]; then
		buildCXXArgs+=(-stdlib=libc++)
		buildLDArgs+=(-stdlib=libc++)
	fi
fi

case "${boostVersion}" in
	1.66)
		BOOST_BASENAME=boost_1_66_0
		BOOST_URL=https://downloads.sourceforge.net/project/boost/boost/1.66.0/${BOOST_BASENAME}.tar.bz2
		BOOST_ARCHIVE_SHA256='5721818253e6a0989583192f96782c4a98eb6204965316df9f5ad75819225ca9'
		;;
	1.69)
		BOOST_BASENAME=boost_1_69_0
		BOOST_URL=https://downloads.sourceforge.net/project/boost/boost/1.69.0/${BOOST_BASENAME}.tar.bz2
		BOOST_ARCHIVE_SHA256='8f32d4617390d1c2d16f26a27ab60d97807b35440d45891fa340fc2648b04406'
		;;
	*)
		echo "Unsupported Boost version: ${boostVersion}" >&2
		exit 1
		;;
esac
BOOST_ARCHIVE="${BOOST_BASENAME}.tar.bz2"
BOOST_ROOT=${BOOST_ROOT-/usr/local/boost}

if [ ! -f "${BOOST_ARCHIVE}" ]; then
	wget --quiet -O "${BOOST_ARCHIVE}.new" "${BOOST_URL}"
	checkHash="$(openssl dgst -sha256 "${BOOST_ARCHIVE}.new" | sed 's@^.*= *@@')"
	if [ "${checkHash}" != "${BOOST_ARCHIVE_SHA256}" ]; then
		echo "Checksum mismatch.  Expected ${BOOST_ARCHIVE_SHA256}, got ${checkHash}" >&2

		exit 1
	fi
	mv "${BOOST_ARCHIVE}.new" "${BOOST_ARCHIVE}" || exit 1
else
	keepArchive='true'
fi

if [ -n "${buildCArgs[*]}" ]; then
	buildArgs+=(cflags="${buildCArgs[*]}")
fi

if [ -n "${buildCXXArgs[*]}" ]; then
	buildArgs+=(cxxflags="${buildCXXArgs[*]}")
fi

if [ -n "${buildLDArgs[*]}" ]; then
	buildArgs+=(linkflags="${buildLDArgs[*]}")
fi

rm -rf "${BOOST_BASENAME}"
tar xf "${BOOST_ARCHIVE}"

pushd "${BOOST_BASENAME}"
./bootstrap.sh "${bootstrapArgs[@]}"
./b2 -d${debugLevel} --prefix="${BOOST_ROOT}" link=static "${buildArgs[@]}" install
popd

rm -rf "${BOOST_BASENAME}"
if [ "${keepArchive}" != 'true' ]; then
	rm -f "${BOOST_ARCHIVE}"
fi
