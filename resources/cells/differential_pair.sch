{
    "components": [
        {
            "id": "Q1",
            "name": "Q1",
            "type": "NPN",
            "x": -40,
            "y": 0,
            "rotation": 0,
            "properties": {}
        },
        {
            "id": "Q2",
            "name": "Q2",
            "type": "NPN",
            "x": 40,
            "y": 0,
            "rotation": 0,
            "properties": {}
        },
        {
            "id": "RC1",
            "name": "RC1",
            "type": "Resistor",
            "x": -40,
            "y": -60,
            "rotation": 90,
            "properties": {
                "R": "10k"
            }
        },
        {
            "id": "RC2",
            "name": "RC2",
            "type": "Resistor",
            "x": 40,
            "y": -60,
            "rotation": 90,
            "properties": {
                "R": "10k"
            }
        },
        {
            "id": "RE",
            "name": "RE",
            "type": "Resistor",
            "x": 0,
            "y": 60,
            "rotation": 90,
            "properties": {
                "R": "5k"
            }
        },
        {
            "id": "IN_P",
            "name": "IN+",
            "type": "Pin",
            "x": -100,
            "y": 0,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "IN_N",
            "name": "IN-",
            "type": "Pin",
            "x": 100,
            "y": 0,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "OUT_P",
            "name": "OUT+",
            "type": "Pin",
            "x": 40,
            "y": -100,
            "rotation": 0,
            "properties": {
                "direction": "Output"
            }
        },
        {
            "id": "OUT_N",
            "name": "OUT-",
            "type": "Pin",
            "x": -40,
            "y": -100,
            "rotation": 0,
            "properties": {
                "direction": "Output"
            }
        },
        {
            "id": "VCC",
            "name": "VCC",
            "type": "Pin",
            "x": 0,
            "y": -120,
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
            "y": 100,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        }
    ],
    "wires": [
        {
            "netName": "b1_net",
            "points": [
                {
                    "x": -100,
                    "y": 0
                },
                {
                    "x": -60,
                    "y": 0
                }
            ]
        },
        {
            "netName": "b2_net",
            "points": [
                {
                    "x": 100,
                    "y": 0
                },
                {
                    "x": 60,
                    "y": 0
                }
            ]
        },
        {
            "netName": "b2_net",
            "points": [
                {
                    "x": 100,
                    "y": 0
                },
                {
                    "x": 20,
                    "y": 0
                }
            ]
        },
        {
            "netName": "c1_net",
            "points": [
                {
                    "x": -40,
                    "y": -20
                },
                {
                    "x": -40,
                    "y": -40
                }
            ]
        },
        {
            "netName": "c2_net",
            "points": [
                {
                    "x": 40,
                    "y": -20
                },
                {
                    "x": 40,
                    "y": -40
                }
            ]
        },
        {
            "netName": "vcc_net",
            "points": [
                {
                    "x": -40,
                    "y": -80
                },
                {
                    "x": 0,
                    "y": -80
                },
                {
                    "x": 40,
                    "y": -80
                }
            ]
        },
        {
            "netName": "vcc_net",
            "points": [
                {
                    "x": 0,
                    "y": -80
                },
                {
                    "x": 0,
                    "y": -120
                }
            ]
        },
        {
            "netName": "e_net",
            "points": [
                {
                    "x": -40,
                    "y": 20
                },
                {
                    "x": 0,
                    "y": 20
                },
                {
                    "x": 40,
                    "y": 20
                }
            ]
        },
        {
            "netName": "e_net",
            "points": [
                {
                    "x": 0,
                    "y": 20
                },
                {
                    "x": 0,
                    "y": 40
                }
            ]
        },
        {
            "netName": "gnd_net",
            "points": [
                {
                    "x": 0,
                    "y": 80
                },
                {
                    "x": 0,
                    "y": 100
                }
            ]
        }
    ],
    "metadata": {
        "description": "BJT Differential Pair",
        "author": "Auto-Generator",
        "timestamp": "2026-02-01"
    }
}