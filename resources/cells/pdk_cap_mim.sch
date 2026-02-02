{
    "components": [
        {
            "id": "C1",
            "name": "C1",
            "type": "Capacitor",
            "x": 0,
            "y": 0,
            "rotation": 90,
            "properties": {
                "C": "1pF"
            }
        },
        {
            "id": "TOP",
            "name": "TOP",
            "type": "Pin",
            "x": 0,
            "y": -40,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        },
        {
            "id": "BOT",
            "name": "BOT",
            "type": "Pin",
            "x": 0,
            "y": 40,
            "rotation": 0,
            "properties": {
                "direction": "Input"
            }
        }
    ],
    "wires": [
        {
            "netName": "top_net",
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
            "netName": "bot_net",
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
        "description": "PDK MIM Capacitor",
        "author": "Auto-Generator",
        "timestamp": "2026-02-01"
    }
}