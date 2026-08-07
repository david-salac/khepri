#!/usr/bin/env bash
set -euo pipefail

BINDIR="bin"
CXX="${CXX:-g++}"

CXXFLAGS="-std=c++23 -Wall -Wextra -Wno-deprecated-declarations"

: "${II_SYSTEM:?Error: II_SYSTEM is not set}"

INGRESINC="$II_SYSTEM/ingres/files"
INGRESLIB="$II_SYSTEM/ingres/lib"

POSTGRESINC="/usr/include"
POSTGRESLIB="/usr/lib/x86_64-linux-gnu"

shopt -s nullglob
COMPILECPP=( *.cpp )

if (( ${#COMPILECPP[@]} == 0 )); then
  echo "Error: No .cpp files found in the current directory." >&2
  exit 1
fi

mkdir -p "$BINDIR"

echo "Compiling ${#COMPILECPP[@]} C++ files..."

"$CXX" $CXXFLAGS -DINGRESSQL -DPGSQL \
  -I. -I"$INGRESINC" \
  -I. -I"$POSTGRESINC" \
  "${COMPILECPP[@]}" \
  -L"$INGRESLIB" \
  -L"$POSTGRESLIB" \
  -linterp.1 -lframe.1 -lq.1 -lcompat.1 -lm \
  -Wl,-rpath,"$INGRESLIB" \
  -l:libiiapi.1.so \
  -lpqxx -lpq -lm \
  -o "$BINDIR/khepri"

echo "Build successful! Output saved to $BINDIR/khepri"