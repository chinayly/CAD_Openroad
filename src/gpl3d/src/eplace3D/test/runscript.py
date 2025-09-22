import sys
import os

if len(sys.argv) < 4:
    print("Usage: python3 runscript.py <benchmark> <case> <test_number> "
    "(optional: <-expandFactor 1.1> <-TP0init> <-clockaware MMM/DME/simple><-clknet><-tpclknet><-clkmass><-tpclkmass><-clkwirelevel><-t3 or -t4><-partoffratio 0.05><-partseed 123456>)")
    sys.exit(1)

benchmark = sys.argv[1]
case = sys.argv[2]
test_num = sys.argv[3]

# Optional arguments
expand_factor = None
tp0_init = False
clkaware = False
spclknet = False
tpclknet = False
spclkmasscenter = False
tpclkmass = False
clkwirelevel = False
tier3 = False
tier4 = False

partoffratio = None
partseed = None

for i in range(4, len(sys.argv)):
    if sys.argv[i] == "-expandFactor":
        expand_factor = sys.argv[i + 1]
    elif sys.argv[i] == "-TP0init":
        tp0_init = True
    elif sys.argv[i] == "-partoffratio":
        partoffratio =  sys.argv[i + 1]
    elif sys.argv[i] == "-partseed":
        partseed =  sys.argv[i + 1]
    elif sys.argv[i] == "-clockaware":
        clkaware = True
        clkmethod = sys.argv[i + 1] 
        if clkmethod not in ["MMM", "DME", "simple"]:
            print("Invalid clock-aware method. Choose from MMM, DME, or simple.")
            sys.exit(1)
    elif sys.argv[i] == "-clknet":
        spclknet = True
    elif sys.argv[i] == "-tpclknet":
        tpclknet = True
    elif sys.argv[i] == "-clkmass":
        spclkmasscenter = True
    elif sys.argv[i] == "-tpclkmass":
        tpclkmass = True
    elif sys.argv[i] == "-clkwirelevel":
        clkwirelevel = True
    elif sys.argv[i] == "-t3":
        tier3 = True
    elif sys.argv[i] == "-t4":
        tier4 = True

# Create output directory if it doesn't exist
output_dir = f"./{benchmark}/{case}/t{test_num}/"
os.makedirs(output_dir, exist_ok=True)

# -plotFiller
cmd = f"../build/Placer/PlacerTest -aux ../../case/{benchmark}/{case}/{case}.aux -outputDir ./{benchmark}/{case}/t{test_num}/ "
if expand_factor:
    cmd += f" -expandFactor {expand_factor}"

if tp0_init:
    cmd += " -TP0init"

if partoffratio:
    cmd += f" -partoffratio {partoffratio}"

if partseed:
    cmd += f" -partseed {partseed}"

if clkaware:
    cmd += " -clockaware"
    cmd += f" -{clkmethod}"

    if spclknet:
        cmd += " -pseudoClkNet"
    if tpclknet:
        cmd += " -TPpseudoClkNet"
    if spclkmasscenter:
        cmd += " -pseudoClkMassCenter"
    if tpclkmass:
        cmd += " -TPClkMass"
    if clkwirelevel:
        cmd += " -clkwirelevel"

if tier3:
    cmd += " -t3"
elif tier4:
    cmd += " -t4"


cmd += f" | tee ./{benchmark}/{case}/t{test_num}/log.txt"


print(f"Executing: {cmd}")
os.system(cmd)
# ./PlacerTest -aux ../../../case/ICCAD2004/ibm02/ibm02.aux -outputDir ../../test/iccad04/ibm02/t2/ -plotFiller | tee ../../test/iccad04/ibm02/t2/log.txt 






# Read results from forLGDP folder
dp_hpwl_total = 0
lg_hpwl_total = 0
gp_hpwl_total = 0
time_total = 0
results_count = 0

