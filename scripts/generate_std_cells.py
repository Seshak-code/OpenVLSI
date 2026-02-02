import json
import os

OUTPUT_DIR = "resources/cells"
os.makedirs(OUTPUT_DIR, exist_ok=True)

def create_component(id, name, type, x, y, rotation=0, properties=None):
    return {
        "id": id,
        "name": name,
        "type": type,
        "x": x,
        "y": y,
        "rotation": rotation,
        "properties": properties or {}
    }

def create_pin(id, name, direction, x, y):
    return create_component(id, name, "Pin", x, y, 0, {"direction": direction})

def create_wire(net_name, points):
    return {
        "netName": net_name,
        "points": [{"x": p[0], "y": p[1]} for p in points]
    }

def save_schematic(filename, components, wires, description):
    data = {
        "components": components,
        "wires": wires,
        "metadata": {
            "description": description,
            "author": "Auto-Generator",
            "timestamp": "2026-02-01"
        }
    }
    path = os.path.join(OUTPUT_DIR, filename)
    with open(path, 'w') as f:
        json.dump(data, f, indent=4)
    print(f"Generated {path}")

# =============================================================================
# Cell Definitions
# =============================================================================

def gen_darlington():
    # Darlington Pair: Q1, Q2.
    # Pins: B (Base), C (Collector), E (Emitter)
    # Layout: Q1 left, Q2 right.
    
    comps = []
    wires = []
    
    # Components
    comps.append(create_component("Q1", "Q1", "NPN", -40, 0))
    comps.append(create_component("Q2", "Q2", "NPN", 40, 0))
    
    # Pins
    comps.append(create_pin("PIN_B", "B", "Input", -100, 0))    # To Q1 Base
    comps.append(create_pin("PIN_C", "C", "Input", 0, -80))     # Common Collector
    comps.append(create_pin("PIN_E", "E", "Output", 40, 80))    # Q2 Emitter
    
    # Wires
    # 1. Base -> Q1.B (assumed left node of Q1)
    wires.append(create_wire("base_net", [(-100, 0), (-60, 0)])) # Pin B -> Q1 Base
    
    # 2. Q1 Emitter -> Q2 Base
    # Assuming Q1 Emitter is bottom (-40, 20) and Q2 Base is left (20, 0)
    # Note: Primitive NPN generic coords need to be known. Assuming standard symbol:
    # C: (0, -20), B: (-20, 0), E: (0, 20) relative to center.
    wires.append(create_wire("inter_net", [(-40, 20), (-40, 40), (20, 40), (20, 0)]))
    
    # 3. Common Collector: Pin C -> Q1.C and Q2.C
    wires.append(create_wire("coll_net", [(0, -80), (-40, -80), (-40, -20)])) # To Q1 C
    wires.append(create_wire("coll_net", [(0, -80), (40, -80), (40, -20)]))   # To Q2 C
    
    # 4. Emitter: Q2.E -> Pin E
    wires.append(create_wire("emit_net", [(40, 20), (40, 80)]))

    save_schematic("darlington_pair.sch", comps, wires, "Generic NPN Darlington Pair")

def gen_divider():
    # Resistive Divider: R1, R2
    # Pins: VIN, VOUT, GND
    
    comps = []
    wires = []
    
    comps.append(create_component("R1", "R1", "Resistor", 0, -40, 90, {"R": "10k"}))
    comps.append(create_component("R2", "R2", "Resistor", 0, 40, 90, {"R": "10k"}))
    
    comps.append(create_pin("PIN_VIN", "VIN", "Input", 0, -100))
    comps.append(create_pin("PIN_VOUT", "VOUT", "Output", 60, 0))
    comps.append(create_pin("PIN_GND", "GND", "Input", 0, 100))
    
    # Wiring
    # VIN -> R1 top
    wires.append(create_wire("vin_net", [(0, -100), (0, -60)]))
    
    # R1 bot -> R2 top -> VOUT
    wires.append(create_wire("vout_net", [(0, -20), (0, 20)])) # Join resistors
    wires.append(create_wire("vout_net", [(0, 0), (60, 0)]))   # To Pin
    
    # R2 bot -> GND
    wires.append(create_wire("gnd_net", [(0, 60), (0, 100)]))

    save_schematic("resistive_divider.sch", comps, wires, "10k:10k Voltage Divider")

