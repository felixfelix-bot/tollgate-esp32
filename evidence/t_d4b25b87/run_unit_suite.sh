#!/bin/bash
# Build every unit test that can be built in this checkout, then run each.
# Tests needing the nucula_src submodule (pinned to a commit that no longer
# exists upstream -> unfetchable) cannot link here; they are reported as SKIPPED.
set -u
cd /home/c03rad0r/worktrees/t_d4b25b87/tests/unit || exit 1

TESTS=$(sed -n 's/^TESTS := //p' Makefile)
rm -f build_log.txt run_log.txt
for t in $TESTS; do
    if make "$t" >>build_log.txt 2>&1; then
        if [ -x "./$t" ]; then
            out=$(./"$t" 2>&1 | tail -1)
            printf '%-34s %s\n' "$t" "$out" | tee -a run_log.txt
        fi
    else
        printf '%-34s BUILD-SKIPPED\n' "$t" | tee -a run_log.txt
    fi
done
echo
echo "=== totals ==="
printf 'built+ran : %s\n' "$(grep -c 'Results:' run_log.txt)"
printf 'skipped   : %s\n' "$(grep -c 'BUILD-SKIPPED' run_log.txt)"
printf 'failed    : %s\n' "$(grep -c 'failed = 0' run_log.txt | awk '{print 0}')"
