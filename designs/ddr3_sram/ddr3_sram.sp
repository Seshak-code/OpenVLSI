* ============================================================================
* DDR3 SRAM Module — SkyWater 130nm (sky130A)
* ============================================================================
* Hierarchical design: 8x8 6T SRAM array + periphery + DDR3 SSTL-15 I/O
*   [1] 6T bitcell subcircuit (cross-coupled inverters + access NMOS)
*   [2] 8x8 bitcell array (64-bit demo scale, MNA-tractable)
*   [3] Precharge & equalize PMOS per bitline pair
*   [4] 3-to-8 row decoder (NAND3 + INV chain)
*   [5] Sense amplifier (cross-coupled latch) per column
*   [6] Write drivers per column
*   [7] DDR3 SSTL-15 DQ I/O slice: push-pull driver + ODT + Vref
* ============================================================================
.include "sky130_fd_pr.lib"
.include "sky130_fd_sc_hd.lib"

* -- Rails ---------------------------------------------------------------
Vvdd  vdd 0 DC 1.8
Vvss  vss 0 DC 0
Vvddq vddq 0 DC 1.5
Vvtt  vtt 0 DC 0.75
Vvref vref 0 DC 0.75

* -- [1] 6T Bitcell Subcircuit ------------------------------------------------
* Storage: 2 cross-coupled inverters (PU1/PU2 + PD1/PD2)
* Access:  2 NMOS pass transistors (PG1/PG2) gated by wordline WL
.subckt sram_6t BL BLB WL vdd vss
+ params: WPU=0.42u WPD=1.0u WPG=0.5u LGATE=0.15u
MPU1 n1 q vdd vdd sky130_fd_pr__pfet_01v8 W={WPU} L={LGATE}
MPU2 q n1 vdd vdd sky130_fd_pr__pfet_01v8 W={WPU} L={LGATE}
MPD1 n1 q vss vss sky130_fd_pr__nfet_01v8 W={WPD} L={LGATE}
MPD2 q n1 vss vss sky130_fd_pr__nfet_01v8 W={WPD} L={LGATE}
MPG1 BL WL q vss sky130_fd_pr__nfet_01v8 W={WPG} L={LGATE}
MPG2 BLB WL n1 vss sky130_fd_pr__nfet_01v8 W={WPG} L={LGATE}
.ends sram_6t

