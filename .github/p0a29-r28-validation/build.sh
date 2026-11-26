#!/usr/bin/env bash
set -euo pipefail

logs="$RUNNER_TEMP/p0a29-r28-evidence"
work="$RUNNER_TEMP/P0A29 R28 validation"
staged="$RUNNER_TEMP/p0a29-r28-executables"
net_name=nn-d1ee674a62ca.nnue
net_sha=d1ee674a62ca754648fb8c2907ff4e92f0c3acff28f59afa0f5bd4e3949538f1
net_bytes=136247652
exe=Revolution-6.40-150926-x86-64-universal
candidate=019a626da232febb59304525d004056ae37ff9a3
candidate_tree=b3e90c1afff6f06ddbe87d5c93e50a31033cb45d
r28=fa46b03399456538bacb20633feb4687114089bf
r28_tree=90eb6b17aa8884ef70411c7cda90eb0c0999b556
p0a28=fe2ec61b6070bcbb1f465286be0f2bb060c807ce
p0a28_tree=3605abae2d5217e7a93c7428c32638a6750d6aaf

mkdir -p "$logs" "$work/net" "$work/embed probe" "$staged"
exec > >(tee "$logs/driver.log") 2>&1

test "$(git rev-parse HEAD^)" = "$candidate"
test "$(git rev-parse "${candidate}^{tree}")" = "$candidate_tree"
test "$(git ls-remote origin refs/heads/main | cut -f1)" = "$r28"
clang++ --version
ld.lld --version
llvm-objcopy --version
make --version
uname -a
lscpu

probe="$work/embed probe"
printf '\000\177\200\372\377' > "$probe/bytes.bin"
cat > "$probe/probe.c" <<'EOF'
#include <stdio.h>
static const unsigned char bytes[] = {
#embed "bytes.bin"
};
int main(void) { return fwrite(bytes, 1, sizeof(bytes), stdout) != sizeof(bytes); }
EOF
clang -std=c23 -Wall -Wextra -Werror -fuse-ld=lld "$probe/probe.c" -o "$probe/probe"
llvm-objcopy --strip-debug "$probe/probe"
"$probe/probe" > "$probe/actual.bin"
cmp "$probe/bytes.bin" "$probe/actual.bin"
od -An -tx1 "$probe/actual.bin" | tee "$logs/embed-probe.log"

net="$work/net/$net_name"
verify_net() {
    test "$(wc -c < "$1")" -eq "$net_bytes" &&
        printf '%s  %s\n' "$net_sha" "$1" | sha256sum -c -
}
fetch_net() {
    local url="$1"
    rm -f "$net.partial"
    curl --fail --show-error --location --retry 1 --connect-timeout 20 --max-time 300 "$url" -o "$net.partial" &&
        verify_net "$net.partial"
}
if ! fetch_net "https://tests.stockfishchess.org/api/nn/$net_name"; then
    fetch_net "https://media.githubusercontent.com/media/official-stockfish/networks/master/$net_name"
fi
mv "$net.partial" "$net"
verify_net "$net" | tee "$logs/nnue.log"

build_exact() {
    local label="$1" head="$2" expected_tree="$3"
    local checkout="$work/$label" report="$logs/$label"
    local -a codes
    mkdir -p "$report" "$staged/$label"
    git worktree add --detach "$checkout" "$head"
    (
        cd "$checkout"
        test "$(git rev-parse HEAD)" = "$head"
        test "$(git rev-parse 'HEAD^{tree}')" = "$expected_tree"
        test -z "$(git status --porcelain=v1)"
        printf 'HEAD=%s\nTREE=%s\n' "$head" "$expected_tree" | tee "$report/identity.log"
        grep -Fx "#define EvalFileDefaultName \"$net_name\"" src/evaluate.h
        if [[ "$label" == candidate ]]; then
            grep -Fx "#define EvalFileDefaultName \"$net_name\"" src/nnue/evaluate.h
        fi
        make -C src clean 2>&1 | tee "$report/clean-universal.log"
        ln "$net" "src/$net_name"
        set +e
        make -C src -j2 build ARCH=x86-64-universal COMP=clang COMPCXX=clang++ OBJCOPY=llvm-objcopy EXTRALDFLAGS='-fuse-ld=lld' 2>&1 | tee "$report/build-universal.log"
        codes=("${PIPESTATUS[@]}")
        set -e
        printf 'BUILD_RC=%s TEE_RC=%s\n' "${codes[0]}" "${codes[1]}" | tee "$report/build-status.log"
        test "${codes[0]}" -eq 0
        test "${codes[1]}" -eq 0
        if grep -Ein 'warning:|error:|fatal error:|undefined reference|undefined symbol|duplicate symbol|make: \*\*\*' "$report/clean-universal.log" "$report/build-universal.log"; then
            exit 1
        fi
        test -x "src/$exe"
        {
            file "src/$exe"
            readelf -h "src/$exe"
            sha256sum "src/$exe"
            wc -c "src/$exe"
            python3 - "src/$exe" "$net" "$net_sha" <<'PY'
import hashlib
import mmap
from pathlib import Path
import sys

exe, network_path, expected = sys.argv[1:]
network = Path(network_path).read_bytes()
with Path(exe).open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as elf:
    offset = elf.find(network)
    assert offset >= 0, 'Authentic network bytes missing from executable'
    digest = hashlib.sha256(elf[offset:offset + len(network)]).hexdigest()
    assert digest == expected
    print(f'EMBEDDED_NNUE_OFFSET={offset} SIZE={len(network)} SHA256={digest}')
PY
        } | tee "$report/elf.log"
        unlink "src/$net_name"
        install -m 755 "src/$exe" "$staged/$label/$exe"
        cmp "src/$exe" "$staged/$label/$exe"
        test -z "$(git status --porcelain=v1 --untracked-files=no)"
        while IFS= read -r untracked; do
            test "$untracked" = "src/$exe"
        done < <(git ls-files --others --exclude-standard)
    )
}

build_exact candidate "$candidate" "$candidate_tree"
build_exact r28 "$r28" "$r28_tree"
build_exact p0a28 "$p0a28" "$p0a28_tree"