forLGDP_dir = f"./{benchmark}/{case}/t{test_num}/forLGDP/"
if os.path.exists(forLGDP_dir):
    # Look for results files (results0.txt, results1.txt, etc.)
    i = 0
    while True:
        results_file = os.path.join(forLGDP_dir, f"Results{i}.txt")
        if not os.path.exists(results_file):
            break
        
        try:
            with open(results_file, 'r') as f:
                lines = f.readlines()
            
            if len(lines) >= 2:
                # Get the second to last line
                second_last_line = lines[-2].strip()
                fourth_last_line = lines[-4].strip() if len(lines) >= 4 else ""
                fiveth_last_line = lines[-5].strip() if len(lines) >= 5 else ""
                sixth_last_line = lines[-6].strip() if len(lines) >= 6 else ""
                # Extract HPWL and Time values
                if "HPWL=" in second_last_line and "Time:" in second_last_line:
                    parts = second_last_line.split()
                    for j, part in enumerate(parts):
                        if part == "HPWL=" and j + 1 < len(parts):
                            hpwl = float(parts[j + 1])
                            # hpwl_total += hpwl
                        elif part == "Time:" and j + 1 < len(parts):
                            time = float(parts[j + 1])
                            time_total += time
                    
                    results_count += 1
                    # print(f"results{i}.txt - HPWL: {hpwl}, Time: {time}")
                if "HPWL=" in fourth_last_line and "Detail" in fourth_last_line:
                    parts = fourth_last_line.split()
                    for j, part in enumerate(parts):
                        if part == "HPWL=" and j + 1 < len(parts):
                            dp_hpwl = float(parts[j + 1])
                            dp_hpwl_total += dp_hpwl
                if "HPWL=" in fiveth_last_line and "Legal" in fiveth_last_line:
                    parts = fiveth_last_line.split()
                    for j, part in enumerate(parts):
                        if part == "HPWL=" and j + 1 < len(parts):
                            lg_hpwl = float(parts[j + 1])
                            lg_hpwl_total += lg_hpwl
                if "HPWL=" in sixth_last_line and "Global" in sixth_last_line:
                    parts = sixth_last_line.split()
                    for j, part in enumerate(parts):
                        if part == "HPWL=" and j + 1 < len(parts):
                            gp_hpwl = float(parts[j + 1])
                            gp_hpwl_total += gp_hpwl


        except Exception as e:
            error_msg = f"Error reading {results_file}: {e}"
            print(error_msg)
            with open(f"./{benchmark}/{case}/t{test_num}/log.txt", 'a') as log_f:
                log_f.write(error_msg + "\n")
        
        i += 1
    
    if results_count > 0:
        output_msg = f"\nfrom {results_count} tiers: \
            \nTotal GP HPWL: {gp_hpwl_total} \
            \nTotal LG HPWL: {lg_hpwl_total} \
            \nTotal DP HPWL: {dp_hpwl_total} "
        print(output_msg)
        with open(f"./{benchmark}/{case}/t{test_num}/log.txt", 'a') as log_f:
            log_f.write(output_msg + "\n")
        # print(f"Total Time: {time_total} sec")
    else:
        error_msg = "No valid results files found in forLGDP folder"
        print(error_msg)
        with open(f"./{benchmark}/{case}/t{test_num}/log.txt", 'a') as log_f:
            log_f.write(error_msg + "\n")
else:
    error_msg = f"forLGDP directory not found: {forLGDP_dir}"
    print(error_msg)
    with open(f"./{benchmark}/{case}/t{test_num}/log.txt", 'a') as log_f:
        log_f.write(error_msg + "\n")




# Read TSV count from log.txt
log_file = f"./{benchmark}/{case}/t{test_num}/log.txt"
tsv_count = None
spclktime =0
tpclktime =0
totalclktime =0
try:
    with open(log_file, 'r') as f:
        lines = f.readlines()
    
    # Find the line with TSV count information
    for line in lines:
        
        if line.strip().startswith("sp-place clk aware time"):
            # Extract the numerical value after "sp-place clk aware time"
            parts = line.strip().split(":")
            if len(parts) >= 2:
                spclktime = float(parts[1].strip())
                continue

        if line.strip().startswith("TSV count:"):
            # Extract the numerical value after "TSV count:"
            parts = line.strip().split(":")
            if len(parts) >= 2:
                tsv_count = int(parts[1].strip())
                continue
        if line.strip().startswith("TP-place clk aware time"):
            # Extract the numerical value after "TP-place clk aware time"
            parts = line.strip().split(":")
            if len(parts) >= 2:
                tpclktime = float(parts[1].strip())
                break


    totalclktime = spclktime + tpclktime
    if tsv_count is not None:
        output_msg = f"TSV count: {tsv_count}"
        output_msg += f"\nsp-place clk aware time: {spclktime} sec"
        output_msg += f"\ntp-place clk aware time: {tpclktime} sec"
        output_msg += f"\ntotal clk aware time: {totalclktime} sec"
        print(output_msg)
        with open(log_file, 'a') as log_f:
            log_f.write(output_msg + "\n")
    else:
        error_msg = "TSV count information not found in log file"
        print(error_msg)
        with open(log_file, 'a') as log_f:
            log_f.write(error_msg + "\n")
        
except FileNotFoundError:
    error_msg = f"Log file not found: {log_file}"
    print(error_msg)
except Exception as e:
    error_msg = f"Error reading log file for TSV count: {e}"
    print(error_msg)