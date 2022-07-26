#!/usr/bin/env python
# coding: utf-8

# In[30]:


import os
import sys
import time
import subprocess


binary = "../../../dram_integerkey.so"
tree = "dram_int_btree"
exp_type = "Uniform"
PM = False
threads = [80,60,40,30,20,10,5,1]
ops = ["-r 1","-r 0 -i 1","-r 0 -s 1"]

pibench_path = "./PiBench"

numactl = "numactl --membind=0"
base_command = "sudo LD_PRELOAD=/usr/lib64/libjemalloc.so"
cores = [0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,\
40,42,44,46,48,50,52,54,56,58,60,62,64,66,68,70,72,74,76,78,\
1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31,33,35,37,39,\
41,43,45,47,49,51,53,55,57,59,61,63,65,67,69,71,73,75,77,79];

op_to_filename = {"-r 1":"lookup", "-r 0 -i 1":"insert", "-r 0 -u 1":"update", "-r 0 -s 1":"scan", 
"-r 0.9 -i 0.1":"read_heavy", "-r 0.5 -i 0.5":"balanced", "-r 0.1 -i 0.9":"write_heavy"}

def create_command(num_thread, op, exp):
    l = []
    for i in range(num_thread):
        l.append(str(cores[i]))
    s = "OMP_PLACES=\'{" + ",".join(l) + "}\' OMP_PROC_BIND=TRUE OMP_NESTED=TRUE"
    command_list = [
        numactl,
        base_command, 
        s, 
        pibench_path, 
        binary, 
        "-n " + str(100000000), 
        "--mode time --seconds " + str(10),
        op,
        "-t " + str(num_thread)
        ]
    if exp == "Skewed":
        command_list.append("--distribution SELFSIMILAR --skew " + str(0.2))
    return " ".join(command_list)





suffix = ""
if exp_type == "Uniform":
    suffix = "_uniform_"
elif exp_type == "Skewed":
    suffix = "_skewed_"


for op in ops:
    filename = tree + suffix + op_to_filename[op] + ".txt"
    if os.path.exists(filename): # remove old result file if exists
        print("removing old file")
        os.remove(filename)
    for t in threads: # for each # thread
        command = create_command(t, op, exp_type) + " >> " + filename
        print(command)
        with open(filename, 'a') as f:
            f.write(command + '\n')
            f.close()
        os.system(command)
        if PM == True:
            os.system("sudo rm pool")
        time.sleep(2)
