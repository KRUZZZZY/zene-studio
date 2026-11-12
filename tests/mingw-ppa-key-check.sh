#!/usr/bin/env bash
# mingw-ppa-key-check.sh — regression for cluster ci-3eb9bda8aa6785a3.
#
# The failure this covers. The mingw64 job installs Qt 5 and libgig from tobydox's
# Launchpad PPA, and `.github/workflows/setup-mingw-packages.sh` used to fetch that PPA's
# signing key at run time from the network:
#
#     apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 72931B47...
#
# On the hosted runner that fetch cannot reach the keyserver. Job 130716216032 of run
# 43076107122 printed, after a 45-second stall,
#
#     gpg: keyserver receive failed: Network is unreachable
#
# so no key was imported and the very next `apt-get update` failed:
#
#     NO_PUBKEY 9C0C72C11E0AF891 NO_PUBKEY 02FE5F12ADDE29B2
#     E: The repository '.../tobydox/mingw-w64/ubuntu focal InRelease' is not signed.
#     ##[error]Process completed with exit code 100
#
# Two defects, one cause. (1) A build dependency is fetched from a network service at run
# time, so runner egress policy turns into a red job: the key must be committed. (2) The one
# key the old line names (02FE5F12ADDE29B2, 1024-bit, 2009) is no longer the PPA's only
# signer - Launchpad signs the PPA metadata with AD1A987F6AC01FA7B66FB4EB9C0C72C11E0AF891
# (4096-bit, 2024) as well, and the legacy key is now flagged "uses weak algorithm
# (rsa1024)" by apt. A keyring that holds only the legacy key therefore does not verify the
# release file completely (gpgv exit 2), which is the state the old step installed whenever
# its fetch did work.
#
# What this script checks, without network access and without root:
#   1. the live install path is still the one this test covers (build.yml runs the script,
#      the dependency list still names the PPA packages);
#   2. the product script installs the PPA keyring committed beside it and pins the source
#      with `signed-by=`, and no longer calls apt-key or any keyserver (comments ignored);
#   3. that committed keyring verifies the real, Launchpad-signed `InRelease` fixture
#      completely - both signers - the same gpgv decision apt's own gpgv method makes when
#      it prints "The repository ... is not signed", and the fixture's documented sha256
#      and origin still match the file;
#   4. red controls prove the checks can fail: a script with the legacy apt-key/keyserver
#      lines restored is rejected, one pinning a keyring that is not committed is
#      rejected, a keyring holding only the legacy key does not verify the fixture, and a
#      tampered fixture is a bad signature.
#
# Deterministic: no wall-clock timing, no sleeps, no network. Inputs are committed files;
# the only seams are the script text (comments stripped) and gpgv, whose verdict is the
# assertion.
#
# Exit codes: 0 = pass, 1 = regression, 2 = setup/harness error, 77 = gpgv unavailable.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
SETUP="$ROOT/.github/workflows/setup-mingw-packages.sh"
BUILD_YML="$ROOT/.github/workflows/build.yml"
MINGW_DEPS="$ROOT/.github/workflows/deps-ubuntu-24.04-mingw.txt"
FIXTURE_DIR="$ROOT/tests/data/mingw-ppa"
FIXTURE="$FIXTURE_DIR/InRelease"
LEGACY_KEYRING="$FIXTURE_DIR/legacy-only.gpg"
NEW_FPR="AD1A987F6AC01FA7B66FB4EB9C0C72C11E0AF891"
LEGACY_FPR="72931B477E22FEFD47F8DECE02FE5F12ADDE29B2"
FIXTURE_SHA256="1d56a54d028396cb1fc79d683fe2c8812e3db6c2e1f1d7f1e02df89c90caf837"

FAILED=0
ok() { printf '  PASS %s\n' "$*"; }
bad() { printf '  FAIL %s\n' "$*"; FAILED=1; }

command -v gpgv >/dev/null 2>&1 || { echo "mingw-ppa-key-check: gpgv not found - SKIP" >&2; exit 77; }
HAVE_GPG=0
command -v gpg >/dev/null 2>&1 && HAVE_GPG=1
for f in "$SETUP" "$BUILD_YML" "$MINGW_DEPS" "$FIXTURE" "$LEGACY_KEYRING"; do
	[[ -f "$f" ]] || { echo "mingw-ppa-key-check: missing $f" >&2; exit 2; }
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
export GNUPGHOME="$TMP/gnupg"
mkdir -m 700 "$GNUPGHOME"

# The script's executable lines only: a comment may (and does) explain the retired
# apt-key/keyserver line by name, which must not count as using it.
code_of() { sed -E '/^[[:space:]]*#/d' "$1" | sed -E 's/[[:space:]]+#.*$//'; }