def gen_rectifier():
    # Half-Wave Rectifier: D1, R_LOAD
    comps = []
    wires = []
    
    comps.append(create_component("D1", "D1", "Diode", -20, -40, 0)) # Rotate 0 usually flows right? assume vertical for now or check. 
    # Let's assume Diode flows down by default or use rotation. 
    # Standard Diode: Anode top, Cathode bottom.
    
    comps.append(create_component("R1", "R_LOAD", "Resistor", 20, 0, 90, {"R": "100k"}))
    
    comps.append(create_pin("PIN_VIN", "VIN", "Input", -60, -40))
    comps.append(create_pin("PIN_VOUT", "VOUT", "Output", 60, -40))
    comps.append(create_pin("PIN_GND", "GND", "Input", 0, 60))
    
    # Actually let's put Diode horizontal data flow.
    # D1 at (0, 0), rotation -90 (Points right)
    comps = []
    comps.append(create_component("D1", "D1", "Diode", 0, 0, -90)) # Anode left, Cathode right
    comps.append(create_component("R1", "R_LOAD", "Resistor", 40, 0, 90, {"R": "100k"})) # Vertical R
    
    comps.append(create_pin("PIN_VIN", "VIN", "Input", -60, 0))
    comps.append(create_pin("PIN_VOUT", "VOUT", "Output", 80, 0))
    comps.append(create_pin("PIN_GND", "GND", "Input", 40, 60))
    
    wires = []
    # VIN -> D1 Anode
    wires.append(create_wire("in_net", [(-60, 0), (-20, 0)]))
    
    # D1 Cathode -> R1 Top -> VOUT
    wires.append(create_wire("out_net", [(20, 0), (40, 0), (80, 0)])) # R1 Top assumed at (40, -20)? 
    # Resistor 90deg: Top is (x, y-20), Bot is (x, y+20).
    wires.append(create_wire("out_net", [(40, 0), (40, -20)])) 
    
    # R1 Bot -> GND
    wires.append(create_wire("gnd_net", [(40, 20), (40, 60)]))
    
    save_schematic("half_wave_rectifier.sch", comps, wires, "Half-Wave Rectifier")

def gen_rc_lowpass():
    comps = []
    wires = []
    
    # Series R, Shunt C
    comps.append(create_component("R1", "R1", "Resistor", 0, 0, 0)) # Horizontal
    comps.append(create_component("C1", "C1", "Capacitor", 40, 0, 90, {"C": "1nF"})) # Vertical
    
    comps.append(create_pin("PIN_VIN", "VIN", "Input", -60, 0))
    comps.append(create_pin("PIN_VOUT", "VOUT", "Output", 80, 0))
    comps.append(create_pin("PIN_GND", "GND", "Input", 40, 60))
    
    # Wiring
    # VIN -> R1 Left
    wires.append(create_wire("in_net", [(-60, 0), (-20, 0)]))
    
    # R1 Right -> C1 Top -> VOUT
    wires.append(create_wire("mid_net", [(20, 0), (40, 0), (80, 0)]))
    wires.append(create_wire("mid_net", [(40, 0), (40, -20)])) # C1 Top
    
    # C1 Bot -> GND
    wires.append(create_wire("gnd_net", [(40, 20), (40, 60)]))
    
    save_schematic("rc_low_pass.sch", comps, wires, "RC Low Pass Filter")

