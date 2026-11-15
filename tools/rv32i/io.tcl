# The core block's pins on the sides that face their pads in the chip
set_io_pin_constraint -region bottom:* -pin_names {clk reset}
set_io_pin_constraint -region top:* -pin_names {uart_data*}
set_io_pin_constraint -region right:* -pin_names {uart_valid halted}
