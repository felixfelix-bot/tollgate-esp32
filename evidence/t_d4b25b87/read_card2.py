import sqlite3
db = "/home/c03rad0r/.hermes/kanban/boards/car-tollgate/kanban.db"
con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
cur = con.cursor()
print("task_comments cols:", [d[1] for d in cur.execute("PRAGMA table_info(task_comments)")])
print("== comments on t_d4b25b87 ==")
for r in cur.execute("SELECT * FROM task_comments WHERE task_id='t_d4b25b87'"):
    print(" *", r)
print("== all cards ==")
for r in cur.execute("SELECT id,title,status,assignee,created_at,completed_at FROM tasks ORDER BY created_at"):
    print(" -", r)
print("== links ==")
print("task_links cols:", [d[1] for d in cur.execute("PRAGMA table_info(task_links)")])
for r in cur.execute("SELECT * FROM task_links"):
    print(" -", r)
con.close()