def gen_pdk_nfeet():
    # Simply a wrapper around PDK_NFET primitive
    comps = []
    wires = []
    comps.append(create_component("M1", "M1", "PDK_NFET", 0, 0, 0, {"Model": "nfet_01v8"}))
    
    # Pins D, G, S, B mapping to internal nodes
    comps.append(create_pin("D", "D", "Input", 0, -40))
    comps.append(create_pin("S", "S", "Input", 0, 40))
    comps.append(create_pin("G", "G", "Input", -40, 0))
    comps.append(create_pin("B", "B", "Input", 40, 0)) 
    
    # Wires (NFET: D top, S bot, G left, B right implicit)
    wires.append(create_wire("d_net", [(0, -40), (0, -20)]))
    wires.append(create_wire("s_net", [(0, 40), (0, 20)]))
    wires.append(create_wire("g_net", [(-40, 0), (-20, 0)]))
    
    # Body usually on right side of symbol
    wires.append(create_wire("b_net", [(40, 0), (20, 0)]))
    
    save_schematic("pdk_nfet.sch", comps, wires, "Standard NFET Wrapper")

def gen_pdk_pfet():
    comps = []
    wires = []
    comps.append(create_component("M1", "M1", "PDK_PFET", 0, 0, 0, {"Model": "pfet_01v8"}))
    comps.append(create_pin("D", "D", "Input", 0, -40))
    comps.append(create_pin("S", "S", "Input", 0, 40))
    comps.append(create_pin("G", "G", "Input", -40, 0))
    comps.append(create_pin("B", "B", "Input", 40, 0)) 
    wires.append(create_wire("d_net", [(0, -40), (0, -20)]))
    wires.append(create_wire("s_net", [(0, 40), (0, 20)]))
    wires.append(create_wire("g_net", [(-40, 0), (-20, 0)]))
    wires.append(create_wire("b_net", [(40, 0), (20, 0)]))
    save_schematic("pdk_pfet.sch", comps, wires, "Standard PFET Wrapper")

def gen_diff_pair():
    # Diff Pair: Q1, Q2, RC1, RC2, RE, RB1, RB2
    comps = []
    
    # Q1 Left (-40, 0), Q2 Right (40, 0)
    comps.append(create_component("Q1", "Q1", "NPN", -40, 0))
    comps.append(create_component("Q2", "Q2", "NPN", 40, 0))
    
    # RC Loads (Top)
    comps.append(create_component("RC1", "RC1", "Resistor", -40, -60, 90, {"R": "10k"}))
    comps.append(create_component("RC2", "RC2", "Resistor", 40, -60, 90, {"R": "10k"}))
    
    # Tail RE (Bot center)
    comps.append(create_component("RE", "RE", "Resistor", 0, 60, 90, {"R": "5k"}))
    
    # Pins
    comps.append(create_pin("IN_P", "IN+", "Input", -100, 0))
    comps.append(create_pin("IN_N", "IN-", "Input", 100, 0))
    comps.append(create_pin("OUT_P", "OUT+", "Output", 40, -100)) # Coll Q2
    comps.append(create_pin("OUT_N", "OUT-", "Output", -40, -100)) # Coll Q1
    comps.append(create_pin("VCC", "VCC", "Input", 0, -120))
    comps.append(create_pin("GND", "GND", "Input", 0, 100))
    
    wires = []
    # Base connections
    wires.append(create_wire("b1_net", [(-100, 0), (-60, 0)]))
    wires.append(create_wire("b2_net", [(100, 0), (60, 0)])) # Q2 Base is on left side though? 
    # Q2 at (40,0). Base is at (20,0). Pin IN- at (100,0). Need to route around?
    # Actually Q2 should be mirrored if typical diff pair drawing?
    # Let's just assume standard orientation implies Base Left.
    wires.append(create_wire("b2_net", [(100, 0), (20, 0)])) # Cross through Q2? No visualizer handles overlapping okay.
    
    # Collectors -> Resistors -> VCC
    wires.append(create_wire("c1_net", [(-40, -20), (-40, -40)])) # Q1 C to RC1 bot
    wires.append(create_wire("c2_net", [(40, -20), (40, -40)]))   # Q2 C to RC2 bot
    wires.append(create_wire("vcc_net", [(-40, -80), (0, -80), (40, -80)])) # RC tops join
    wires.append(create_wire("vcc_net", [(0, -80), (0, -120)]))   # To Pin
    
    # Emitters -> Tail
    wires.append(create_wire("e_net", [(-40, 20), (0, 20), (40, 20)])) # Emitters join
    wires.append(create_wire("e_net", [(0, 20), (0, 40)])) # To RE top
    wires.append(create_wire("gnd_net", [(0, 80), (0, 100)])) # RE bot to GND
    
    save_schematic("differential_pair.sch", comps, wires, "BJT Differential Pair")

