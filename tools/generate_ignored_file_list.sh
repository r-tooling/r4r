#!/usr/bin/env bash
set -euo pipefail

# Use no-break space delimiter
DELIM=$'\u00A0'

# An array of blacklisted files
BLACKLIST_FILES=(
	"/dev/*"
	"/proc/*"
	"/sys/*"
)

## code

if [ $# -ne 1 ]; then
	echo "Usage: $0 <output_file>" >&2
	exit 1
fi

OUTPUT="$1"

is_blacklisted() {
	local file_path="$1"
	for pattern in "${BLACKLIST_FILES[@]}"; do
		# shellcheck disable=SC2053
		if [[ "$file_path" == $pattern ]]; then
			return 0
		fi
	done
	return 1
}

cat >"$OUTPUT" <<EOF
#include "../file_info.hpp"
#include "../filesystem_trie.hpp"
#include <filesystem>

// Convert a numeric mode (e.g. 755) to std::filesystem::perms bitmask.
static std::filesystem::perms to_fs_perms_func(unsigned mode_value) {
    using std::filesystem::perms;
    perms p = perms::none;
    if (mode_value & 0400) p |= perms::owner_read;
    if (mode_value & 0200) p |= perms::owner_write;
    if (mode_value & 0100) p |= perms::owner_exec;
    if (mode_value & 0040) p |= perms::group_read;
    if (mode_value & 0020) p |= perms::group_write;
    if (mode_value & 0010) p |= perms::group_exec;
    if (mode_value & 0004) p |= perms::others_read;
    if (mode_value & 0002) p |= perms::others_write;
    if (mode_value & 0001) p |= perms::others_exec;
    return p;
}

FileSystemTrie<FileInfo> default_file_list() {
    FileSystemTrie<FileInfo> trie{};
EOF

script="
  DELIM='$DELIM'
  BF_PATTERN='$(
	IFS="|"
	echo "${BLACKLIST_FILES[*]}"
)'
"

# shellcheck disable=SC2016,SC2089
# it is tempting to add \( -perm -o=r -o -perm -o=w -o -perm -o=x -o -perm -g=r -o -perm -g=w -o -perm -g=x \)
# but the final file reduction is minimal
script+='
  find / -type f 2>/dev/null | grep -vE "$BF_PATTERN" | while IFS= read -r file; do
    # we do not want to fail the entire script if a command fails.
    # so we capture errors and continue.
    sha1="$((sha1sum "$file" 2>/dev/null | cut -d " " -f1) || echo "error")"
    stat="$(stat -c "%U${DELIM}%G${DELIM}%s${DELIM}%a" "$file" 2>/dev/null || echo "error")"

    echo "$file${DELIM}${stat}${DELIM}${sha1}"
  done
'

files=0
docker run --rm ubuntu:22.04 bash -c "$script" | while IFS= read -r line; do
	IFS="$DELIM" read -r path user group size numeric_perms sha1 <<<"$line"

	if [[ "$user" == "error" ]]; then
		echo "WARNING: Could not stat '$path'" >&2
		continue
	fi
	if [[ "$sha1" == "error" ]]; then
		echo "WARNING: Could not sha1sum '$path'" >&2
		continue
	fi

	cat >>"$OUTPUT" <<EOF
	   trie.insert("$path", {
	         "$user",
	         "$group",
	         to_fs_perms_func(${numeric_perms}),
	         static_cast<std::uintmax_t>(${size}),
	         "$sha1"
	     });
EOF

	printf "\r%s\033[K" "Loaded ($files): $path"
	files=$((files + 1))
done

cat >>"$OUTPUT" <<EOF
    return trie;
}
EOF