* -- [2] 8x8 Bitcell Array -----------------------------------------------------
* Row 0 (address 0b000)
XBC00 bl0 blb0 wl0 vdd vss sram_6t
XBC01 bl1 blb1 wl0 vdd vss sram_6t
XBC02 bl2 blb2 wl0 vdd vss sram_6t
XBC03 bl3 blb3 wl0 vdd vss sram_6t
XBC04 bl4 blb4 wl0 vdd vss sram_6t
XBC05 bl5 blb5 wl0 vdd vss sram_6t
XBC06 bl6 blb6 wl0 vdd vss sram_6t
XBC07 bl7 blb7 wl0 vdd vss sram_6t
* Row 1 (address 0b001)
XBC10 bl0 blb0 wl1 vdd vss sram_6t
XBC11 bl1 blb1 wl1 vdd vss sram_6t
XBC12 bl2 blb2 wl1 vdd vss sram_6t
XBC13 bl3 blb3 wl1 vdd vss sram_6t
XBC14 bl4 blb4 wl1 vdd vss sram_6t
XBC15 bl5 blb5 wl1 vdd vss sram_6t
XBC16 bl6 blb6 wl1 vdd vss sram_6t
XBC17 bl7 blb7 wl1 vdd vss sram_6t
* Row 2 (address 0b010)
XBC20 bl0 blb0 wl2 vdd vss sram_6t
XBC21 bl1 blb1 wl2 vdd vss sram_6t
XBC22 bl2 blb2 wl2 vdd vss sram_6t
XBC23 bl3 blb3 wl2 vdd vss sram_6t
XBC24 bl4 blb4 wl2 vdd vss sram_6t
XBC25 bl5 blb5 wl2 vdd vss sram_6t
XBC26 bl6 blb6 wl2 vdd vss sram_6t
XBC27 bl7 blb7 wl2 vdd vss sram_6t
* Row 3 (address 0b011)
XBC30 bl0 blb0 wl3 vdd vss sram_6t
XBC31 bl1 blb1 wl3 vdd vss sram_6t
XBC32 bl2 blb2 wl3 vdd vss sram_6t
XBC33 bl3 blb3 wl3 vdd vss sram_6t
XBC34 bl4 blb4 wl3 vdd vss sram_6t
XBC35 bl5 blb5 wl3 vdd vss sram_6t
XBC36 bl6 blb6 wl3 vdd vss sram_6t
XBC37 bl7 blb7 wl3 vdd vss sram_6t
* Row 4 (address 0b100)
XBC40 bl0 blb0 wl4 vdd vss sram_6t
XBC41 bl1 blb1 wl4 vdd vss sram_6t
XBC42 bl2 blb2 wl4 vdd vss sram_6t
XBC43 bl3 blb3 wl4 vdd vss sram_6t
XBC44 bl4 blb4 wl4 vdd vss sram_6t
XBC45 bl5 blb5 wl4 vdd vss sram_6t
XBC46 bl6 blb6 wl4 vdd vss sram_6t
XBC47 bl7 blb7 wl4 vdd vss sram_6t
* Row 5 (address 0b101)
XBC50 bl0 blb0 wl5 vdd vss sram_6t
XBC51 bl1 blb1 wl5 vdd vss sram_6t
XBC52 bl2 blb2 wl5 vdd vss sram_6t
XBC53 bl3 blb3 wl5 vdd vss sram_6t
XBC54 bl4 blb4 wl5 vdd vss sram_6t
XBC55 bl5 blb5 wl5 vdd vss sram_6t
XBC56 bl6 blb6 wl5 vdd vss sram_6t
XBC57 bl7 blb7 wl5 vdd vss sram_6t
* Row 6 (address 0b110)
XBC60 bl0 blb0 wl6 vdd vss sram_6t
XBC61 bl1 blb1 wl6 vdd vss sram_6t
XBC62 bl2 blb2 wl6 vdd vss sram_6t
XBC63 bl3 blb3 wl6 vdd vss sram_6t
XBC64 bl4 blb4 wl6 vdd vss sram_6t
XBC65 bl5 blb5 wl6 vdd vss sram_6t
XBC66 bl6 blb6 wl6 vdd vss sram_6t
XBC67 bl7 blb7 wl6 vdd vss sram_6t
* Row 7 (address 0b111)
XBC70 bl0 blb0 wl7 vdd vss sram_6t
XBC71 bl1 blb1 wl7 vdd vss sram_6t
XBC72 bl2 blb2 wl7 vdd vss sram_6t
XBC73 bl3 blb3 wl7 vdd vss sram_6t
XBC74 bl4 blb4 wl7 vdd vss sram_6t
XBC75 bl5 blb5 wl7 vdd vss sram_6t
XBC76 bl6 blb6 wl7 vdd vss sram_6t
XBC77 bl7 blb7 wl7 vdd vss sram_6t

* -- [3] Precharge & Equalize (per bitline pair) -----------------------------------
* PMOS precharge to VDD + equalize transistor between BL/BLB
MPC0a bl0 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC0b blb0 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC0e bl0 pre blb0 vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC1a bl1 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC1b blb1 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC1e bl1 pre blb1 vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC2a bl2 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC2b blb2 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC2e bl2 pre blb2 vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC3a bl3 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC3b blb3 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC3e bl3 pre blb3 vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC4a bl4 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC4b blb4 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC4e bl4 pre blb4 vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC5a bl5 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC5b blb5 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC5e bl5 pre blb5 vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC6a bl6 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC6b blb6 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC6e bl6 pre blb6 vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC7a bl7 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC7b blb7 pre vdd vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u
MPC7e bl7 pre blb7 vdd sky130_fd_pr__pfet_01v8 W=1u L=0.15u

