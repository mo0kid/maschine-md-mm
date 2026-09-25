# Empty Machinedrum and Monomachine pattern/kit banks

- `Machinedrum-empty-patterns-kits.syx` contains 64 initialized blank kits and 128 cleared patterns for Machinedrum OS 1.63. The kits have usable names (`INIT KIT 01` through `INIT KIT 64`) and no assigned drum machines. Patterns A1-D16 are linked to kits 1-64; patterns E1-H16 repeat that mapping.
- `Monomachine-empty-patterns-kits.syx` contains 128 cleared kit dumps and 128 cleared pattern dumps for Monomachine OS 1.32b.

The templates were obtained from each machine's firmware after using its panel's **CLEAR KIT** and **CLEAR PATTERN** actions. The Machinedrum kit names and pattern-kit associations were initialized so that selecting a pattern loads a usable blank kit slot. Each dump was assigned a target slot and given a fresh Elektron checksum. The files contain only kit and pattern dumps; they leave globals, songs, and samples untouched.

Importing a file replaces every kit and pattern in that machine's working memory. Save a backup first if you want to keep the current contents. In Maschine MD-MM, choose **File > Load SysEx File to Machinedrum...** or **Load SysEx File to Monomachine...** and select the corresponding file.

Machinedrum kits start with no assigned machines, so they are silent until you assign a machine in **EDIT KIT**. Save the kit after editing it. In Extended mode, selecting another pattern loads the kit linked to that pattern.

The Machinedrum file has been imported and read back through the OS 1.63 firmware emulator, with all 192 slots matching. The Monomachine file passes the SysEx format checks, and its first kit and pattern were accepted by OS 1.32b. However, that firmware leaves **SYSEX RECV** after each dump. The current Maschine MD-MM loader sends the remaining dumps without returning to receive mode, so loading this entire file through that menu does **not** clear all Monomachine slots. Use a sender that can pause and re-enter **GLOBAL > FILE > SYSEX RECV** for each dump, or split the file into individual SysEx messages before sending. Do not rely on one uninterrupted transfer to clear the Monomachine.
