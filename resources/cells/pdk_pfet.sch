{
    "components": [
        {
            "id": "M1",
            "name": "M1",
            "type": "PDK_PFET",
            "x": 0,
            "y": 0,
            "rotation": 0,
            "properties": {
                "Model": "pfet_01v8"
            }
        },
        {
            "id": "D",
            "name": "D",
            "type": "Pin",
            "x": 0,
            "y": -40,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "S",
            "name": "S",
            "type": "Pin",
            "x": 0,
            "y": 40,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "G",
            "name": "G",
            "type": "Pin",
            "x": -40,
            "y": 0,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "B",
            "name": "B",
            "type": "Pin",
            "x": 40,
            "y": 0,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        }
    ],
    "wires": [
        {
            "netName": "d_net",
            "points": [
                {
                    "x": 0,
                    "y": -40
                },
                {
                    "x": 0,
                    "y": -20
                }
            ]
        },
        {
            "netName": "s_net",
            "points": [
                {
                    "x": 0,
                    "y": 40
                },
                {
                    "x": 0,
                    "y": 20
                }
            ]
        },
        {
            "netName": "g_net",
            "points": [
                {
                    "x": -40,
                    "y": 0
                },
                {
                    "x": -20,
                    "y": 0
                }
            ]
        },
        {
            "netName": "b_net",
            "points": [
                {
                    "x": 40,
                    "y": 0
                },
                {
                    "x": 20,
                    "y": 0
                }
            ]
        }
    ],
    "metadata": {
        "description": "Standard PFET Wrapper",
        "author": "Auto-Generator",
        "timestamp": "2026-02-01"
    }
}