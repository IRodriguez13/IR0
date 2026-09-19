#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""git-filter-repo --commit-callback body: rewrite Cursor Agent commits."""

MAINT_NAME = b"Iv\xc3\xa1n Ezequiel Rodriguez"
MAINT_EMAIL = b"ivanrwcm25@gmail.com"
SOB = b"Signed-off-by: Iv\xc3\xa1n Ezequiel Rodriguez <ivanrwcm25@gmail.com>"

was_cursor = (
    commit.author_email == b"cursoragent@cursor.com"
    or commit.committer_email == b"cursoragent@cursor.com"
)

if commit.author_email == b"cursoragent@cursor.com":
    commit.author_name = MAINT_NAME
    commit.author_email = MAINT_EMAIL
if commit.committer_email == b"cursoragent@cursor.com":
    commit.committer_name = MAINT_NAME
    commit.committer_email = MAINT_EMAIL

msg = commit.message.decode("utf-8", errors="replace")
kept: list[str] = []
for line in msg.splitlines():
    if line.startswith("Co-authored-by:"):
        continue
    if line.startswith("Helped-by:"):
        continue
    low = line.lower()
    if line.startswith("Reviewed-by:") and ("cursor" in low or "composer" in low):
        continue
    kept.append(line)

new_msg = "\n".join(kept).rstrip()
if new_msg:
    new_msg += "\n"
if SOB.decode("utf-8") not in new_msg and (
    was_cursor or commit.author_email == MAINT_EMAIL
):
    new_msg += SOB.decode("utf-8") + "\n"
commit.message = new_msg.encode("utf-8")
