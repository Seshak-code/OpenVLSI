{
    "components": [
        {
            "id": "R1",
            "name": "R1",
            "type": "Resistor",
            "x": 0,
            "y": 0,
            "rotation": 0,
            "properties": {}
        },
        {
            "id": "C1",
            "name": "C1",
            "type": "Capacitor",
            "x": 40,
            "y": 0,
            "rotation": 90,
            "properties": {
                "C": "1nF"
            }
        },
        {
            "id": "PIN_VIN",
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
            "id": "PIN_VOUT",
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
            "id": "PIN_GND",
            "name": "GND",
            "type": "Pin",
            "x": 40,
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
            "points": [
                {
                    "x": -60,
                    "y": 0
                },
                {
                    "x": -20,
                    "y": 0
                }
            ]
        },
        {
            "netName": "mid_net",
            "points": [
                {
                    "x": 20,
                    "y": 0
                },
                {
                    "x": 40,
                    "y": 0
                },
                {
                    "x": 80,
                    "y": 0
                }
            ]
        },
        {
            "netName": "mid_net",
            "points": [
                {
                    "x": 40,
                    "y": 0
                },
                {
                    "x": 40,
                    "y": -20
                }
            ]
        },
        {
            "netName": "gnd_net",
            "points": [
                {
                    "x": 40,
                    "y": 20
                },
                {
                    "x": 40,
                    "y": 60
                }
            ]
        }
    ],
    "metadata": {
        "description": "RC Low Pass Filter",
        "author": "Auto-Generator",
        "timestamp": "2026-02-01"
    }
}