#!/usr/bin/env bash
set -uo pipefail

if [[ $# != 4 ]]; then
    printf 'Usage: %s <candidate ELF> <frozen R28 ELF> <P0A28 ELF> <output directory>\n' "$0" >&2
    exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P) || exit 2
candidate=$(realpath -e -- "$1") || exit 2
r28=$(realpath -e -- "$2") || exit 2
p0a28=$(realpath -e -- "$3") || exit 2
mkdir -p -- "$4" || exit 2
output=$(realpath -e -- "$4") || exit 2
statuses="$output/runtime-status.log"
: > "$statuses" || exit 2

for file in "$candidate" "$r28" "$p0a28"; do
    if [[ ! -x "$file" ]]; then
        printf 'Executable is missing or lacks execute permission: %s\n' "$file" | tee -a "$statuses" >&2
        exit 2
    fi
done
for file in phase_a_runtime.py compatibility_runtime.py; do
    if [[ ! -r "$script_dir/$file" ]]; then
        printf 'Co-located runtime script is missing: %s\n' "$script_dir/$file" | tee -a "$statuses" >&2
        exit 2
    fi
done

scratch=$(mktemp -d "${RUNNER_TEMP:-/tmp}/revolution-runtime.XXXXXXXX") || exit 2
trap 'rm -rf -- "$scratch"' EXIT
mkdir -p -- "$scratch/vendor" "$scratch/verified-fixtures" "$scratch/syzygy" || exit 2

python3 -m pip install --no-cache-dir --disable-pip-version-check --target "$scratch/vendor" 'chess==1.11.2' \
    2>&1 | tee "$output/runtime-pip.log"
pip_status=("${PIPESTATUS[@]}")
printf 'PIP_RC=%s PIP_TEE_RC=%s\n' "${pip_status[@]}" | tee -a "$statuses"
if [[ ${pip_status[0]} != 0 || ${pip_status[1]} != 0 ]]; then
    printf 'BLACK_BOX_RUNTIME=NOT_RUN COMPATIBILITY_RUNTIME=NOT_RUN REASON=PYTHON_CHESS_INSTALL_FAILED\n' | tee -a "$statuses"
    exit 1
fi
export PYTHONPATH="$scratch/vendor${PYTHONPATH:+:$PYTHONPATH}"
python3 -c 'import chess; print(f"PYTHON_CHESS={chess.__version__}"); assert chess.__version__ == "1.11.2"' \
    | tee -a "$statuses" || exit 2

python3 "$script_dir/phase_a_runtime.py" \
    --candidate "$candidate" --r28 "$r28" --p0a28 "$p0a28" --output "$output/black-box" \
    2>&1 | tee "$output/black-box.log"
blackbox_status=("${PIPESTATUS[@]}")
printf 'BLACK_BOX_RC=%s BLACK_BOX_TEE_RC=%s\n' "${blackbox_status[@]}" | tee -a "$statuses"

fetch_checked() {
    local base=$1 directory=$2 filename=$3 expected=$4
    local destination="$directory/$filename"
    printf 'FETCH %s/%s\n' "$base" "$filename"
    curl --proto '=https' --proto-redir '=https' --fail --location --silent --show-error \
        --retry 2 --connect-timeout 20 --max-time 90 --output "$destination.part" "$base/$filename" || return 1
    printf '%s  %s\n' "$expected" "$destination.part" | sha256sum --strict --check || return 1
    mv -- "$destination.part" "$destination"
}

fetch_fixtures() {
    local ctg_base='https://raw.githubusercontent.com/sshivaji/ctgexporter/aed420ba6f57e063603661d227568aa690948fba/examples'
    local syzygy_base='https://raw.githubusercontent.com/niklasf/python-chess/b4493c88b243157abd425362ab8f23fbb1a1c311/data/syzygy/regular'
    local file sha
    local ctg_rc=0 syzygy_rc=0
    while read -r sha file; do
        fetch_checked "$ctg_base" "$scratch/verified-fixtures" "$file" "$sha" || ctg_rc=1
    done <<'HASHES'
9fade80e38c0f533247095afcd0adc38bbee2b480af659d2b1c8c2e9a58b7768 simple.ctg
59baf5a04762db5ddc30795b9e54b21d425d7b4f9665306fe553c5964b0fb8dc simple.ctb
edd0c1427274d066d08649452ffb22f2c210e80fc57907e7f0fd31293571df3b simple.cto
HASHES
    while read -r sha file; do
        fetch_checked "$syzygy_base" "$scratch/syzygy" "$file" "$sha" || syzygy_rc=1
    done <<'HASHES'
63ad9e15cd0f5e91e42e6f669a9f6116ae9f9eabd85757e9fb28d2be6074aed9 KPvK.rtbw
f2469f063c9b5748b7b8a0e33d65e41be2090c397b4bcbedbeedbc5ff301b596 KPvK.rtbz
b018ece9cc195618981b027268f9cca929a1ed4c0bdd739874b7846640ef479a KQRvK.rtbw
9766d3e684582297943583e1981154609655034cea2aa78a5494cc01c8c6bcee KQRvK.rtbz
45c453e5113a714bd4ece1cb5ba78cf5d21dd8b06f89708675d164fbe9bd3b53 KQvK.rtbw
ad20819e947f38bf06865a888dbde488ee4a34d2aa64b5727e1882313b1273a3 KQvK.rtbz
4c05ec32e6e43b690d235307de75432a3ceecdbc3d2659d8101ed15b48b8445d KRRvK.rtbw
e68314e00bf9f16122005a06f7d42bc0f6c682f30e8ca50532ba44444f3b96ce KRRvK.rtbz
37f8601644113dc83be9822913d6ecf75e5c8b7eeb11b7689c38a3ff986d2a48 KRvK.rtbw
9ce83c0f6204fcca761c4203a4644ad2993462749fa39ac27815a29dee740b20 KRvK.rtbz
HASHES
    printf 'CTG_FIXTURE_RC=%s SYZYGY_FIXTURE_RC=%s\n' "$ctg_rc" "$syzygy_rc" | tee -a "$statuses"
    if (( syzygy_rc == 0 )); then
        : > "$scratch/syzygy-verified"
    fi
    if (( ctg_rc == 0 )); then
        : > "$scratch/ctg-verified"
    fi
    (( ctg_rc == 0 && syzygy_rc == 0 ))
}

fetch_fixtures 2>&1 | tee "$output/runtime-fixtures.log"
fixture_status=("${PIPESTATUS[@]}")
printf 'FIXTURE_RC=%s FIXTURE_TEE_RC=%s\n' "${fixture_status[@]}" | tee -a "$statuses"
printf 'SYZYGY_1_PIECE=NOT_APPLICABLE SYZYGY_5_6_PIECES=NOT_RUN REASON=NO_VERIFIED_5_6_PIECE_FIXTURE_STAGED\n' | tee -a "$statuses"

run_compatibility() {
    local label=$1 engine=$2 fixture_dir="$scratch/$1-fixtures" isolated_cwd="$scratch/$1-cwd"
    local -a tablebase_args=()
    mkdir -p -- "$fixture_dir" "$isolated_cwd" || return 2
    if [[ -f "$scratch/ctg-verified" ]]; then
        cp -- "$scratch/verified-fixtures/"simple.ct{g,b,o} "$fixture_dir/" || return 2
    else
        printf 'CTG_FIXTURE_AVAILABLE=NO REASON=CTG_FIXTURE_VERIFICATION_FAILED\n'
    fi
    if [[ -f "$scratch/syzygy-verified" ]]; then
        tablebase_args=(--tablebase-dir "$scratch/syzygy")
    else
        printf 'SYZYGY_RUNTIME=NOT_RUN REASON=SYZYGY_FIXTURE_VERIFICATION_FAILED\n'
    fi
    printf 'COMPATIBILITY_TEST_EXECUTABLE=%s LABEL=%s\n' "$engine" "$label"
    python3 "$script_dir/compatibility_runtime.py" --fixture-dir "$fixture_dir" \
        --workspace "$isolated_cwd" --engine "$engine" "${tablebase_args[@]}"
}

run_compatibility candidate "$candidate" 2>&1 | tee "$output/compatibility-candidate.log"
candidate_compat=("${PIPESTATUS[@]}")
printf 'CANDIDATE_COMPAT_RC=%s CANDIDATE_COMPAT_TEE_RC=%s\n' "${candidate_compat[@]}" | tee -a "$statuses"
run_compatibility r28 "$r28" 2>&1 | tee "$output/compatibility-r28.log"
r28_compat=("${PIPESTATUS[@]}")
printf 'R28_COMPAT_RC=%s R28_COMPAT_TEE_RC=%s\n' "${r28_compat[@]}" | tee -a "$statuses"

for rc in "${blackbox_status[@]}" "${fixture_status[@]}" "${candidate_compat[@]}" "${r28_compat[@]}"; do
    if (( rc != 0 )); then
        exit 1
    fi
done
