{
    "components": [
        {
            "id": "M_NMOS",
            "name": "M_NMOS",
            "type": "PDK_NFET",
            "x": 0,
            "y": 20,
            "rotation": 0,
            "properties": {
                "Model": "nfet_01v8",
                "W": "2u",
                "L": "0.18u"
            }
        },
        {
            "id": "M_PMOS",
            "name": "M_PMOS",
            "type": "PDK_PFET",
            "x": 0,
            "y": -20,
            "rotation": 0,
            "properties": {
                "Model": "pfet_01v8",
                "W": "4u",
                "L": "0.18u"
            }
        },
        {
            "id": "PIN_IN",
            "name": "VIN",
            "type": "Pin",
            "x": -60,
            "y": 0,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "PIN_OUT",
            "name": "VOUT",
            "type": "Pin",
            "x": 60,
            "y": 0,
            "rotation": 0,
            "properties": {
                "direction": "Output"
            }
        },
        {
            "id": "PIN_VDD",
            "name": "VDD",
            "type": "Pin",
            "x": 0,
            "y": -60,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "PIN_GND",
            "name": "GND",
            "type": "Pin",
            "x": 0,
            "y": 60,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        }
    ],
    "wires": [
        {
            "netName": "in_net",
            "points": [ {"x": -60, "y": 0}, {"x": -30, "y": 0} ] 
        },
        {
            "netName": "out_net",
            "points": [ {"x": 30, "y": 0}, {"x": 60, "y": 0} ]
        }
    ],
    "metadata": {
        "description": "CMOS Inverter (File-Based)",
        "author": "System",
        "timestamp": "2026-01-27"
    }
}
