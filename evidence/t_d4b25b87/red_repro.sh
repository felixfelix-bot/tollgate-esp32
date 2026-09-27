#!/bin/bash
# Reproduce the RED state: run the new tests against the pre-fix tree
# (origin/master == 46f9a58) where main/gps.{c,h} and the get_location MCP tool
# do not exist.  A red here is what proves the tests actually guard the change.
set -u
BASE=/home/c03rad0r/worktrees/t_d4b25b87-base
NEW=/home/c03rad0r/worktrees/t_d4b25b87

echo "### RED 1: new test_gps.c + new Makefile against pre-fix tree (no main/gps.c)"
cp "$NEW/tests/unit/test_gps.c" "$BASE/tests/unit/test_gps.c"
cp "$NEW/tests/unit/Makefile"   "$BASE/tests/unit/Makefile"
( cd "$BASE/tests/unit" && make test_gps 2>&1 | tail -4 )

echo
echo "### RED 2: new test_mcp_handler.c against pre-fix tree, original Makefile"
git -C "$BASE" checkout -- tests/unit/Makefile
cp "$NEW/tests/unit/test_mcp_handler.c" "$BASE/tests/unit/test_mcp_handler.c"
( cd "$BASE/tests/unit" && make test_mcp_handler 2>&1 | grep -E "error|Error|GetObjectItem.*get_location" | head -6 )

# Clean up the scratch copies so the base worktree stays pristine.
git -C "$BASE" checkout -- tests/unit/Makefile tests/unit/test_mcp_handler.c 2>/dev/null
rm -f "$BASE/tests/unit/test_gps.c"
