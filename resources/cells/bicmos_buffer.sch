{
    "components": [
        {
            "id": "M1",
            "name": "M1",
            "type": "PDK_NFET",
            "x": -40,
            "y": 0,
            "rotation": 0,
            "properties": {}
        },
        {
            "id": "Q1",
            "name": "Q1",
            "type": "NPN",
            "x": 40,
            "y": 0,
            "rotation": 0,
            "properties": {}
        },
        {
            "id": "VIN",
            "name": "VIN",
            "type": "Pin",
            "x": -80,
            "y": 0,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "VOUT",
            "name": "VOUT",
            "type": "Pin",
            "x": 80,
            "y": 0,
            "rotation": 0,
            "properties": {
                "direction": "Output"
            }
        },
        {
            "id": "VDD",
            "name": "VDD",
            "type": "Pin",
            "x": 0,
            "y": -80,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "GND",
            "name": "GND",
            "type": "Pin",
            "x": 0,
            "y": 80,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "R_DRAIN",
            "name": "R_D",
            "type": "Resistor",
            "x": 0,
            "y": -40,
            "rotation": 90,
            "properties": {
                "R": "10k"
            }
        }
    ],
    "wires": [
        {
            "netName": "vin_net",
            "points": [
                {
                    "x": -80,
                    "y": 0
                },
                {
                    "x": -60,
                    "y": 0
                }
            ]
        },
        {
            "netName": "inter_net",
            "points": [
                {
                    "x": -40,
                    "y": -20
                },
                {
                    "x": 20,
                    "y": 0
                }
            ]
        },
        {
            "netName": "pullup_net",
            "points": [
                {
                    "x": 0,
                    "y": -20
                },
                {
                    "x": 0,
                    "y": -40
                }
            ]
        }
    ],
    "metadata": {
        "description": "BiCMOS Buffer",
        "author": "Auto-Generator",
        "timestamp": "2026-02-01"
    }
}