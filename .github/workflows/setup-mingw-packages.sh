#!/bin/sh
set -eu

# TODO: Stop using this PPA once all MinGW dependencies are available through vcpkg and work with CMake.

# Only these packages from tobydox's PPA are currently used:
#   libgig-mingw-w64    [vcpkg does not support MinGW yet]
#   qt5base-mingw-w64   [vcpkg takes too much memory to build this]
#   qt5svg-mingw-w64

# The rest of the MinGW dependencies come from vcpkg.
# suil, once added, would need to be built with Qt, due to its dependency on it.

# The PPA's signing keys are committed beside this script and pinned with signed-by=.
# They used to be fetched at run time with
#   apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 72931B47...
# which needs outbound HKP: on the hosted runner that call died with
#   gpg: keyserver receive failed: Network is unreachable
# and the following apt-get update failed with exit 100 ("repository ... is not signed").
# A build dependency must not depend on a network keyserver. The committed keyring holds
# both keys Launchpad signs this PPA's metadata with - the current 4096-bit key
# AD1A987F6AC01FA7B66FB4EB9C0C72C11E0AF891 and the legacy 72931B477E22FEFD47F8DECE02FE5F12ADDE29B2 -
# because a keyring with only the legacy key verifies the release file incompletely.
# Refresh instructions and the fixture's provenance are in tests/data/mingw-ppa/README.md.
KEYRING_SRC="$(dirname "$0")/keys/tobydox-mingw-w64.gpg"
install -d -m 0755 /etc/apt/keyrings
install -m 0644 "$KEYRING_SRC" /etc/apt/keyrings/tobydox-mingw-w64.gpg
echo "deb [signed-by=/etc/apt/keyrings/tobydox-mingw-w64.gpg] http://ppa.launchpad.net/tobydox/mingw-w64/ubuntu focal main" \
  > /etc/apt/sources.list.d/tobydox-mingw-w64.list
apt-get update -y
