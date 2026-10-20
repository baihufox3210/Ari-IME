#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/.." && pwd)
binary=${1:-ari-ime-dict}
corpus=${2:-"$repo_dir/docs/candidate-benchmark.tsv"}

if [[ "$binary" != */* ]]; then
    binary_path=$(command -v -- "$binary" || true)
else
    binary_path=$binary
fi
if [[ -z "$binary_path" || ! -x "$binary_path" ]]; then
    printf 'candidate-benchmark: executable not found: %s\n' "$binary" >&2
    exit 2
fi
if [[ ! -r "$corpus" ]]; then
    printf 'candidate-benchmark: corpus is not readable: %s\n' "$corpus" >&2
    exit 2
fi

temporary_dir=$(mktemp -d)
trap 'rm -rf -- "$temporary_dir"' EXIT
export ARI_IME_USER_DATA_DIR="$temporary_dir/user-data"
export ARI_IME_DISABLE_AUTOLEARN=1

printf 'binary\t%s\n' "$binary_path"
if ! info_output=$("$binary_path" info 2>"$temporary_dir/info.stderr"); then
    cat "$temporary_dir/info.stderr" >&2
    exit 1
fi
filter_expected_diagnostics() {
    awk '$0 != "[ERROR chewing::dictionary::loader] Dictionary file not found: chewing.dat"' "$1" >&2
}
filter_expected_diagnostics "$temporary_dir/info.stderr"
printf '%s\n' "$info_output" | awk -F '\t' '$1 == "libchewing" { print "libchewing\t" $2 }'
printf 'keys\ttarget\tresult\trank\n'

failures=0
while IFS=$'\t' read -r keys target; do
    [[ -z "$keys" || "$keys" == \#* ]] && continue
    output=$("$binary_path" candidates "$keys" 2>"$temporary_dir/candidates.stderr") || {
        cat "$temporary_dir/candidates.stderr" >&2
        printf '%s\t%s\tERROR\t-\n' "$keys" "$target"
        failures=$((failures + 1))
        continue
    }
    filter_expected_diagnostics "$temporary_dir/candidates.stderr"
    rank=$(printf '%s\n' "$output" | awk -F '\t' -v target="$target" '
        $1 == "candidate" { rank_count++; if ($2 == target) { print rank_count; exit } }
    ')
    if [[ -n "$rank" ]]; then
        printf '%s\t%s\tPASS\t%s\n' "$keys" "$target" "$rank"
    else
        printf '%s\t%s\tMISS\t-\n' "$keys" "$target"
        failures=$((failures + 1))
    fi
done < "$corpus"

if (( failures > 0 )); then
    printf 'candidate-benchmark: %d target(s) missing from the first page\n' "$failures" >&2
    exit 1
fi