# The key file the script installs, as a repo-relative path under .github/workflows/.
key_of() { code_of "$1" | grep -oE 'keys/[A-Za-z0-9._-]+\.gpg' | head -1; }

# ---------------------------------------------------------------------------
# The checker. Applied to the product script (must pass) and to weakened copies
# (must fail), so a green run is a decision, not a no-op.
# ---------------------------------------------------------------------------
check_script() { # <script> <repo root> -> 0 = the script pins the PPA to a committed key
	local script="$1" root="$2" code src_line keyring key_base signed_by rc=0
	code="$(code_of "$script")"

	if grep -qE 'apt-key' <<<"$code"; then
		bad "the install step still calls apt-key (deprecated; it reads a run-time keyring)"
		rc=1
	else
		ok "no apt-key call in the install step"
	fi
	if grep -qE -- '--keyserver|keyserver\.|hkps?://|sks-keyservers' <<<"$code"; then
		bad "the install step still fetches the key from a network keyserver"
		rc=1
	else
		ok "no network keyserver fetch in the install step"
	fi

	src_line="$(grep 'tobydox/mingw-w64/ubuntu' <<<"$code" | head -1)"
	if [[ -z "$src_line" ]]; then
		bad "no apt source line for the MinGW PPA in $script"
		return 1
	fi
	signed_by="$(sed -n 's/.*signed-by=\([^] ]*\).*/\1/p' <<<"$src_line")"
	if [[ -z "$signed_by" ]]; then
		bad "the PPA source line has no signed-by= keyring: $src_line"
		rc=1
	else
		ok "the PPA source line is pinned with signed-by=$signed_by"
	fi

	# The keyring the script installs must be a committed file named as keys/<file>.
	keyring="$(key_of "$script")"
	if [[ -z "$keyring" ]]; then
		bad "the install step names no committed keyring under .github/workflows/keys/"
		return 1
	fi
	if [[ ! -s "$root/.github/workflows/$keyring" ]]; then
		bad "the keyring the script installs ($keyring) is not committed"
		return 1
	fi
	ok "the install step uses the committed keyring $keyring"
	if [[ -n "$signed_by" ]]; then
		key_base="$(basename "$keyring")"
		if [[ "$(basename "$signed_by")" != "$key_base" ]]; then
			bad "signed-by=$signed_by does not name the installed keyring $key_base"
			rc=1
		else
			ok "signed-by names the installed keyring ($key_base)"
		fi
	fi
	if sh -n "$script" 2>"$TMP/sh.err"; then
		ok "the install step is valid POSIX sh"
	else
		bad "the install step is not valid POSIX sh: $(cat "$TMP/sh.err")"
		rc=1
	fi
	return $rc
}

echo "=== Part 1: the live install path this test covers ==="
if grep -qE '^[[:space:]]+sudo \.github/workflows/setup-mingw-packages\.sh' "$BUILD_YML"; then
	ok "build.yml still installs the MinGW dependencies through the script"
else
	bad "build.yml no longer runs .github/workflows/setup-mingw-packages.sh"
fi
for pkg in qt5base-mingw-w64 qt5svg-mingw-w64 libgig-mingw-w64; do
	if grep -qx "$pkg" "$MINGW_DEPS"; then
		ok "the dependency list still installs $pkg from the PPA"
	else
		bad "the dependency list no longer names $pkg"
	fi
done

echo
echo "=== Part 2: the product script installs a committed key, no keyserver ==="
check_script "$SETUP" "$ROOT"

echo
echo "=== Part 3: the committed keyring verifies the real PPA release file ==="
actual_sha="$(sha256sum "$FIXTURE" | cut -d' ' -f1)"
if [[ "$actual_sha" == "$FIXTURE_SHA256" ]]; then
	ok "the InRelease fixture is the file whose sha256 tests/data/mingw-ppa/README.md records"
else
	bad "the InRelease fixture's sha256 is $actual_sha, not the documented $FIXTURE_SHA256"
fi
if grep -qx 'Origin: LP-PPA-tobydox-mingw-w64' "$FIXTURE" && grep -qx 'Suite: focal' "$FIXTURE"; then
	ok "the fixture is the PPA's focal InRelease for LP-PPA-tobydox-mingw-w64"
else
	bad "the fixture does not look like the PPA's focal InRelease (Origin/Suite headers)"
fi
if grep -qE '^ [0-9a-f]{32} +[0-9]+ main/binary-amd64/Packages$' "$FIXTURE"; then
	ok "the fixture carries the amd64 Packages index the mingw64 job installs from"
else
	bad "the fixture has no amd64 Packages index"
fi