def gen_bicmos():
    # BiCMOS: M1 (NMOS) input, Q1 (NPN) output
    comps = []
    wires = []
    
    comps.append(create_component("M1", "M1", "PDK_NFET", -40, 0))
    comps.append(create_component("Q1", "Q1", "NPN", 40, 0))
    
    comps.append(create_pin("VIN", "VIN", "Input", -80, 0))
    comps.append(create_pin("VOUT", "VOUT", "Output", 80, 0)) # Q1 Emitter
    comps.append(create_pin("VDD", "VDD", "Input", 0, -80))
    comps.append(create_pin("GND", "GND", "Input", 0, 80))
    
    # Wiring M1 Drain to VDD (via load R usually, but this buffer M1 drives Q1 base?)
    # Per code: M1 D=2, G=1, S=0. Q1 B=2. So M1 Drain drives Q1 Base.
    # R_DRAIN at node 2 pull up to VDD.
    comps.append(create_component("R_DRAIN", "R_D", "Resistor", 0, -40, 90, {"R":"10k"}))
    
    wires.append(create_wire("vin_net", [(-80, 0), (-60, 0)])) # M1 Gate
    wires.append(create_wire("inter_net", [(-40, -20), (20, 0)])) # M1 D to Q1 B (Direct)
    wires.append(create_wire("pullup_net", [(0, -20), (0, -40)])) # Node 2 to R_D bot
    
    save_schematic("bicmos_buffer.sch", comps, wires, "BiCMOS Buffer")

def gen_pdk_res():
    comps = []
    wires = []
    comps.append(create_component("R1", "R1", "Resistor", 0, 0, 90, {"R": "1k"}))
    comps.append(create_pin("A", "A", "Input", 0, -40))
    comps.append(create_pin("B", "B", "Input", 0, 40))
    comps.append(create_pin("SUB", "SUB", "Input", 40, 0))
    wires.append(create_wire("a_net", [(0, -40), (0, -20)]))
    wires.append(create_wire("b_net", [(0, 40), (0, 20)]))
    save_schematic("pdk_res_poly.sch", comps, wires, "PDK Poly Resistor")

def gen_pdk_cap():
    comps = []
    wires = []
    comps.append(create_component("C1", "C1", "Capacitor", 0, 0, 90, {"C": "1pF"}))
    comps.append(create_pin("TOP", "TOP", "Input", 0, -40))
    comps.append(create_pin("BOT", "BOT", "Input", 0, 40))
    wires.append(create_wire("top_net", [(0, -40), (0, -20)]))
    wires.append(create_wire("bot_net", [(0, 40), (0, 20)]))
    save_schematic("pdk_cap_mim.sch", comps, wires, "PDK MIM Capacitor")

if __name__ == "__main__":
    gen_darlington()
    gen_divider()
    gen_rectifier()
    gen_rc_lowpass()
    gen_pdk_nfeet()
    gen_pdk_pfet()
    gen_diff_pair()
    gen_bicmos()
    gen_pdk_res()
    gen_pdk_cap()
