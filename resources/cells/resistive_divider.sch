{
    "components": [
        {
            "id": "R1",
            "name": "R1",
            "type": "Resistor",
            "x": 0,
            "y": -40,
            "rotation": 90,
            "properties": {
                "R": "10k"
            }
        },
        {
            "id": "R2",
            "name": "R2",
            "type": "Resistor",
            "x": 0,
            "y": 40,
            "rotation": 90,
            "properties": {
                "R": "10k"
            }
        },
        {
            "id": "PIN_VIN",
            "name": "VIN",
            "type": "Pin",
            "x": 0,
            "y": -100,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "PIN_VOUT",
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
            "id": "PIN_GND",
            "name": "GND",
            "type": "Pin",
            "x": 0,
            "y": 100,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        }
    ],
    "wires": [
        {
            "netName": "vin_net",
            "points": [
                {
                    "x": 0,
                    "y": -100
                },
                {
                    "x": 0,
                    "y": -60
                }
            ]
        },
        {
            "netName": "vout_net",
            "points": [
                {
                    "x": 0,
                    "y": -20
                },
                {
                    "x": 0,
                    "y": 20
                }
            ]
        },
        {
            "netName": "vout_net",
            "points": [
                {
                    "x": 0,
                    "y": 0
                },
                {
                    "x": 60,
                    "y": 0
                }
            ]
        },
        {
            "netName": "gnd_net",
            "points": [
                {
                    "x": 0,
                    "y": 60
                },
                {
                    "x": 0,
                    "y": 100
                }
            ]
        }
    ],
    "metadata": {
        "description": "10k:10k Voltage Divider",
        "author": "Auto-Generator",
        "timestamp": "2026-02-01"
    }
}