#!/usr/bin/env python
# coding: utf-8

# In[30]:


import os
import re
import sys
import csv
from operator import add

if len(sys.argv) != 2:
	print("Input a file to parse!")
	quit()

filename = sys.argv[1]

with open(filename) as f:
	lines = f.readlines()
	f.close()


for line in lines:
	match = re.search('- Completed: (\d+)', line)
	if match:
		print(match.group(1) + "    ", end = "")
	match = re.search('- Succeeded: (\d+)', line)
	if match:
		print(match.group(1))

