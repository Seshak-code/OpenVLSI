{
    "components": [
        {
            "id": "R1",
            "name": "R1",
            "type": "Resistor",
            "x": 0,
            "y": 0,
            "rotation": 90,
            "properties": {
                "R": "1k"
            }
        },
        {
            "id": "A",
            "name": "A",
            "type": "Pin",
            "x": 0,
            "y": -40,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "B",
            "name": "B",
            "type": "Pin",
            "x": 0,
            "y": 40,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "SUB",
            "name": "SUB",
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
            "netName": "a_net",
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
            "netName": "b_net",
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
        }
    ],
    "metadata": {
        "description": "PDK Poly Resistor",
        "author": "Auto-Generator",
        "timestamp": "2026-02-01"
    }
}