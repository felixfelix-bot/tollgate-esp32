import sqlite3, json
db = "/home/c03rad0r/.hermes/kanban/boards/car-tollgate/kanban.db"
con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
con.row_factory = sqlite3.Row
cur = con.cursor()
print("== tables ==")
for r in cur.execute("SELECT name FROM sqlite_master WHERE type='table'"):
    print(" ", r["name"])
print("== task t_d4b25b87 ==")
cols = [d[1] for d in cur.execute("PRAGMA table_info(tasks)")]
print("cols:", cols)
row = cur.execute("SELECT * FROM tasks WHERE id='t_d4b25b87'").fetchone()
for k in row.keys():
    v = row[k]
    if isinstance(v, str) and len(v) > 4000:
        v = v[:4000] + f"...[{len(row[k])} chars]"
    print(f"--{k}: {v}")
print("== comments on t_d4b25b87 ==")
for r in cur.execute("SELECT author, created_ts, substr(body,1,2000) b FROM task_comments WHERE task_id='t_d4b25b87' ORDER BY created_ts"):
    print(" *", r["author"], r["created_ts"], r["b"])
print("== sibling cards ==")
for r in cur.execute("SELECT id,title,status,assignee,created_at,completed_at,substr(COALESCE(result,''),1,600) res FROM tasks ORDER BY created_at"):
    print(" -", r["id"], "|", r["status"], "|", r["assignee"], "|", r["title"], "|", r["created_at"], "|", r["completed_at"])
con.close()