KEY_REL="$(key_of "$SETUP")"
if [[ -z "$KEY_REL" ]]; then
	bad "no committed keyring named by the script, so the PPA file cannot be verified"
	KEYRING=""
else
	KEYRING="$ROOT/.github/workflows/$KEY_REL"
	if gpgv --keyring "$KEYRING" "$FIXTURE" >"$TMP/gpgv.out" 2>&1; then
		ok "gpgv accepts the Launchpad-signed InRelease with the committed keyring"
	else
		bad "gpgv rejects the InRelease fixture with the committed keyring:"
		sed 's/^/        /' "$TMP/gpgv.out"
	fi
	for fp in "$NEW_FPR" "$LEGACY_FPR"; do
		if grep -q "using RSA key $fp" "$TMP/gpgv.out"; then
			ok "the fixture signature by $fp verifies (key is in the committed keyring)"
		else
			bad "the fixture signature by $fp is not verifiable with the committed keyring"
		fi
	done
	if [[ $HAVE_GPG -eq 1 ]]; then
		if gpg --batch --no-default-keyring --keyring "$KEYRING" --list-packets 2>/dev/null \
			| grep -qE '^:(secret key|secret sub key) packet:'; then
			bad "the committed keyring contains secret key material"
		else
			ok "the committed keyring holds public keys only"
		fi
		if gpg --batch --no-default-keyring --keyring "$KEYRING" --with-colons \
			--import-options show-only --import "$KEYRING" 2>/dev/null \
			| grep -q "^fpr:::::::::$NEW_FPR:"; then
			ok "the committed keyring holds the current signing key $NEW_FPR"
		else
			bad "the committed keyring does not hold the current signing key $NEW_FPR"
		fi
	else
		echo "  NOTE gpg not installed; the keyring-content checks above are covered by gpgv"
	fi
fi

echo
echo "=== Part 4: red controls (the checks can fail) ==="
# Control A: the original run-time apt-key/keyserver install, byte for byte, must be
# rejected - and the rejection must name both things it does.
cat >"$TMP/legacy.sh" <<'EOF'
#!/bin/sh
sh -c 'echo "deb http://ppa.launchpad.net/tobydox/mingw-w64/ubuntu focal main" > \
  /etc/apt/sources.list.d/tobydox-mingw-w64.list'
apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 72931B477E22FEFD47F8DECE02FE5F12ADDE29B2
apt-get update -y
EOF
# In a subshell: its deliberate bad() lines set FAILED, which must not leak into this run.
if ( check_script "$TMP/legacy.sh" "$ROOT" ) >"$TMP/legacy.out" 2>&1; then
	bad "control A: the legacy apt-key/keyserver script was accepted"
elif grep -q 'still calls apt-key' "$TMP/legacy.out" && grep -q 'network keyserver' "$TMP/legacy.out"; then
	ok "control A: the legacy apt-key/keyserver script is rejected for both reasons"
else
	bad "control A: legacy script rejected, but not for the apt-key/keyserver reason"
	sed 's/^/        /' "$TMP/legacy.out"
fi

# Control B: a keyring named by the script but not committed must be rejected.
sed 's#keys/tobydox-mingw-w64\.gpg#keys/not-committed.gpg#g' "$SETUP" >"$TMP/nokey.sh"
if ( check_script "$TMP/nokey.sh" "$ROOT" ) >"$TMP/nokey.out" 2>&1; then
	bad "control B: a script naming an uncommitted keyring was accepted"
else
	ok "control B: a script naming an uncommitted keyring is rejected"
fi

# Control C: a keyring holding only the legacy key does not verify the release file.
if gpgv --keyring "$LEGACY_KEYRING" "$FIXTURE" >"$TMP/legacy-key.out" 2>&1; then
	bad "control C: a legacy-key-only keyring was accepted - the completeness check is void"
else
	ok "control C: a legacy-key-only keyring is rejected (both signers are required)"
fi

# Control D: a tampered release file is a bad signature, not a pass.
if [[ -n "$KEYRING" ]]; then
	sed '0,/^Origin: /s//Origin: not-the-ppa/' "$FIXTURE" >"$TMP/tampered"
	if gpgv --keyring "$KEYRING" "$TMP/tampered" >"$TMP/tampered.out" 2>&1; then
		bad "control D: a tampered InRelease verified - signature checking is void"
	else
		ok "control D: a tampered InRelease is rejected"
	fi
else
	bad "control D: no keyring to test tamper rejection against"
fi

echo
if [[ $FAILED -eq 0 ]]; then
	echo "PASS: the MinGW PPA key is committed, pinned with signed-by, and verifies the signed release file"
	exit 0
fi
echo "FAIL: see the failing lines above"
exit 1
