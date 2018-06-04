#!/bin/sh

opts="--prefix=$PWD/.. --with-pic --disable-shared --enable-static"

srdrir=''

while test $# -ge 1; do
  case "$1" in
    -h | --help)
      echo 'configure script for external package'
      exit 0
      ;;
    --prefix=*)
      shift
      ;;
    --srcdir=*)
      opts="$opts '$1'"
      srcdiropt=`echo $1 | sed 's/--srcdir=//'`
      srcdir=`readlink -f $srcdiropt`
      shift
      ;;
    *)
      shift
      ;;
  esac
done

if test -f config.status; then
  echo "configured already. skip $PWD"
else
  eval "$srcdir/configure $opts"
fi