* -- [4] 3-to-8 Row Decoder (NAND3 + INV) ---------------------------------------
* wl_i active-high when (a2,a1,a0) == i
XND0 a0 a1 a2 nd0 vdd vss sky130_fd_sc_hd__nand3_1
XWI0 nd0 wl0 vdd vss sky130_fd_sc_hd__inv_1
XND1 a0b a1 a2 nd1 vdd vss sky130_fd_sc_hd__nand3_1
XWI1 nd1 wl1 vdd vss sky130_fd_sc_hd__inv_1
XND2 a0 a1b a2 nd2 vdd vss sky130_fd_sc_hd__nand3_1
XWI2 nd2 wl2 vdd vss sky130_fd_sc_hd__inv_1
XND3 a0b a1b a2 nd3 vdd vss sky130_fd_sc_hd__nand3_1
XWI3 nd3 wl3 vdd vss sky130_fd_sc_hd__inv_1
XND4 a0 a1 a2b nd4 vdd vss sky130_fd_sc_hd__nand3_1
XWI4 nd4 wl4 vdd vss sky130_fd_sc_hd__inv_1
XND5 a0b a1 a2b nd5 vdd vss sky130_fd_sc_hd__nand3_1
XWI5 nd5 wl5 vdd vss sky130_fd_sc_hd__inv_1
XND6 a0 a1b a2b nd6 vdd vss sky130_fd_sc_hd__nand3_1
XWI6 nd6 wl6 vdd vss sky130_fd_sc_hd__inv_1
XND7 a0b a1b a2b nd7 vdd vss sky130_fd_sc_hd__nand3_1
XWI7 nd7 wl7 vdd vss sky130_fd_sc_hd__inv_1
* Address input buffers
XAB0 a0 a0b vdd vss sky130_fd_sc_hd__inv_1
XAB1 a1 a1b vdd vss sky130_fd_sc_hd__inv_1
XAB2 a2 a2b vdd vss sky130_fd_sc_hd__inv_1
* Address sources (DC for .OP; PULSE for transient read/write)
Va0 a0 0 DC 0 PULSE(0 1.8 0 0.1n 0.1n 4.9n 10n)
Va1 a1 0 DC 0
Va2 a2 0 DC 0

* -- [5] Sense Amplifiers (cross-coupled latch per column) --------------------------
* SAE high -> latch regeneratively amplifies BL/BLB differential
MSA0p sa0 sa0b vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MSA0n sa0 sa0b vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA0q sa0b sa0 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA0e sa0 sae bl0 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA0f sa0b sae blb0 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA1p sa1 sa1b vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MSA1n sa1 sa1b vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA1q sa1b sa1 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA1e sa1 sae bl1 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA1f sa1b sae blb1 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA2p sa2 sa2b vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MSA2n sa2 sa2b vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA2q sa2b sa2 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA2e sa2 sae bl2 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA2f sa2b sae blb2 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA3p sa3 sa3b vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MSA3n sa3 sa3b vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA3q sa3b sa3 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA3e sa3 sae bl3 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA3f sa3b sae blb3 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA4p sa4 sa4b vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MSA4n sa4 sa4b vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA4q sa4b sa4 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA4e sa4 sae bl4 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA4f sa4b sae blb4 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA5p sa5 sa5b vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MSA5n sa5 sa5b vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA5q sa5b sa5 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA5e sa5 sae bl5 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA5f sa5b sae blb5 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA6p sa6 sa6b vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MSA6n sa6 sa6b vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA6q sa6b sa6 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA6e sa6 sae bl6 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA6f sa6b sae blb6 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA7p sa7 sa7b vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MSA7n sa7 sa7b vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA7q sa7b sa7 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MSA7e sa7 sae bl7 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
MSA7f sa7b sae blb7 vss sky130_fd_pr__nfet_01v8 W=2u L=0.15u
* Sense-enable driver
Vsae sae 0 DC 1.8 PULSE(1.8 0 0 0.1n 0.1n 2.9n 10n)

