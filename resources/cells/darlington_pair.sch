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
            "id": "PIN_B",
            "name": "B",
            "type": "Pin",
            "x": -100,
            "y": 0,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "PIN_C",
            "name": "C",
            "type": "Pin",
            "x": 0,
            "y": -80,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "PIN_E",
            "name": "E",
            "type": "Pin",
            "x": 40,
            "y": 80,
            "rotation": 0,
            "properties": {
                "direction": "Output"
            }
        }
    ],
    "wires": [
        {
            "netName": "base_net",
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
            "netName": "inter_net",
            "points": [
                {
                    "x": -40,
                    "y": 20
                },
                {
                    "x": -40,
                    "y": 40
                },
                {
                    "x": 20,
                    "y": 40
                },
                {
                    "x": 20,
                    "y": 0
                }
            ]
        },
        {
            "netName": "coll_net",
            "points": [
                {
                    "x": 0,
                    "y": -80
                },
                {
                    "x": -40,
                    "y": -80
                },
                {
                    "x": -40,
                    "y": -20
                }
            ]
        },
        {
            "netName": "coll_net",
            "points": [
                {
                    "x": 0,
                    "y": -80
                },
                {
                    "x": 40,
                    "y": -80
                },
                {
                    "x": 40,
                    "y": -20
                }
            ]
        },
        {
            "netName": "emit_net",
            "points": [
                {
                    "x": 40,
                    "y": 20
                },
                {
                    "x": 40,
                    "y": 80
                }
            ]
        }
    ],
    "metadata": {
        "description": "Generic NPN Darlington Pair",
        "author": "Auto-Generator",
        "timestamp": "2026-02-01"
    }
}