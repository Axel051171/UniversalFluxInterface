# Bring-up aid, board v1.0: reads the ID of the PSRAM U11 (APS6404L) by bit-banging the
# pins over SWD, without the firmware and without the OCTOSPI controller. Separates a
# hardware fault (chip silent) from a firmware/controller fault (chip answers here).
#
#   openocd -f tigard-swd.cfg -f target/stm32h7x.cfg -f psram_probe.tcl
#
# Pins: CS PB10 (pull-up R16), CLK PB2, IO0/SI PD11, IO1/SO PD12. Expected ID: 0D 5D.
# The firmware restarts at the end.
#
# Bench 2026-11-11: the first two ID lines read FF/00 on a healthy chip that sat in QPI
# mode; only the lines after the four-line reset show the ID. FF/00 there too = hardware.

proc rd {addr} { return [lindex [read_memory $addr 32 1] 0] }
proc modify {addr clear set} { mww $addr [expr {([rd $addr] & ~$clear) | $set}] }
proc pin {port n v} { mww [expr {$port + 0x18}] [expr {$v ? (1 << $n) : (1 << ($n + 16))}] }
proc cs {v} { pin 0x58020400 10 $v }
proc clk {v} { pin 0x58020400 2 $v }
proc mosi {v} { pin 0x58020C00 11 $v }
proc miso {} { return [expr {([rd 0x58020C10] >> 12) & 1}] }
proc tx {byte} {
    for {set i 7} {$i >= 0} {incr i -1} {
        mosi [expr {($byte >> $i) & 1}]
        clk 1
        clk 0
    }
}
proc rx {} {
    set b 0
    for {set i 0} {$i < 8} {incr i} {
        set b [expr {($b << 1) | [miso]}]
        clk 1
        clk 0
    }
    return $b
}
proc command {byte} { cs 0; tx $byte; cs 1 }
proc read_id {} {
    cs 0
    tx 0x9F
    tx 0; tx 0; tx 0
    set out ""
    for {set i 0} {$i < 4} {incr i} { append out [format "%02X " [rx]] }
    cs 1
    return $out
}
# SO pull: 1 = up, 2 = down (PUPDR bits 25:24 of GPIOD)
proc so_pull {mode} { modify 0x58020C0C 0x03000000 [expr {$mode << 24}] }

init
reset halt
modify 0x580244E0 0 0x0A                    ;# RCC AHB4ENR: GPIOB, GPIOD

# CS as input first: R16 must pull it to 3V3
modify 0x58020400 0x00300000 0
echo [format "CS line PB10 as input (R16 pull-up, expected 1): %d" [expr {([rd 0x58020410] >> 10) & 1}]]

cs 1
clk 0
mosi 0
modify 0x58020400 0x00300030 0x00100010     ;# PB2, PB10 outputs
modify 0x58020C00 0x03C00000 0x00400000     ;# PD11 output, PD12 input

# Each driven line must follow its own output (a line stuck by a short reads 0/0 or 1/1).
# IO2 PB13 and IO3 PD13 are outputs only for this check.
proc follows {name port n} {
    pin $port $n 1
    set hi [expr {([rd [expr {$port + 0x10}]] >> $n) & 1}]
    pin $port $n 0
    set lo [expr {([rd [expr {$port + 0x10}]] >> $n) & 1}]
    echo [format "line %-3s driven 1/0 reads %d/%d (expected 1/0)" $name $hi $lo]
}
modify 0x58020400 0x0C000000 0x04000000     ;# PB13 output
modify 0x58020C00 0x0C000000 0x04000000     ;# PD13 output
follows CLK 0x58020400 2
follows SI  0x58020C00 11
follows IO2 0x58020400 13
follows IO3 0x58020C00 13
follows CS  0x58020400 10
cs 1
pin 0x58020400 13 1
pin 0x58020C00 13 1

so_pull 1
echo [format "SO idle with pull-up   (expected 1): %d" [miso]]
so_pull 2
echo [format "SO idle with pull-down (expected 0): %d" [miso]]

command 0x66
command 0x99
sleep 5

so_pull 1
echo "ID with pull-up   on SO (expected 0D 5D ..): [read_id]"
so_pull 2
echo "ID with pull-down on SO (expected 0D 5D ..): [read_id]"

# In case the chip sits in QPI mode (READ ID does not exist there and an SPI reset is not
# understood): reset and "exit quad" sent on four lines, then the SPI ID once more.
proc nibble {v} {
    pin 0x58020C00 11 [expr {$v & 1}]
    pin 0x58020C00 12 [expr {($v >> 1) & 1}]
    pin 0x58020400 13 [expr {($v >> 2) & 1}]
    pin 0x58020C00 13 [expr {($v >> 3) & 1}]
    clk 1
    clk 0
}
proc qcommand {byte} { cs 0; nibble [expr {$byte >> 4}]; nibble [expr {$byte & 15}]; cs 1 }
so_pull 0
modify 0x58020C00 0x03000000 0x01000000     ;# PD12 output for the four-line commands
qcommand 0x66
qcommand 0x99
sleep 5
qcommand 0xF5
sleep 5
modify 0x58020C00 0x03000000 0              ;# PD12 input again
pin 0x58020400 13 1
pin 0x58020C00 13 1
mosi 0
command 0x66
command 0x99
sleep 5
so_pull 1
echo "after QPI reset/exit, pull-up   (expected 0D 5D ..): [read_id]"
so_pull 2
echo "after QPI reset/exit, pull-down (expected 0D 5D ..): [read_id]"

reset run
shutdown
