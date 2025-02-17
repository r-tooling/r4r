#!/usr/bin/env bash
set -euo pipefail

###############################################################################
# Usage
###############################################################################
usage() {
	cat <<EOF
Usage: $0 [OPTIONS] <expected_dir> <actual_dir>

Options:
  --diff-tool <TOOL>   Use a custom diff command/tool (e.g. "diff -u").
                       Default: "git diff --no-index --color=auto"

  --exclude <glob>     Exclude files matching the given glob pattern.
                       Can be specified multiple times to exclude multiple patterns.
                       Example: --exclude "*.tmp" --exclude "tests/temp/*"

  --fail               Non-interactive mode: automatically fail on any discrepancy.

Examples:
  $0 --diff-tool "diff -u" snapshots_expected snapshots_actual
  $0 --exclude "*.tmp" --exclude "cache/*" snapshots_expected snapshots_actual
EOF
	exit 1
}

###############################################################################
# Parse Arguments
###############################################################################
DIFF_TOOL="git diff --no-index --color=auto" # default diff tool
EXCLUDES=()
EXPECTED_DIR=""
ACTUAL_DIR=""
NONINTERACTIVE_FAIL=false

while [[ $# -gt 0 ]]; do
	case "$1" in
	--diff-tool)
		shift
		[[ $# -lt 1 ]] && {
			echo "Error: Missing argument for --diff-tool"
			usage
		}
		DIFF_TOOL="$1"
		shift
		;;
	--exclude)
		shift
		[[ $# -lt 1 ]] && {
			echo "Error: Missing argument for --exclude"
			usage
		}
		EXCLUDES+=("$1")
		shift
		;;
	--fail)
		# Non-interactive mode that always fails on discrepancies
		NONINTERACTIVE_FAIL=true
		shift
		;;
	-h | --help)
		usage
		;;
	*)
		# Positional arguments: expected_dir and actual_dir
		if [[ -z "$EXPECTED_DIR" ]]; then
			EXPECTED_DIR="$1"
		elif [[ -z "$ACTUAL_DIR" ]]; then
			ACTUAL_DIR="$1"
		else
			echo "Unrecognized argument: $1"
			usage
		fi
		shift
		;;
	esac
done

# Validate mandatory directories
[[ -z "$EXPECTED_DIR" || -z "$ACTUAL_DIR" ]] && usage
[[ ! -d "$EXPECTED_DIR" ]] && {
	echo "Error: '$EXPECTED_DIR' is not a directory."
	exit 1
}
[[ ! -d "$ACTUAL_DIR" ]] && {
	echo "Error: '$ACTUAL_DIR' is not a directory."
	exit 1
}

###############################################################################
# Gather Files
###############################################################################
mapfile -t expected_files < <(cd "$EXPECTED_DIR" && find . -type f | sort)
mapfile -t actual_files < <(cd "$ACTUAL_DIR" && find . -type f | sort)
ALL_FILES=($(printf '%s\n' "${expected_files[@]}" "${actual_files[@]}" | sort -u))

###############################################################################
# Counters
###############################################################################
checked=0
ok=0
failed=0
ignored=0
updated=0

###############################################################################
# Helper Functions
###############################################################################
are_files_same() {
	# Return 0 if identical, else 1
	diff -q "$1" "$2" >/dev/null 2>&1
}

show_diff() {
	# Show a diff using the user-specified DIFF_TOOL.
	local f1="$1"
	local f2="$2"
	[[ ! -f "$f1" ]] && f1="/dev/null"
	[[ ! -f "$f2" ]] && f2="/dev/null"

	# shellcheck disable=SC2086  # we want word splitting for DIFF_TOOL
	$DIFF_TOOL "$f1" "$f2" || true
}

prompt_action() {
	local status="$1"
	while true; do
		case "$status" in
		"missing in actual")
			read -r -p "  [i]gnore / [f]ail / [u]pdate ? " choice
			case "$choice" in
			i | I)
				echo "ignore"
				return
				;;
			f | F)
				echo "fail"
				return
				;;
			u | U)
				echo "update-actual"
				return
				;;
			*) echo "Invalid choice, try again." ;;
			esac
			;;
		"missing in expected" | "different")
			read -r -p "  [i]gnore / [f]ail / [u]pdate ? " choice
			case "$choice" in
			i | I)
				echo "ignore"
				return
				;;
			f | F)
				echo "fail"
				return
				;;
			u | U)
				echo "update-expected"
				return
				;;
			*) echo "Invalid choice, try again." ;;
			esac
			;;
		*)
			# Should never reach here unless status is "OK"
			echo "ignore"
			return
			;;
		esac
	done
}

###############################################################################
# Main Comparison Loop
###############################################################################
checked="${#ALL_FILES[@]}"

for file in "${ALL_FILES[@]}"; do
	excluded=false
	for pattern in "${EXCLUDES[@]}"; do
		if [[ "$file" == $pattern ]]; then
			excluded=true
			break
		fi
	done

	if $excluded; then
		echo "Checking file: $file .... excluded"
		continue
	fi

	local_act="$ACTUAL_DIR/$file"
	local_exp="$EXPECTED_DIR/$file"

	# Determine status
	if [[ ! -f "$local_act" && ! -f "$local_exp" ]]; then
		# Edge case: not found in either (shouldn't happen with union).
		# We won't increment counters here.
		continue
	elif [[ ! -f "$local_act" ]]; then
		status="missing in actual"
	elif [[ ! -f "$local_exp" ]]; then
		status="missing in expected"
	else
		# Both exist => check if same
		if are_files_same "$local_act" "$local_exp"; then
			status="OK"
		else
			status="different"
		fi
	fi

	echo "Checking file: $file .... $status"

	if [[ "$status" == "OK" ]]; then
		((ok += 1))
		continue
	fi

	# For non-OK files, show diff and prompt user
	show_diff "$local_act" "$local_exp"

	if $NONINTERACTIVE_FAIL; then
		((failed += 1))
		continue
	fi

	action="$(prompt_action "$status")"

	case "$action" in
	ignore)
		((ignored += 1))
		;;
	fail)
		((failed += 1))
		;;
	update-actual)
		mkdir -p "$(dirname "$local_act")"
		cp "$local_exp" "$local_act"
		((updated += 1))
		;;
	update-expected)
		mkdir -p "$(dirname "$local_exp")"
		cp "$local_act" "$local_exp"
		((updated += 1))
		;;
	esac
done

###############################################################################
# Summary
###############################################################################
echo
echo "Checked $checked files ($ok ok, $failed failed, $ignored ignored, $updated updated)"
if [[ $failed -gt 0 ]]; then
	exit 1
else
	exit 0
fi
