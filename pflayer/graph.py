import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("stats.csv")

plt.figure(figsize=(10,6))

plt.plot(df["mix"], df["logicalReads"], marker="o", label="Logical Reads")
plt.plot(df["mix"], df["logicalWrites"], marker="o", label="Logical Writes")
plt.plot(df["mix"], df["physicalReads"], marker="o", label="Physical Reads")
plt.plot(df["mix"], df["physicalWrites"], marker="o", label="Physical Writes")
plt.plot(df["mix"], df["pagesAccessed"], marker="o", label="Pages Accessed")

plt.xlabel("Write Percentage (%)")
plt.ylabel("Operation Count")
plt.title("PF Layer I/O Statistics vs Read/Write Mix")
plt.grid(True)
plt.legend()
plt.savefig("pf_stats.png")
plt.show()
