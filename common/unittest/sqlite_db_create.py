#!/usr/bin/python
import sqlite3

cx = sqlite3.connect("test.db");
cur = cx.cursor();

cur.execute("pragma foreign_keys=on");

cur.execute("create table t_student(idx integer primary key autoincrement , bin_info blob)"); 
cx.commit();

cx.close();
	


