
import os

benchmark = "aspdac-benchmark"
caselist = ["ibm14"]  # "case3", "ibm08", "ibm13"
#"case2h", "case3", "ibm02", "ibm08", "ibm13", "ibm14"


for case in caselist:

    cmd = f"python3 runscript.py {benchmark} {case} newweightDME -clockaware DME  -clknet -tpclknet"
    # Execute the command here
    print(f"Executing: {cmd}")
    os.system(cmd)
    cmd = f"python3 runscript.py {benchmark} {case} newweightMMM -clockaware MMM  -clknet -tpclknet"
    # Execute the command here
    print(f"Executing: {cmd}")
    os.system(cmd)
# # #     cmd = f"python3 runscript.py {benchmark} {case} accDMEno0initt3 -clockaware DME -clknet -tpclknet -t3"
# # #     # Execute the command here
# # #     print(f"Executing: {cmd}")
# # #     os.system(cmd)
# # #     cmd = f"python3 runscript.py {benchmark} {case} accMMMno0initt3 -clockaware MMM -clknet -tpclknet -t3"
# # #     # Execute the command here
# # #     print(f"Executing: {cmd}")
# # #     os.system(cmd)


#     cmd = f"python3 runscript.py {benchmark} {case} accsimplet4 -clockaware simple  -clknet -t4"
#     # Execute the command here
#     print(f"Executing: {cmd}")
#     os.system(cmd)


    # cmd = f"python3 runscript.py {benchmark} {case} accDMEt4 -clockaware DME -TP0init -clknet -tpclknet -t4"
    # # Execute the command here
    # print(f"Executing: {cmd}")
    # os.system(cmd)
#     cmd = f"python3 runscript.py {benchmark} {case} accDMEno0initt4 -clockaware DME -clknet -tpclknet -t4"
#     # Execute the command here
#     print(f"Executing: {cmd}")
#     os.system(cmd)
#     cmd = f"python3 runscript.py {benchmark} {case} accMMMno0initt4 -clockaware MMM -clknet -tpclknet -t4"
#     # Execute the command here
#     print(f"Executing: {cmd}")
#     os.system(cmd)



#     # cmd = f"python3 runscript.py {benchmark} {case} base-acc -TP0init"
#     # # Execute the command here
#     # print(f"Executing: {cmd}")
#     # os.system(cmd)

#     cmd = f"python3 runscript.py {benchmark} {case} DMEclk-acc -TP0init -clockaware DME -clknet"
#     # Execute the command here
#     print(f"Executing: {cmd}")
#     os.system(cmd)   

#     cmd = f"python3 runscript.py {benchmark} {case} MMMclk-acc -TP0init -clockaware MMM -clknet"
#     # Execute the command here
#     print(f"Executing: {cmd}")
#     os.system(cmd)

#     cmd = f"python3 runscript.py {benchmark} {case} MMMclkno0init-acc -clockaware MMM -clknet"
#     # Execute the command here
#     print(f"Executing: {cmd}")
#     os.system(cmd)


# caselist = ["clkad1", "clkbb1"]  #, "clkbb1"
# for case in caselist:

#     cmd = f"python3 runscript.py {benchmark} {case} newweightDME -clockaware DME  -clknet -tpclknet -expandFactor 1.03"
#     # Execute the command here
#     print(f"Executing: {cmd}")
#     os.system(cmd)

    # cmd = f"python3 runscript.py {benchmark} {case} accsimplet3 -clockaware simple  -clknet  -expandFactor 1.03 -t3"
    # # Execute the command here
    # print(f"Executing: {cmd}")
    # os.system(cmd)
    # cmd = f"python3 runscript.py {benchmark} {case} accDMEno0initt3 -clockaware DME -expandFactor 1.03 -clknet -tpclknet -t3"
    # # Execute the command here
    # print(f"Executing: {cmd}")
    # os.system(cmd)
    # cmd = f"python3 runscript.py {benchmark} {case} accMMMno0initt3 -clockaware MMM -expandFactor 1.03 -clknet -tpclknet -t3"
    # # Execute the command here
    # print(f"Executing: {cmd}")
    # os.system(cmd)

    # cmd = f"python3 runscript.py {benchmark} {case} accsimplet4 -clockaware simple  -clknet -expandFactor 1.03 -t4"
    # # Execute the command here
    # print(f"Executing: {cmd}")
    # os.system(cmd)

    # cmd = f"python3 runscript.py {benchmark} {case} accDMEt4 -clockaware DME -TP0init -expandFactor 1.03 -clknet -tpclknet -t4"
    # # Execute the command here
    # print(f"Executing: {cmd}")
    # os.system(cmd)
    # cmd = f"python3 runscript.py {benchmark} {case} accDMEno0initt4 -clockaware DME -expandFactor 1.03 -clknet -tpclknet -t4"
    # # Execute the command here
    # print(f"Executing: {cmd}")
    # os.system(cmd)
    # cmd = f"python3 runscript.py {benchmark} {case} accMMMno0initt4 -clockaware MMM -expandFactor 1.03 -clknet -tpclknet -t4"
    # # Execute the command here
    # print(f"Executing: {cmd}")
    # os.system(cmd)





#     cmd = f"python3 runscript.py {benchmark} {case} base-acc -TP0init  -expandFactor 1.03"
#     # Execute the command here
#     print(f"Executing: {cmd}")
#     os.system(cmd)

#     cmd = f"python3 runscript.py {benchmark} {case} DMEclk-acc -TP0init -expandFactor 1.03 -clockaware DME -clknet"
#     # Execute the command here
#     print(f"Executing: {cmd}")
#     os.system(cmd)   

#     cmd = f"python3 runscript.py {benchmark} {case} DMEclkno0init-acc -expandFactor 1.03 -clockaware DME -clknet"
#     # Execute the command here
#     print(f"Executing: {cmd}")
#     os.system(cmd)

#     cmd = f"python3 runscript.py {benchmark} {case} MMMclk-acc -TP0init -expandFactor 1.03 -clockaware MMM -clknet"
#     # Execute the command here
#     print(f"Executing: {cmd}")
#     os.system(cmd)

#     cmd = f"python3 runscript.py {benchmark} {case} MMMclkno0init-acc -expandFactor 1.03 -clockaware MMM -clknet"
#     # Execute the command here
#     print(f"Executing: {cmd}")
#     os.system(cmd)
