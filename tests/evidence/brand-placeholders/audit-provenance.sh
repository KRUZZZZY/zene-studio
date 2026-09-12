#!/bin/bash
# Provenance audit: for each candidate identity image in HEAD, compare bytes to origin/master
# (a) at the same path, (b) at the pre-rename upstream path if git records a rename.
cd "$(dirname "$0")/.." || exit 1
IMGRE='\.(svg|png|ico|icns|xpm|jpg)$'

echo "== A. Rename map from origin/master -> HEAD (images only) =="
git diff --summary origin/master..HEAD | grep -E '^ rename ' | grep -iE "$IMGRE" | sort

echo
echo "== B. Identity-artwork candidates: byte-compare to origin/master =="
printf '%-72s %-10s %-8s %-9s %s\n' PATH HEAD_BYTES MD5_HEAD SAME_PATH RENAME_SRC
CAND=$(git ls-files | grep -iE "$IMGRE" | grep -E '^(data/themes/default|data/backgrounds|cmake/linux/icons|cmake/nsis|cmake/apple)/')
for f in $CAND; do
  sz=$(stat -c%s "$f")
  md5=$(md5sum "$f" | cut -c1-8)
  same="n/a"
  if git cat-file -e "origin/master:$f" 2>/dev/null; then
    if git show "origin/master:$f" | cmp -s - "$f"; then same="IDENTICAL"; else same="differs"; fi
  fi
  # find rename source
  src=$(git diff --summary -M origin/master..HEAD -- "$f" | grep -oE '\{[^}]*=> *[^}]*\}' >/dev/null 2>&1; git diff --find-renames=50% --name-status -M origin/master..HEAD -- "$f" | awk '$1 ~ /^R/ {print $2}')
  if [ -n "$src" ]; then
    if git show "origin/master:$src" | cmp -s - "$f"; then r="IDENTICAL_TO_UPSTREAM"; else r="differs_from($src)"; fi
  else r="-"; fi
  printf '%-72s %-10s %-8s %-9s %s\n' "$f" "$sz" "$md5" "$same" "$r"
done