* -- [6] Write Drivers (per column) ----------------------------------------------
* WEB asserted -> DIN drives BL/BLB complementarily
MWD0a bl0 din0 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD0b bl0 din0 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MWD0c blb0 dinn0 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD0d blb0 dinn0 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
Vdin0 din0 0 DC 1.8
Vdinn0 dinn0 0 DC 0
MWD1a bl1 din1 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD1b bl1 din1 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MWD1c blb1 dinn1 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD1d blb1 dinn1 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
Vdin1 din1 0 DC 1.8
Vdinn1 dinn1 0 DC 0
MWD2a bl2 din2 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD2b bl2 din2 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MWD2c blb2 dinn2 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD2d blb2 dinn2 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
Vdin2 din2 0 DC 1.8
Vdinn2 dinn2 0 DC 0
MWD3a bl3 din3 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD3b bl3 din3 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MWD3c blb3 dinn3 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD3d blb3 dinn3 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
Vdin3 din3 0 DC 1.8
Vdinn3 dinn3 0 DC 0
MWD4a bl4 din4 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD4b bl4 din4 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MWD4c blb4 dinn4 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD4d blb4 dinn4 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
Vdin4 din4 0 DC 1.8
Vdinn4 dinn4 0 DC 0
MWD5a bl5 din5 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD5b bl5 din5 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MWD5c blb5 dinn5 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD5d blb5 dinn5 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
Vdin5 din5 0 DC 1.8
Vdinn5 dinn5 0 DC 0
MWD6a bl6 din6 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD6b bl6 din6 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MWD6c blb6 dinn6 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD6d blb6 dinn6 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
Vdin6 din6 0 DC 1.8
Vdinn6 dinn6 0 DC 0
MWD7a bl7 din7 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD7b bl7 din7 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
MWD7c blb7 dinn7 vdd vdd sky130_fd_pr__pfet_01v8 W=2u L=0.15u
MWD7d blb7 dinn7 vss vss sky130_fd_pr__nfet_01v8 W=1u L=0.15u
Vdin7 din7 0 DC 1.8
Vdinn7 dinn7 0 DC 0

* -- [7] DDR3 SSTL-15 DQ I/O Slice --------------------------------------------
* Push-pull driver (PMOS pull-up + NMOS pull-down) with series R
MDQp dq_out sa0 vddq vddq sky130_fd_pr__pfet_01v8 W=60u L=0.15u
MDQn dq_out sa0 vss vss sky130_fd_pr__nfet_01v8 W=30u L=0.15u
Rdq dq_out dq_pad 15
* On-die termination to VTT
Rtt1 dq_pad tt_mid 20
Rtt2 tt_mid vtt 20
* DRAM-side load
Cdq dq_pad 0 2p
* Vref buffer (source follower)
Rvr1 vddq vref_l 10k
Rvr2 vref_l 0 10k
Cvr vref_l 0 10p
Mvr vddq vref_l vref_o 0 sky130_fd_pr__nfet_01v8 W=4u L=0.5u
Ivr vref_o 0 DC 14u

* -- Control timing sources ----------------------------------------------
Vpre pre 0 DC 0 PULSE(0 1.8 0 0.1n 0.1n 2.9n 10n)

* -- Model Cards (SkyWater 130nm Level-1 equivalents) ---------------------
.model sky130_fd_pr__nfet_01v8 nmos level=1 vto=0.45 kp=120u lambda=0.05 gamma=0.4
.model sky130_fd_pr__pfet_01v8 pmos level=1 vto=-0.45 kp=40u lambda=0.05 gamma=0.4

* -- Analyses ----------------------------------------------------------------
.op
.tran 10p 10n
.end
