import json, sys
n = int(sys.argv[1]) if len(sys.argv) > 1 else 10000
cats = []
for i in range(n):
    cats.append({
        "name": "Cat%d" % i,
        "attributes": [
            {"name": "A", "type": "Number", "precision": 10, "scale": 0},
            {"name": "B", "type": "String", "length": 25},
        ],
    })
json.dump({"name": "Big", "catalogs": cats}, sys.stdout)
